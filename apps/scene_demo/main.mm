// Scene Demo — macOS
//
// PBR scene with directional shadows and a free-fly camera.
// Uses engine::platform::IWindow, engine::rendering::Renderer, and the ECS
// rendering systems — no direct GLFW/bgfx initialisation except for mouse
// capture (which requires the GLFW handle from GlfwWindow).
//
// Controls:
//   LMB / RMB  — click to capture mouse (hidden cursor + raw motion)
//   Escape     — release mouse
//   WASD       — move (horizontal)
//   Q / E      — move down / up
//   Shift      — 3× speed
//   F          — toggle HUD

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>  // GLFW_CURSOR_DISABLED, glfwSetInputMode, glfwGetFramebufferSize
#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/assets/AssetManager.h"
#include "engine/assets/GltfLoader.h"
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
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShadowRenderer.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/threading/ThreadPool.h"
#include "engine/ui/DebugHud.h"

using namespace engine::assets;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::platform;
using namespace engine::rendering;
using namespace engine::threading;

// =============================================================================
// Camera
// =============================================================================

struct Camera
{
    glm::vec3 pos = {0.f, 3.f, 9.f};
    float yaw = 0.f;      // degrees, Y-axis rotation
    float pitch = -12.f;  // degrees, X-axis rotation (clamped ±89)

    static constexpr float kBaseSpeed = 5.f;
    static constexpr float kFastMult = 3.5f;
    static constexpr float kSensitivity = 0.18f;  // deg/pixel

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

    void update(const InputState& state, bool mouseActive, float dt)
    {
        if (mouseActive)
        {
            yaw += (float)state.mouseDeltaX() * kSensitivity;
            pitch -= (float)state.mouseDeltaY() * kSensitivity;
            pitch = glm::clamp(pitch, -89.f, 89.f);
        }

        float spd = kBaseSpeed * (state.isKeyHeld(Key::LeftShift) ? kFastMult : 1.f);

        glm::vec3 fwd = forward();
        glm::vec3 rht = right();

        if (state.isKeyHeld(Key::W))
            pos += fwd * spd * dt;
        if (state.isKeyHeld(Key::S))
            pos -= fwd * spd * dt;
        if (state.isKeyHeld(Key::A))
            pos -= rht * spd * dt;
        if (state.isKeyHeld(Key::D))
            pos += rht * spd * dt;
        if (state.isKeyHeld(Key::E))
            pos.y += spd * dt;
        if (state.isKeyHeld(Key::Q))
            pos.y -= spd * dt;
    }
};

// =============================================================================
// Scene definition
// =============================================================================

struct ObjectDesc
{
    glm::vec3 pos;
    glm::vec3 scale;
    float albedo[3];  // RGB
    float roughness;
};

static constexpr ObjectDesc kObjects[] = {
    // Ground plane — wide flat slab
    {{0.f, -0.15f, 0.f}, {14.f, 0.3f, 14.f}, {0.53f, 0.50f, 0.48f}, 0.88f},
    // Central cube
    {{0.f, 0.50f, 0.f}, {1.f, 1.0f, 1.f}, {0.80f, 0.28f, 0.28f}, 0.38f},
    // Tall left pillar
    {{-3.5f, 1.0f, -1.f}, {0.8f, 2.0f, 0.8f}, {0.28f, 0.52f, 0.82f}, 0.28f},
    // Low wide block (right)
    {{2.5f, 0.4f, 1.f}, {2.2f, 0.8f, 2.2f}, {0.72f, 0.65f, 0.18f}, 0.62f},
    // Slender back post
    {{-1.0f, 1.5f, -3.5f}, {0.6f, 3.0f, 0.6f}, {0.38f, 0.72f, 0.38f}, 0.32f},
    // Small front-right cube
    {{1.5f, 0.3f, 3.f}, {0.6f, 0.6f, 0.6f}, {0.75f, 0.75f, 0.75f}, 0.50f},
    // Extra decorative block (far right)
    {{4.f, 0.75f, -2.f}, {1.0f, 1.5f, 1.0f}, {0.60f, 0.40f, 0.70f}, 0.45f},
};

// =============================================================================
// Entry point
// =============================================================================

int main()
{
    constexpr uint32_t kInitW = 1280;
    constexpr uint32_t kInitH = 720;

    // -------------------------------------------------------------------------
    // Window — engine abstraction; GlfwWindow handles glfwInit and CAMetalLayer
    // -------------------------------------------------------------------------
    auto window = createWindow({kInitW, kInitH, "Scene Demo"});
    if (!window)
        return 1;

    // Cast to the concrete type for GLFW-specific operations (mouse capture,
    // raw framebuffer size).  This is safe: createWindow() always returns a
    // GlfwWindow on desktop platforms.
    auto* glfwWin = static_cast<GlfwWindow*>(window.get());
    GLFWwindow* glfwHandle = glfwWin->glfwHandle();

    // -------------------------------------------------------------------------
    // Renderer — handles bgfx::renderFrame(), bgfx::init(), bgfx::shutdown()
    // -------------------------------------------------------------------------
    Renderer renderer;
    {
        RendererDesc rd;
        rd.nativeWindowHandle = window->nativeWindowHandle();  // CAMetalLayer* on macOS
        rd.nativeDisplayHandle = window->nativeDisplayHandle();
        rd.width = kInitW;
        rd.height = kInitH;
        rd.headless = false;
        if (!renderer.init(rd))
            return 1;
    }

    // -------------------------------------------------------------------------
    // Asset system — ThreadPool + StdFileSystem + AssetManager
    // -------------------------------------------------------------------------
    ThreadPool threadPool(2);
    StdFileSystem fileSystem(".");
    AssetManager assets(threadPool, fileSystem);
    assets.registerLoader(std::make_unique<TextureLoader>());
    assets.registerLoader(std::make_unique<GltfLoader>());

    // -------------------------------------------------------------------------
    // GPU resources
    // -------------------------------------------------------------------------
    engine::rendering::ProgramHandle shadowProg = loadShadowProgram();
    engine::rendering::ProgramHandle pbrProg = loadPbrProgram();

    RenderResources res;
    const uint32_t meshId = res.addMesh(buildMesh(makeCubeMeshData()));

    // Default fallback textures (white 2D, neutral normal, white cube)
    res.createDefaultTextures();

    ShadowRenderer shadow;
    {
        ShadowDesc sd;
        sd.resolution = 2048;
        sd.cascadeCount = 1;
        shadow.init(sd);
    }

    renderer.endFrame();  // flush resource uploads

    // -------------------------------------------------------------------------
    // ECS — create one entity per scene object
    // -------------------------------------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;

    for (const auto& obj : kObjects)
    {
        // Register material in the resource table
        Material mat;
        mat.albedo = {obj.albedo[0], obj.albedo[1], obj.albedo[2], 1.0f};
        mat.roughness = obj.roughness;
        mat.metallic = 0.0f;
        const uint32_t matId = res.addMaterial(mat);

        // World transform — TRS baked into the matrix at spawn; static scene
        glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.f), obj.pos), obj.scale);

        EntityID e = reg.createEntity();
        reg.emplace<WorldTransformComponent>(e, WorldTransformComponent{model});
        reg.emplace<MeshComponent>(e, MeshComponent{meshId});
        reg.emplace<MaterialComponent>(e, MaterialComponent{matId});
        reg.emplace<VisibleTag>(e);
        reg.emplace<ShadowVisibleTag>(e, ShadowVisibleTag{1});  // cascade 0
    }

    // -------------------------------------------------------------------------
    // Input
    // -------------------------------------------------------------------------
    GlfwInputBackend inputBackend(glfwHandle);
    InputSystem inputSys(inputBackend);
    InputState inputState;

    bool mouseCaptured = false;
    bool skipMouseFrame = false;  // suppress delta on first captured frame

    // -------------------------------------------------------------------------
    // Light (fixed directional)
    //
    // u_dirLight[0].xyz = direction FROM surface TOWARD the light (see fs_pbr.sc)
    // u_dirLight[1].rgb = colour * intensity
    // -------------------------------------------------------------------------
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(-0.4f, 1.f, -0.6f));
    constexpr float kLightIntens = 2.8f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.90f * kLightIntens, 0.f};

    // Shadow light — orthographic view aligned with kLightDir, covers the scene
    const glm::vec3 kLightPos = kLightDir * 40.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-14.f, 14.f, -14.f, 14.f, 0.1f, 100.f);

    // -------------------------------------------------------------------------
    // Main loop
    // -------------------------------------------------------------------------
    engine::ui::DebugHud hud;
    hud.init();

    Camera cam;
    bool showHud = true;
    int prevFbW = 0;
    int prevFbH = 0;
    double prevTime = glfwGetTime();

    while (!window->shouldClose())
    {
        // -- Time -------------------------------------------------------------
        double now = glfwGetTime();
        float dt = static_cast<float>(glm::min(now - prevTime, 0.05));
        prevTime = now;

        // -- Events -----------------------------------------------------------
        window->pollEvents();

        // -- Framebuffer resize -----------------------------------------------
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

        // Toggle mouse capture
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

        // Suppress the frame right after capture (cursor jumps)
        bool applyMouse = mouseCaptured && !skipMouseFrame;
        skipMouseFrame = false;

        cam.update(inputState, applyMouse, dt);

        // -- Begin frame ------------------------------------------------------
        // beginFrameDirect() routes kViewOpaque to the backbuffer and calls
        // bgfx::touch(0).  The inline Reinhard in fs_pbr.sc handles tonemapping;
        // Phase 7 will remove it and switch to renderer.beginFrame() +
        // postProcess.submit() for the full post-process chain.
        renderer.beginFrameDirect();

        // -- Shadow pass (view 0) ---------------------------------------------
        // Must be called after beginFrameDirect() so that bgfx::touch(0) in
        // beginFrameDirect() doesn't override beginCascade's view rect.
        // bgfx view state is "last write wins" — beginCascade's setViewRect
        // sets the shadow atlas dimensions (2048×2048) which must not be
        // overridden later in the frame.
        shadow.beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, res, bgfx::ProgramHandle{shadowProg.idx}, 0);

        // -- Opaque pass (view 9) —  render to backbuffer --------------------
        const auto W = static_cast<uint16_t>(fbW);
        const auto H = static_cast<uint16_t>(fbH);

        glm::mat4 view = cam.view();
        glm::mat4 proj = glm::perspective(
            glm::radians(60.f), static_cast<float>(fbW) / static_cast<float>(fbH), 0.1f, 200.f);

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x87CEEBFF)
            .transform(view, proj);

        const glm::mat4 shadowMat = shadow.shadowMatrix(0);
        const PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), shadow.atlasTexture(), W, H, 0.1f, 200.f,
        };
        drawCallSys.update(reg, res, bgfx::ProgramHandle{pbrProg.idx}, renderer.uniforms(), frame);

        // -- HUD (debug text overlay) -----------------------------------------
        // Do NOT set setViewRect(0, ...) here — that would override the shadow
        // pass's 2048×2048 atlas viewport set by beginCascade above.
        // bgfx debug text renders to the full backbuffer regardless of view rects.
        hud.begin(static_cast<uint32_t>(fbW), static_cast<uint32_t>(fbH));

        if (showHud)
        {
            hud.printf(1, 1, "Scene Demo  |  %.1f fps  |  %.3f ms", dt > 0 ? 1.f / dt : 0.f,
                       dt * 1000.f);

            hud.printf(1, 2, "Camera  pos (%.1f, %.1f, %.1f)  yaw %.0f  pitch %.0f", cam.pos.x,
                       cam.pos.y, cam.pos.z, cam.yaw, cam.pitch);

            if (mouseCaptured)
                hud.printf(1, 3, "Mouse: CAPTURED   Esc = release");
            else
                hud.printf(1, 3, "Click to capture mouse   F = toggle HUD");

            hud.printf(1, 4, "WASD = move   Q/E = down/up   Shift = fast");
        }

        hud.end();

        // -- Asset uploads (before endFrame so bgfx::frame() submits them this tick) --
        assets.processUploads();

        // -- Flip -------------------------------------------------------------
        renderer.endFrame();
    }

    // -------------------------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------------------------
    hud.shutdown();
    shadow.shutdown();
    if (engine::rendering::isValid(shadowProg))
        bgfx::destroy(bgfx::ProgramHandle{shadowProg.idx});
    if (engine::rendering::isValid(pbrProg))
        bgfx::destroy(bgfx::ProgramHandle{pbrProg.idx});
    res.destroyAll();

    renderer.endFrame();  // flush pending destroy commands
    renderer.shutdown();  // bgfx::shutdown() + uniforms.destroy()

    // window destructor calls glfwDestroyWindow + glfwTerminate
    return 0;
}
