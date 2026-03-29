// Audio Demo — macOS
//
// Demonstrates the audio system with spatial 3D audio, sound categories, and
// volume control.  A listener cube (magenta) is moved with WASD; four corner
// pillars emit looping ambient tones; a spinning center cube plays music.
//
// Controls:
//   WASD                — move listener
//   Left click          — play 3D ping at ground hit point
//   Right click + drag  — orbit camera
//   Scroll              — zoom

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#include "engine/audio/AudioComponents.h"
#include "engine/audio/AudioSystem.h"
#include "engine/audio/IAudioEngine.h"
#include "engine/audio/SoLoudAudioEngine.h"
#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/TransformSystem.h"
#include "imgui.h"

using namespace engine::core;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::audio;
using namespace engine::platform;
using namespace engine::rendering;

// =============================================================================
// Orbit camera
// =============================================================================

struct OrbitCamera
{
    float distance = 20.0f;
    float yaw = 0.0f;
    float pitch = 40.0f;
    glm::vec3 target = {0, 0, 0};

    [[nodiscard]] glm::vec3 position() const
    {
        float r = glm::radians(pitch);
        float y = glm::radians(yaw);
        return target + glm::vec3(distance * std::cos(r) * std::sin(y), distance * std::sin(r),
                                  distance * std::cos(r) * std::cos(y));
    }

    [[nodiscard]] glm::mat4 view() const
    {
        return glm::lookAt(position(), target, glm::vec3(0, 1, 0));
    }
};

// =============================================================================
// Procedural WAV tone generator
// =============================================================================

static std::vector<uint8_t> generateToneWav(float frequencyHz, float durationSec,
                                            float sampleRate = 44100.0f)
{
    uint32_t numSamples = static_cast<uint32_t>(sampleRate * durationSec);
    uint32_t dataSize = numSamples * 2;  // 16-bit = 2 bytes per sample
    uint32_t fileSize = 44 + dataSize;   // WAV header = 44 bytes

    std::vector<uint8_t> wav(fileSize);

    // WAV header
    memcpy(&wav[0], "RIFF", 4);
    uint32_t chunkSize = fileSize - 8;
    memcpy(&wav[4], &chunkSize, 4);
    memcpy(&wav[8], "WAVE", 4);
    memcpy(&wav[12], "fmt ", 4);
    uint32_t subchunk1Size = 16;
    memcpy(&wav[16], &subchunk1Size, 4);
    uint16_t audioFormat = 1;  // PCM
    memcpy(&wav[20], &audioFormat, 2);
    uint16_t numChannels = 1;
    memcpy(&wav[22], &numChannels, 2);
    uint32_t sr = static_cast<uint32_t>(sampleRate);
    memcpy(&wav[24], &sr, 4);
    uint32_t byteRate = sr * 2;
    memcpy(&wav[28], &byteRate, 4);
    uint16_t blockAlign = 2;
    memcpy(&wav[32], &blockAlign, 2);
    uint16_t bitsPerSample = 16;
    memcpy(&wav[34], &bitsPerSample, 2);
    memcpy(&wav[36], "data", 4);
    memcpy(&wav[40], &dataSize, 4);

    // Generate sine wave samples
    int16_t* samples = reinterpret_cast<int16_t*>(&wav[44]);
    for (uint32_t i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / sampleRate;
        float value = std::sin(2.0f * 3.14159265f * frequencyHz * t);
        // Apply fade-in/fade-out envelope to avoid clicks
        float envelope = 1.0f;
        float fadeTime = 0.01f * sampleRate;
        if (i < static_cast<uint32_t>(fadeTime))
            envelope = static_cast<float>(i) / fadeTime;
        if (i > numSamples - static_cast<uint32_t>(fadeTime))
            envelope = static_cast<float>(numSamples - i) / fadeTime;
        samples[i] = static_cast<int16_t>(value * envelope * 16000.0f);
    }

    return wav;
}

// =============================================================================
// Ray-plane intersection (Y=0 ground plane)
// =============================================================================

static bool rayGroundPlane(glm::vec3 origin, glm::vec3 dir, glm::vec3& hitOut)
{
    if (std::abs(dir.y) < 1e-6f)
        return false;
    float t = -origin.y / dir.y;
    if (t < 0.0f)
        return false;
    hitOut = origin + dir * t;
    return true;
}

// =============================================================================
// Unproject mouse position to world-space near point
// =============================================================================

static glm::vec3 screenToWorld(float mx, float my, float fbW, float fbH, const glm::mat4& view,
                               const glm::mat4& proj)
{
    float ndcX = (2.0f * mx / fbW) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / fbH);
    glm::vec4 clipNear = glm::vec4(ndcX, ndcY, 0.0f, 1.0f);  // bgfx [0,1] depth
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 worldNear = invVP * clipNear;
    worldNear /= worldNear.w;
    return glm::vec3(worldNear);
}

// =============================================================================
// Entry point
// =============================================================================

static float s_zoomScrollDelta = 0.f;

int main()
{
    Engine eng;
    EngineDesc desc;
    desc.windowTitle = "Audio Demo";
    if (!eng.init(desc))
        return 1;

    // Hook up zoom scroll (piggybacks on Engine's scroll callback via user pointer).
    glfwSetScrollCallback(eng.glfwHandle(),
                          [](GLFWwindow* win, double /*xoff*/, double yoff)
                          {
                              auto* e = static_cast<Engine*>(glfwGetWindowUserPointer(win));
                              if (e)
                                  e->imguiScrollAccum() += static_cast<float>(yoff);
                              s_zoomScrollDelta += static_cast<float>(yoff);
                          });

    // -- Audio engine ---------------------------------------------------------
    SoLoudAudioEngine audioEngine;
    if (!audioEngine.init())
    {
        fprintf(stderr, "audio_demo: failed to initialize audio engine\n");
        return 1;
    }
    AudioSystem audioSys(audioEngine);

    // -- Generate procedural audio clips --------------------------------------
    auto musicWav = generateToneWav(330.0f, 2.0f);
    uint32_t musicClipId = audioEngine.loadClip(musicWav.data(), musicWav.size());

    auto ambientWav220 = generateToneWav(220.0f, 1.5f);
    uint32_t ambientClip220 = audioEngine.loadClip(ambientWav220.data(), ambientWav220.size());

    auto ambientWav277 = generateToneWav(277.0f, 1.5f);
    uint32_t ambientClip277 = audioEngine.loadClip(ambientWav277.data(), ambientWav277.size());

    auto ambientWav330 = generateToneWav(330.0f, 1.5f);
    uint32_t ambientClip330 = audioEngine.loadClip(ambientWav330.data(), ambientWav330.size());

    auto ambientWav440 = generateToneWav(440.0f, 1.5f);
    uint32_t ambientClip440 = audioEngine.loadClip(ambientWav440.data(), ambientWav440.size());

    auto sfxPingWav = generateToneWav(880.0f, 0.3f);
    uint32_t sfxPingClipId = audioEngine.loadClip(sfxPingWav.data(), sfxPingWav.size());

    auto uiClickWav = generateToneWav(1200.0f, 0.1f);
    uint32_t uiClickClipId = audioEngine.loadClip(uiClickWav.data(), uiClickWav.size());

    // -- ECS & rendering systems ----------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;

    // -- Create cube mesh (shared by all entities) ----------------------------
    MeshData cubeData = makeCubeMeshData();
    Mesh cubeMesh = buildMesh(cubeData);
    uint32_t cubeMeshId = eng.resources().addMesh(std::move(cubeMesh));

    // -- Create materials -----------------------------------------------------
    // Ground plane — dark gray
    Material groundMat;
    groundMat.albedo = {0.3f, 0.3f, 0.3f, 1.0f};
    groundMat.roughness = 0.7f;
    groundMat.metallic = 0.0f;
    uint32_t groundMatId = eng.resources().addMaterial(groundMat);

    // Music box — white
    Material musicMat;
    musicMat.albedo = {1.0f, 1.0f, 1.0f, 1.0f};
    musicMat.roughness = 0.4f;
    musicMat.metallic = 0.0f;
    uint32_t musicMatId = eng.resources().addMaterial(musicMat);

    // Corner pillars
    Material redMat;
    redMat.albedo = {1.0f, 0.2f, 0.2f, 1.0f};
    redMat.roughness = 0.5f;
    redMat.metallic = 0.0f;
    uint32_t redMatId = eng.resources().addMaterial(redMat);

    Material greenMat;
    greenMat.albedo = {0.2f, 1.0f, 0.2f, 1.0f};
    greenMat.roughness = 0.5f;
    greenMat.metallic = 0.0f;
    uint32_t greenMatId = eng.resources().addMaterial(greenMat);

    Material blueMat;
    blueMat.albedo = {0.2f, 0.2f, 1.0f, 1.0f};
    blueMat.roughness = 0.5f;
    blueMat.metallic = 0.0f;
    uint32_t blueMatId = eng.resources().addMaterial(blueMat);

    Material yellowMat;
    yellowMat.albedo = {1.0f, 1.0f, 0.2f, 1.0f};
    yellowMat.roughness = 0.5f;
    yellowMat.metallic = 0.0f;
    uint32_t yellowMatId = eng.resources().addMaterial(yellowMat);

    // Listener — magenta
    Material magentaMat;
    magentaMat.albedo = {1.0f, 0.2f, 1.0f, 1.0f};
    magentaMat.roughness = 0.5f;
    magentaMat.metallic = 0.0f;
    uint32_t magentaMatId = eng.resources().addMaterial(magentaMat);

    // -- Create ground plane entity -------------------------------------------
    EntityID groundEntity = reg.createEntity();
    {
        TransformComponent tc;
        tc.position = {0.0f, 0.0f, 0.0f};
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {12.0f, 0.1f, 12.0f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(groundEntity, tc);
        reg.emplace<WorldTransformComponent>(groundEntity);
        reg.emplace<MeshComponent>(groundEntity, cubeMeshId);
        reg.emplace<MaterialComponent>(groundEntity, groundMatId);
        reg.emplace<VisibleTag>(groundEntity);
        reg.emplace<ShadowVisibleTag>(groundEntity, ShadowVisibleTag{0xFF});
    }

    // -- Create spinning music box (center, white) ----------------------------
    EntityID musicEntity = reg.createEntity();
    {
        TransformComponent tc;
        tc.position = {0.0f, 0.5f, 0.0f};
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {0.6f, 0.6f, 0.6f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(musicEntity, tc);
        reg.emplace<WorldTransformComponent>(musicEntity);
        reg.emplace<MeshComponent>(musicEntity, cubeMeshId);
        reg.emplace<MaterialComponent>(musicEntity, musicMatId);
        reg.emplace<VisibleTag>(musicEntity);
        reg.emplace<ShadowVisibleTag>(musicEntity, ShadowVisibleTag{0xFF});

        AudioSourceComponent asc;
        asc.clipId = musicClipId;
        asc.category = SoundCategory::Music;
        asc.volume = 1.0f;
        asc.minDistance = 1.0f;
        asc.maxDistance = 15.0f;
        // flags: loop=1, spatial=2, autoPlay=4
        asc.flags = 0x01 | 0x02 | 0x04;
        reg.emplace<AudioSourceComponent>(musicEntity, asc);
    }

    // -- Create corner ambient sources ----------------------------------------
    struct CornerDef
    {
        glm::vec3 pos;
        uint32_t matId;
        uint32_t clipId;
    };
    // clang-format off
    CornerDef corners[] = {
        {{-4.0f, 0.5f, -4.0f}, redMatId,    ambientClip220},  // front-left,  220 Hz
        {{ 4.0f, 0.5f, -4.0f}, greenMatId,  ambientClip277},  // front-right, 277 Hz
        {{-4.0f, 0.5f,  4.0f}, blueMatId,   ambientClip330},  // back-left,   330 Hz
        {{ 4.0f, 0.5f,  4.0f}, yellowMatId, ambientClip440},  // back-right,  440 Hz
    };
    // clang-format on

    EntityID cornerEntities[4];
    for (int i = 0; i < 4; i++)
    {
        cornerEntities[i] = reg.createEntity();

        TransformComponent tc;
        tc.position = {corners[i].pos.x, corners[i].pos.y, corners[i].pos.z};
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {0.5f, 0.8f, 0.5f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(cornerEntities[i], tc);
        reg.emplace<WorldTransformComponent>(cornerEntities[i]);
        reg.emplace<MeshComponent>(cornerEntities[i], cubeMeshId);
        reg.emplace<MaterialComponent>(cornerEntities[i], corners[i].matId);
        reg.emplace<VisibleTag>(cornerEntities[i]);
        reg.emplace<ShadowVisibleTag>(cornerEntities[i], ShadowVisibleTag{0xFF});

        AudioSourceComponent asc;
        asc.clipId = corners[i].clipId;
        asc.category = SoundCategory::Ambient;
        asc.volume = 1.0f;
        asc.minDistance = 1.0f;
        asc.maxDistance = 10.0f;
        // flags: loop=1, spatial=2, autoPlay=4
        asc.flags = 0x01 | 0x02 | 0x04;
        reg.emplace<AudioSourceComponent>(cornerEntities[i], asc);
    }

    // -- Create listener entity (magenta, small, moves with WASD) -------------
    EntityID listenerEntity = reg.createEntity();
    {
        TransformComponent tc;
        tc.position = {0.0f, 0.3f, 3.0f};
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {0.3f, 0.3f, 0.3f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(listenerEntity, tc);
        reg.emplace<WorldTransformComponent>(listenerEntity);
        reg.emplace<MeshComponent>(listenerEntity, cubeMeshId);
        reg.emplace<MaterialComponent>(listenerEntity, magentaMatId);
        reg.emplace<VisibleTag>(listenerEntity);
        reg.emplace<ShadowVisibleTag>(listenerEntity, ShadowVisibleTag{0xFF});

        AudioListenerComponent alc;
        alc.priority = 0;
        reg.emplace<AudioListenerComponent>(listenerEntity, alc);
    }

    // -- Light ----------------------------------------------------------------
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
    constexpr float kLightIntens = 6.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};

    const glm::vec3 kLightPos = kLightDir * 20.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-14.f, 14.f, -14.f, 14.f, 0.1f, 50.f);

    // -- Camera and interaction state -----------------------------------------
    OrbitCamera cam;
    bool rightDragging = false;
    double prevMouseX = 0.0, prevMouseY = 0.0;

    // Volume state for ImGui sliders
    float masterVolume = 1.0f;
    float musicVolume = 1.0f;
    float sfxVolume = 1.0f;
    float ambientVolume = 1.0f;
    float uiVolume = 1.0f;

    // -- Main loop ------------------------------------------------------------
    float dt = 0.f;
    while (eng.beginFrame(dt))
    {
        if (eng.fbWidth() == 0 || eng.fbHeight() == 0)
            continue;

        const auto& input = eng.inputState();
        const float fbW = static_cast<float>(eng.fbWidth());
        const float fbH = static_cast<float>(eng.fbHeight());

        double mx, my;
        glfwGetCursorPos(eng.glfwHandle(), &mx, &my);
        float physMx = static_cast<float>(mx * eng.contentScaleX());
        float physMy = static_cast<float>(my * eng.contentScaleY());

        bool imguiWants = eng.imguiWantsMouse();

        // -- WASD listener movement -------------------------------------------
        {
            constexpr float kMoveSpeed = 5.0f;
            auto* tc = reg.get<TransformComponent>(listenerEntity);
            if (tc)
            {
                if (input.isKeyHeld(Key::W))
                    tc->position.z -= kMoveSpeed * dt;
                if (input.isKeyHeld(Key::S))
                    tc->position.z += kMoveSpeed * dt;
                if (input.isKeyHeld(Key::A))
                    tc->position.x -= kMoveSpeed * dt;
                if (input.isKeyHeld(Key::D))
                    tc->position.x += kMoveSpeed * dt;
                tc->flags |= 1;  // mark dirty
            }
        }

        // -- Spin music box ---------------------------------------------------
        {
            auto* tc = reg.get<TransformComponent>(musicEntity);
            if (tc)
            {
                // 1 revolution per 4 seconds = 2*pi/4 rad/s
                float angularSpeed = 2.0f * 3.14159265f / 4.0f;
                glm::quat spin = glm::angleAxis(angularSpeed * dt, glm::vec3(0.0f, 1.0f, 0.0f));
                tc->rotation = spin * tc->rotation;
                tc->flags |= 1;
            }
        }

        // -- Camera orbit (right-drag) and zoom (scroll) ----------------------
        if (!imguiWants)
        {
            bool rightDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            if (rightDown)
            {
                if (rightDragging)
                {
                    double dx = mx - prevMouseX;
                    double dy = my - prevMouseY;
                    cam.yaw += static_cast<float>(dx) * 0.3f;
                    cam.pitch += static_cast<float>(dy) * 0.3f;
                    cam.pitch = glm::clamp(cam.pitch, -89.0f, 89.0f);
                }
                rightDragging = true;
            }
            else
            {
                rightDragging = false;
            }

            if (std::abs(s_zoomScrollDelta) > 0.01f)
            {
                cam.distance -= s_zoomScrollDelta * 1.0f;
                cam.distance = glm::clamp(cam.distance, 5.0f, 60.0f);
            }
        }

        // -- Click-to-ping (fire-and-forget 3D sound) -------------------------
        if (!imguiWants && input.isMouseButtonPressed(MouseButton::Left))
        {
            glm::mat4 viewMat = cam.view();
            glm::mat4 projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 100.f);
            glm::vec3 camPos = cam.position();

            glm::vec3 nearPt = screenToWorld(physMx, physMy, fbW, fbH, viewMat, projMat);
            glm::vec3 rayDir = glm::normalize(nearPt - camPos);

            glm::vec3 hitPoint;
            if (rayGroundPlane(camPos, rayDir, hitPoint))
            {
                engine::math::Vec3 hitPos(hitPoint.x, hitPoint.y, hitPoint.z);
                audioEngine.play3D(sfxPingClipId, hitPos, SoundCategory::SFX, 1.0f, false);
            }
        }

        prevMouseX = mx;
        prevMouseY = my;
        s_zoomScrollDelta = 0.f;

        // -- Transform system -------------------------------------------------
        transformSys.update(reg);

        // -- Audio system (after TransformSystem) -----------------------------
        audioSys.update(reg);

        // -- Render -----------------------------------------------------------
        eng.renderer().beginFrameDirect();

        glm::mat4 viewMat = cam.view();
        glm::vec3 camPos = cam.position();
        glm::mat4 projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 100.f);

        // Shadow pass
        eng.shadow().beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, eng.resources(), eng.shadowProgram(), 0);

        // Opaque pass
        const auto W = eng.fbWidth();
        const auto H = eng.fbHeight();

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x1A1A2EFF)
            .transform(viewMat, projMat);

        const glm::mat4 shadowMat = eng.shadow().shadowMatrix(0);
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), eng.shadow().atlasTexture(), W, H, 0.05f, 100.f,
        };
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;
        drawCallSys.update(reg, eng.resources(), eng.pbrProgram(), eng.uniforms(), frame);

        // -- HUD --------------------------------------------------------------
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(1, 1, 0x0f, "Audio Demo  |  %.1f fps  |  %.3f ms",
                            dt > 0 ? 1.f / dt : 0.f, dt * 1000.f);
        bgfx::dbgTextPrintf(1, 2, 0x07, "WASD=move  |  LMB=ping  |  RMB=orbit  |  Scroll=zoom");

        // -- ImGui panel ------------------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 380), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Audio Demo"))
        {
            // Master volume
            if (ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f))
                audioEngine.setMasterVolume(masterVolume);

            ImGui::Separator();

            // Per-category volumes
            if (ImGui::SliderFloat("Music", &musicVolume, 0.0f, 1.0f))
                audioEngine.setCategoryVolume(SoundCategory::Music, musicVolume);

            if (ImGui::SliderFloat("SFX", &sfxVolume, 0.0f, 1.0f))
                audioEngine.setCategoryVolume(SoundCategory::SFX, sfxVolume);

            if (ImGui::SliderFloat("Ambient", &ambientVolume, 0.0f, 1.0f))
                audioEngine.setCategoryVolume(SoundCategory::Ambient, ambientVolume);

            if (ImGui::SliderFloat("UI", &uiVolume, 0.0f, 1.0f))
                audioEngine.setCategoryVolume(SoundCategory::UI, uiVolume);

            ImGui::Separator();

            // Listener position display
            auto* tc = reg.get<TransformComponent>(listenerEntity);
            if (tc)
            {
                ImGui::Text("Listener: (%.1f, %.1f, %.1f)", tc->position.x, tc->position.y,
                            tc->position.z);
            }

            ImGui::Separator();

            // Play UI sound button
            if (ImGui::Button("Play UI Sound"))
            {
                audioEngine.play(uiClickClipId, SoundCategory::UI, 1.0f, false);
            }

            ImGui::Separator();

            ImGui::Text("Controls:");
            ImGui::Text("  WASD  - move listener");
            ImGui::Text("  Left click - play 3D ping");
            ImGui::Text("  Right drag - orbit camera");
            ImGui::Text("  Scroll - zoom");
        }
        ImGui::End();

        eng.endFrame();
    }

    // -- Cleanup --------------------------------------------------------------
    audioEngine.stopAll();
    audioEngine.shutdown();

    return 0;
}
