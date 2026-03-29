// Animation Demo — macOS
//
// Loads a skinned glTF model model and plays its walk animation with ImGui
// controls for play/pause, speed, and playback scrubbing.
//
// Controls:
//   Right-drag  — orbit camera
//   Scroll      — zoom
//   ImGui panel — play/pause, stop, speed slider, progress

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSystem.h"
#include "engine/assets/AssetManager.h"
#include "engine/assets/GltfAsset.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/GltfSceneSpawner.h"
#include "engine/assets/StdFileSystem.h"
#include "engine/assets/TextureLoader.h"
#include "engine/ecs/Registry.h"
#include "engine/input/InputState.h"
#include "engine/input/InputSystem.h"
#include "engine/input/Key.h"
#include "engine/input/desktop/GlfwInputBackend.h"
#include "engine/memory/FrameArena.h"
#include "engine/platform/Window.h"
#include "engine/platform/desktop/GlfwWindow.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShadowRenderer.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/TransformSystem.h"
#include "engine/threading/ThreadPool.h"
#include "imgui.h"

using namespace engine::animation;
using namespace engine::assets;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::platform;
using namespace engine::rendering;
using namespace engine::threading;

// =============================================================================
// Orbit camera
// =============================================================================

struct OrbitCamera
{
    float distance = 5.0f;
    float yaw = 0.0f;
    float pitch = 15.0f;
    glm::vec3 target = {0, 0.5f, 0};

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
// Entry point
// =============================================================================

static float s_imguiScrollF = 0.f;
static float s_zoomScrollDelta = 0.f;

int main()
{
    constexpr uint32_t kInitW = 1280;
    constexpr uint32_t kInitH = 720;

    // -- Window ---------------------------------------------------------------
    auto window = createWindow({kInitW, kInitH, "Animation Demo"});
    if (!window)
        return 1;

    auto* glfwWin = static_cast<GlfwWindow*>(window.get());
    GLFWwindow* glfwHandle = glfwWin->glfwHandle();

    // -- Renderer -------------------------------------------------------------
    Renderer renderer;
    {
        RendererDesc rd;
        rd.nativeWindowHandle = window->nativeWindowHandle();
        rd.nativeDisplayHandle = window->nativeDisplayHandle();
        rd.width = kInitW;
        rd.height = kInitH;
        rd.headless = false;
        if (!renderer.init(rd))
            return 1;
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);

    // -- Asset system ---------------------------------------------------------
    ThreadPool threadPool(2);
    StdFileSystem fileSystem(".");
    AssetManager assets(threadPool, fileSystem);
    assets.registerLoader(std::make_unique<TextureLoader>());
    assets.registerLoader(std::make_unique<GltfLoader>());

    // -- GPU resources --------------------------------------------------------
    bgfx::ProgramHandle shadowProg = loadShadowProgram();
    bgfx::ProgramHandle pbrProg = loadPbrProgram();
    bgfx::ProgramHandle skinnedPbrProg = loadSkinnedPbrProgram();
    bgfx::ProgramHandle skinnedShadowProg = loadSkinnedShadowProgram();

    RenderResources res;

    const uint8_t kWhite[4] = {255, 255, 255, 255};
    bgfx::TextureHandle whiteTex =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                              bgfx::copy(kWhite, sizeof(kWhite)));
    res.setWhiteTexture(whiteTex);

    const uint8_t kNeutralNormal[4] = {128, 128, 255, 255};
    bgfx::TextureHandle neutralNormalTex =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                              bgfx::copy(kNeutralNormal, sizeof(kNeutralNormal)));
    res.setNeutralNormalTexture(neutralNormalTex);

    uint8_t cubeFaces[6 * 4];
    for (int i = 0; i < 6 * 4; ++i)
        cubeFaces[i] = 255;
    bgfx::TextureHandle whiteCubeTex =
        bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                bgfx::copy(cubeFaces, sizeof(cubeFaces)));
    res.setWhiteCubeTexture(whiteCubeTex);

    ShadowRenderer shadow;
    {
        ShadowDesc sd;
        sd.resolution = 2048;
        sd.cascadeCount = 1;
        shadow.init(sd);
    }

    // -- ImGui ----------------------------------------------------------------
    imguiCreate(16.f);
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
        io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
        io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
        io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
        io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
        io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
        io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
        io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
        io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
        io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
        io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
        io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
        io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
        io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
        io.KeyMap[ImGuiKey_Insert] = GLFW_KEY_INSERT;
        io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
        io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
        io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
        io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
        io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
        io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    glfwSetScrollCallback(glfwHandle,
                          [](GLFWwindow*, double /*xoff*/, double yoff)
                          {
                              s_imguiScrollF += static_cast<float>(yoff);
                              s_zoomScrollDelta += static_cast<float>(yoff);
                          });

    float s_contentScaleX = 1.f, s_contentScaleY = 1.f;
    glfwGetWindowContentScale(glfwHandle, &s_contentScaleX, &s_contentScaleY);

    renderer.endFrame();  // flush resource uploads

    // -- Start async model load ----------------------------------------------
    auto modelHandle = assets.load<GltfAsset>("BrainStem.glb");

    // -- ECS ------------------------------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;
    AnimationSystem animSys;
    AnimationResources animRes;
    bool modelSpawned = false;

    // -- Ground plane ---------------------------------------------------------
    MeshData cubeData = makeCubeMeshData();
    Mesh cubeMesh = buildMesh(cubeData);
    uint32_t cubeMeshId = res.addMesh(std::move(cubeMesh));

    Material groundMat;
    groundMat.albedo = {0.2f, 0.2f, 0.2f, 1.0f};
    groundMat.roughness = 0.8f;
    groundMat.metallic = 0.0f;
    uint32_t groundMatId = res.addMaterial(groundMat);

    EntityID groundEntity = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = {0.0f, -0.5f, 0.0f};
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {5.0f, 0.1f, 5.0f};
        tc.flags = 1;  // dirty
        reg.emplace<TransformComponent>(groundEntity, tc);
        reg.emplace<WorldTransformComponent>(groundEntity);
        reg.emplace<MeshComponent>(groundEntity, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(groundEntity, MaterialComponent{groundMatId});
        reg.emplace<VisibleTag>(groundEntity);
        reg.emplace<ShadowVisibleTag>(groundEntity, ShadowVisibleTag{0xFF});
    }

    // -- Test shadow cube (static, above ground — should cast a visible shadow)
    EntityID testCube = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = {2.0f, 1.5f, 0.0f};
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {0.5f, 0.5f, 0.5f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(testCube, tc);
        reg.emplace<MeshComponent>(testCube, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(testCube, MaterialComponent{groundMatId});
        reg.emplace<VisibleTag>(testCube);
        reg.emplace<ShadowVisibleTag>(testCube, ShadowVisibleTag{0xFF});
    }

    // -- Light indicator cube (shows light source position) ------------------
    Material lightMat{};
    lightMat.albedo = {1.0f, 0.9f, 0.3f, 1.0f};
    lightMat.emissiveScale = 5.0f;
    lightMat.roughness = 1.0f;
    uint32_t lightMatId = res.addMaterial(lightMat);

    EntityID lightIndicator = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)) * 5.0f;
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {0.15f, 0.15f, 0.15f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(lightIndicator, tc);
        reg.emplace<MeshComponent>(lightIndicator, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(lightIndicator, MaterialComponent{lightMatId});
        reg.emplace<VisibleTag>(lightIndicator);
    }

    // -- Input ----------------------------------------------------------------
    GlfwInputBackend inputBackend(glfwHandle);
    InputSystem inputSys(inputBackend);
    InputState inputState;

    // -- Light ----------------------------------------------------------------
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f));
    constexpr float kLightIntens = 8.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};

    const glm::vec3 kLightPos = kLightDir * 15.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-5.f, 5.f, -5.f, 5.f, 0.1f, 40.f);

    // -- Camera and interaction state -----------------------------------------
    OrbitCamera cam;
    int prevFbW = 0, prevFbH = 0;
    double prevTime = glfwGetTime();

    bool rightDragging = false;
    double prevMouseX = 0.0, prevMouseY = 0.0;

    // -- Frame arena ----------------------------------------------------------
    engine::memory::FrameArena frameArena(2 * 1024 * 1024);

    float renderMs = 0.f;

    // -- Main loop ------------------------------------------------------------
    while (!window->shouldClose())
    {
        double now = glfwGetTime();
        float dt = static_cast<float>(glm::min(now - prevTime, 0.05));
        prevTime = now;

        window->pollEvents();

        int fbW, fbH;
        glfwGetFramebufferSize(glfwHandle, &fbW, &fbH);
        if ((fbW != prevFbW || fbH != prevFbH) && fbW > 0 && fbH > 0)
        {
            renderer.resize(static_cast<uint32_t>(fbW), static_cast<uint32_t>(fbH));
            prevFbW = fbW;
            prevFbH = fbH;
        }
        if (fbW <= 0 || fbH <= 0)
        {
            renderer.endFrame();
            continue;
        }

        // -- Input ------------------------------------------------------------
        inputSys.update(inputState);

        double mx, my;
        glfwGetCursorPos(glfwHandle, &mx, &my);
        float physMx = static_cast<float>(mx * s_contentScaleX);
        float physMy = static_cast<float>(my * s_contentScaleY);

        // -- ImGui begin frame ------------------------------------------------
        {
            uint8_t imguiButtons = 0;
            if (glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
                imguiButtons |= IMGUI_MBUT_LEFT;
            if (glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
                imguiButtons |= IMGUI_MBUT_RIGHT;
            if (glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
                imguiButtons |= IMGUI_MBUT_MIDDLE;

            // Feed keyboard nav state for ImGui.
            ImGuiIO& io = ImGui::GetIO();
            static const int kNavKeys[] = {GLFW_KEY_UP,        GLFW_KEY_DOWN,   GLFW_KEY_PAGE_UP,
                                           GLFW_KEY_PAGE_DOWN, GLFW_KEY_HOME,   GLFW_KEY_END,
                                           GLFW_KEY_ENTER,     GLFW_KEY_ESCAPE, GLFW_KEY_SPACE,
                                           GLFW_KEY_BACKSPACE, GLFW_KEY_TAB,    GLFW_KEY_DELETE,
                                           GLFW_KEY_INSERT,    GLFW_KEY_LEFT,   GLFW_KEY_RIGHT};
            for (int k : kNavKeys)
                io.KeysDown[k] = (glfwGetKey(glfwHandle, k) == GLFW_PRESS);

            imguiBeginFrame(static_cast<int32_t>(physMx), static_cast<int32_t>(physMy),
                            imguiButtons, static_cast<int32_t>(s_imguiScrollF),
                            static_cast<uint16_t>(fbW), static_cast<uint16_t>(fbH), -1, kViewImGui);
        }

        bool imguiWantsMouse = ImGui::GetIO().WantCaptureMouse;

        // -- Camera orbit (right-drag) and zoom (scroll) ----------------------
        if (!imguiWantsMouse)
        {
            bool rightDown = glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
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
                cam.distance -= s_zoomScrollDelta * 0.5f;
                cam.distance = glm::clamp(cam.distance, 1.0f, 20.0f);
            }
        }

        prevMouseX = mx;
        prevMouseY = my;
        s_zoomScrollDelta = 0.f;

        // -- Asset uploads & spawn on Ready -----------------------------------
        assets.processUploads();

        const AssetState modelState = assets.state(modelHandle);
        if (!modelSpawned && modelState == AssetState::Ready)
        {
            const GltfAsset* model = assets.get<GltfAsset>(modelHandle);
            GltfSceneSpawner::spawn(*model, reg, res, animRes);
            modelSpawned = true;

            // Start playing animation by default.
            reg.view<AnimatorComponent>().each(
                [&](EntityID, AnimatorComponent& ac)
                {
                    ac.flags |= AnimatorComponent::kFlagPlaying;
                    ac.flags |= AnimatorComponent::kFlagLooping;
                    ac.speed = 1.0f;
                });
        }
        else if (modelState == AssetState::Failed)
        {
            static bool printed = false;
            if (!printed)
            {
                printed = true;
                fprintf(stderr, "animation_demo: asset load failed: %s\n",
                        assets.error(modelHandle).c_str());
            }
        }

        // -- ImGui controls update AnimatorComponent --------------------------
        // Gather info for the panel before rendering.
        float currentTime = 0.0f;
        float clipDuration = 0.0f;
        float animSpeed = 1.0f;
        bool isPlaying = false;
        uint32_t jointCount = 0;
        const char* clipName = "N/A";

        reg.view<AnimatorComponent>().each(
            [&](EntityID, AnimatorComponent& ac)
            {
                currentTime = ac.playbackTime;
                animSpeed = ac.speed;
                isPlaying = (ac.flags & AnimatorComponent::kFlagPlaying) != 0;

                const AnimationClip* clip = animRes.getClip(ac.clipId);
                if (clip)
                {
                    clipDuration = clip->duration;
                    if (!clip->name.empty())
                        clipName = clip->name.c_str();
                }
            });

        reg.view<SkeletonComponent>().each(
            [&](EntityID, const SkeletonComponent& sc)
            {
                const Skeleton* skel = animRes.getSkeleton(sc.skeletonId);
                if (skel)
                    jointCount = skel->jointCount();
            });

        // -- Animation system update ------------------------------------------
        animSys.update(reg, dt, animRes, frameArena.resource());


        // -- Transform system -------------------------------------------------
        transformSys.update(reg);

        // -- Render -----------------------------------------------------------
        double frameStart = glfwGetTime();
        renderer.beginFrameDirect();

        glm::mat4 viewMat = cam.view();
        glm::vec3 camPos = cam.position();
        glm::mat4 projMat = glm::perspective(
            glm::radians(45.f), static_cast<float>(fbW) / static_cast<float>(fbH), 0.05f, 50.f);

        // Shadow pass (view 0)
        shadow.beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, res, shadowProg, 0);
        // Skinned shadow pass — animated depth with bone matrices.
        const engine::math::Mat4* shadowBones = animSys.boneBuffer();
        if (shadowBones)
            drawCallSys.submitSkinnedShadowDrawCalls(reg, res, skinnedShadowProg, 0, shadowBones);

        // Opaque pass (view 9) to backbuffer
        const auto W = static_cast<uint16_t>(fbW);
        const auto H = static_cast<uint16_t>(fbH);

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x1A1A2EFF)
            .transform(viewMat, projMat);

        const glm::mat4 shadowMat = shadow.shadowMatrix(0);
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), shadow.atlasTexture(), W, H, 0.05f, 50.f,
        };
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;

        // Regular (non-skinned) draw calls (ground plane, etc.)
        drawCallSys.update(reg, res, pbrProg, renderer.uniforms(), frame);

        // Skinned draw calls
        const engine::math::Mat4* boneBuffer = animSys.boneBuffer();
        if (boneBuffer)
        {
            drawCallSys.updateSkinned(reg, res, skinnedPbrProg, renderer.uniforms(), frame,
                                      boneBuffer);
        }

        // -- HUD --------------------------------------------------------------
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(1, 1, 0x0f,
                            "Animation Demo  |  %.1f fps  |  %.3f ms  |  render %.3f ms",
                            dt > 0 ? 1.f / dt : 0.f, dt * 1000.f, renderMs);
        bgfx::dbgTextPrintf(1, 2, 0x07, "RMB=orbit  |  Scroll=zoom  |  Arena: %zu KB / %zu KB",
                            frameArena.bytesUsed() / 1024, frameArena.capacity() / 1024);

        if (modelState == AssetState::Ready)
            bgfx::dbgTextPrintf(1, 3, 0x0a, "BrainStem.glb — Ready");
        else if (modelState == AssetState::Failed)
            bgfx::dbgTextPrintf(1, 3, 0x0c, "BrainStem.glb — FAILED");
        else
            bgfx::dbgTextPrintf(1, 3, 0x0e, "BrainStem.glb — Loading...");

        // -- ImGui panel ------------------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(10, 80), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 260), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Animation Controls"))
        {
            ImGui::Text("Clip: %s", clipName);
            ImGui::Text("Joints: %u", jointCount);
            ImGui::Separator();

            // Play / Pause button
            if (ImGui::Button(isPlaying ? "Pause" : "Play", ImVec2(80, 0)))
            {
                reg.view<AnimatorComponent>().each(
                    [&](EntityID, AnimatorComponent& ac)
                    { ac.flags ^= AnimatorComponent::kFlagPlaying; });
            }
            ImGui::SameLine();

            // Stop button
            if (ImGui::Button("Stop", ImVec2(80, 0)))
            {
                reg.view<AnimatorComponent>().each(
                    [&](EntityID, AnimatorComponent& ac)
                    {
                        ac.playbackTime = 0.0f;
                        ac.flags &= ~AnimatorComponent::kFlagPlaying;
                    });
            }

            ImGui::Separator();

            // Speed slider
            float speedVal = animSpeed;
            if (ImGui::SliderFloat("Speed", &speedVal, 0.0f, 3.0f, "%.2f"))
            {
                reg.view<AnimatorComponent>().each([&](EntityID, AnimatorComponent& ac)
                                                   { ac.speed = speedVal; });
            }

            ImGui::Separator();

            // Playback time display
            ImGui::Text("Time: %.2f / %.2f s", currentTime, clipDuration);

            // Progress bar
            float progress = (clipDuration > 0.0f) ? (currentTime / clipDuration) : 0.0f;
            ImGui::ProgressBar(progress, ImVec2(-1, 0));
        }
        ImGui::End();

        imguiEndFrame();

        // -- Flip -------------------------------------------------------------
        renderer.endFrame();

        double frameEnd = glfwGetTime();
        renderMs = static_cast<float>((frameEnd - frameStart) * 1000.0);

        frameArena.reset();
    }

    // -- Cleanup --------------------------------------------------------------
    assets.release(modelHandle);

    imguiDestroy();

    shadow.shutdown();
    if (bgfx::isValid(shadowProg))
        bgfx::destroy(shadowProg);
    if (bgfx::isValid(pbrProg))
        bgfx::destroy(pbrProg);
    if (bgfx::isValid(skinnedPbrProg))
        bgfx::destroy(skinnedPbrProg);
    if (bgfx::isValid(skinnedShadowProg))
        bgfx::destroy(skinnedShadowProg);
    if (bgfx::isValid(whiteTex))
        bgfx::destroy(whiteTex);
    if (bgfx::isValid(whiteCubeTex))
        bgfx::destroy(whiteCubeTex);
    res.destroyAll();

    renderer.endFrame();
    renderer.shutdown();

    return 0;
}
