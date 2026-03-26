// Helmet Demo — macOS
//
// Loads the Damaged Helmet glTF (single GLB file) via the async AssetManager,
// spawns ECS entities from the loaded scene, and renders with PBR + directional
// shadow.  Camera orbits the helmet with click-to-capture free-fly fallback.
//
// Controls:
//   LMB / RMB  — click to capture mouse (orbit mode while held)
//   Escape     — release mouse
//   WASD       — free-fly move
//   Q / E      — move down / up
//   Shift      — 3× speed
//   F          — toggle HUD

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
#include "engine/platform/Window.h"
#include "engine/platform/desktop/GlfwWindow.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShadowRenderer.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/threading/ThreadPool.h"

using namespace engine::assets;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::platform;
using namespace engine::rendering;
using namespace engine::threading;

// =============================================================================
// Orbit + free-fly camera
// =============================================================================

struct Camera
{
    glm::vec3 pos = {0.f, 0.5f, 3.5f};
    float yaw = 0.f;
    float pitch = -8.f;

    static constexpr float kBaseSpeed = 3.f;
    static constexpr float kFastMult = 3.5f;
    static constexpr float kSensitivity = 0.18f;

    [[nodiscard]] glm::vec3 forward() const
    {
        float y = glm::radians(yaw);
        float p = glm::radians(pitch);
        return {std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
    }

    [[nodiscard]] glm::vec3 right() const
    {
        return glm::normalize(glm::cross(forward(), glm::vec3(0, 1, 0)));
    }

    [[nodiscard]] glm::mat4 view() const
    {
        return glm::lookAt(pos, pos + forward(), glm::vec3(0, 1, 0));
    }

    void update(const InputState& input, bool mouseActive, float dt)
    {
        if (mouseActive)
        {
            yaw += static_cast<float>(input.mouseDeltaX()) * kSensitivity;
            pitch -= static_cast<float>(input.mouseDeltaY()) * kSensitivity;
            pitch = glm::clamp(pitch, -89.f, 89.f);
        }

        float spd = kBaseSpeed * (input.isKeyHeld(Key::LeftShift) ? kFastMult : 1.f);
        glm::vec3 fwd = forward();
        glm::vec3 rht = right();

        if (input.isKeyHeld(Key::W))
            pos += fwd * spd * dt;
        if (input.isKeyHeld(Key::S))
            pos -= fwd * spd * dt;
        if (input.isKeyHeld(Key::A))
            pos -= rht * spd * dt;
        if (input.isKeyHeld(Key::D))
            pos += rht * spd * dt;
        if (input.isKeyHeld(Key::E))
            pos.y += spd * dt;
        if (input.isKeyHeld(Key::Q))
            pos.y -= spd * dt;
    }
};

// =============================================================================
// Entry point
// =============================================================================

int main()
{
    constexpr uint32_t kInitW = 1280;
    constexpr uint32_t kInitH = 720;

    // -- Window ---------------------------------------------------------------
    auto window = createWindow({kInitW, kInitH, "Damaged Helmet"});
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

    // -- Asset system ---------------------------------------------------------
    // Assets are resolved relative to the binary's working directory.
    // CMake copies DamagedHelmet.glb to the build directory at configure time.
    ThreadPool threadPool(2);
    StdFileSystem fileSystem(".");
    AssetManager assets(threadPool, fileSystem);
    assets.registerLoader(std::make_unique<TextureLoader>());
    assets.registerLoader(std::make_unique<GltfLoader>());

    // -- GPU resources --------------------------------------------------------
    bgfx::ProgramHandle shadowProg = loadShadowProgram();
    bgfx::ProgramHandle pbrProg = loadPbrProgram();

    RenderResources res;

    const uint8_t kWhite[4] = {255, 255, 255, 255};
    bgfx::TextureHandle whiteTex =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                              bgfx::copy(kWhite, sizeof(kWhite)));
    res.setWhiteTexture(whiteTex);

    // 1×1 white cube texture — used as a safe default for unbound IBL cube samplers
    // (s_irradiance slot 6, s_prefiltered slot 7) until real IBL maps are loaded.
    // Six identical faces: +X -X +Y -Y +Z -Z, each 1×1 RGBA8 white.
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

    renderer.endFrame();  // flush resource uploads before loading

    // -- Start async helmet load ----------------------------------------------
    auto helmetHandle = assets.load<GltfAsset>("DamagedHelmet.glb");

    // -- ECS ------------------------------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    bool helmetSpawned = false;

    // -- Input ----------------------------------------------------------------
    GlfwInputBackend inputBackend(glfwHandle);
    InputSystem inputSys(inputBackend);
    InputState inputState;

    bool mouseCaptured = false;
    bool skipMouseFrame = false;

    // -- Light ----------------------------------------------------------------
    // Direction FROM the surface TOWARD the light (see fs_pbr.sc).
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(-0.5f, 1.f, 0.8f));
    constexpr float kLightIntens = 3.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.90f * kLightIntens, 0.f};

    const glm::vec3 kLightPos = kLightDir * 10.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-2.f, 2.f, -2.f, 2.f, 0.1f, 30.f);

    // -- Main loop ------------------------------------------------------------
    Camera cam;
    bool showHud = true;
    int prevFbW = 0;
    int prevFbH = 0;
    double prevTime = glfwGetTime();

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

        if (!mouseCaptured && (inputState.isMouseButtonPressed(MouseButton::Left) ||
                               inputState.isMouseButtonPressed(MouseButton::Right)))
        {
            mouseCaptured = true;
            skipMouseFrame = true;
            glfwSetInputMode(glfwHandle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(glfwHandle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        if (mouseCaptured && inputState.isKeyPressed(Key::Escape))
        {
            mouseCaptured = false;
            glfwSetInputMode(glfwHandle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        if (inputState.isKeyPressed(Key::F))
            showHud = !showHud;

        bool applyMouse = mouseCaptured && !skipMouseFrame;
        skipMouseFrame = false;

        cam.update(inputState, applyMouse, dt);

        // -- Asset uploads & spawn on Ready -----------------------------------
        assets.processUploads();

        const AssetState helmetState = assets.state(helmetHandle);
        if (!helmetSpawned && helmetState == AssetState::Ready)
        {
            const GltfAsset* helmet = assets.get<GltfAsset>(helmetHandle);
            GltfSceneSpawner::spawn(*helmet, reg, res);
            helmetSpawned = true;
        }

        // -- Render -----------------------------------------------------------
        renderer.beginFrameDirect();

        // Shadow pass (view 0)
        shadow.beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, res, shadowProg, 0);

        // Opaque pass (view 9) to backbuffer
        const auto W = static_cast<uint16_t>(fbW);
        const auto H = static_cast<uint16_t>(fbH);

        glm::mat4 view = cam.view();
        glm::mat4 proj = glm::perspective(
            glm::radians(45.f), static_cast<float>(fbW) / static_cast<float>(fbH), 0.05f, 50.f);

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x1A1A2EFF)
            .transform(view, proj);

        const glm::mat4 shadowMat = shadow.shadowMatrix(0);
        const PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), shadow.atlasTexture(), W, H, 0.05f, 50.f,
        };
        drawCallSys.update(reg, res, pbrProg, renderer.uniforms(), frame);

        // -- HUD --------------------------------------------------------------
        bgfx::dbgTextClear();
        if (showHud)
        {
            bgfx::dbgTextPrintf(1, 1, 0x0f, "Helmet Demo  |  %.1f fps  |  %.3f ms",
                                dt > 0 ? 1.f / dt : 0.f, dt * 1000.f);

            if (helmetState == AssetState::Ready)
                bgfx::dbgTextPrintf(1, 2, 0x0a, "DamagedHelmet.glb — Ready");
            else if (helmetState == AssetState::Failed)
                bgfx::dbgTextPrintf(1, 2, 0x0c, "DamagedHelmet.glb — FAILED: %s",
                                    assets.error(helmetHandle).c_str());
            else
                bgfx::dbgTextPrintf(1, 2, 0x0e, "DamagedHelmet.glb — Loading...");

            bgfx::dbgTextPrintf(1, 3, 0x07,
                                "Click to capture mouse  |  WASD/QE  |  F=HUD  |  Esc=release");
        }

        // -- Flip -------------------------------------------------------------
        renderer.endFrame();
    }

    // -- Cleanup --------------------------------------------------------------
    assets.release(helmetHandle);

    shadow.shutdown();
    if (bgfx::isValid(shadowProg))
        bgfx::destroy(shadowProg);
    if (bgfx::isValid(pbrProg))
        bgfx::destroy(pbrProg);
    if (bgfx::isValid(whiteTex))
        bgfx::destroy(whiteTex);
    if (bgfx::isValid(whiteCubeTex))
        bgfx::destroy(whiteCubeTex);
    res.destroyAll();

    renderer.endFrame();
    renderer.shutdown();

    return 0;
}
