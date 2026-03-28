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

// bgfx imgui backend — declares imguiCreate/Destroy/BeginFrame/EndFrame.
// engine_debug's PUBLIC include path exposes examples/common/imgui/.
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
#include "engine/debug/DebugTexturePanel.h"
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
#include "engine/scene/TransformSystem.h"
#include "engine/threading/ThreadPool.h"
#include "imgui.h"

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
    glm::vec3 pos = {1.8f, 0.5f, 3.0f};
    float yaw = -30.f;
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

// Accumulated scroll forwarded to imguiBeginFrame.  Stored as float so that
// fractional trackpad deltas (e.g. 0.3 per event) accumulate correctly; the
// bgfx imgui backend computes io.MouseWheel from the integer difference between
// successive calls, so we pass the floor-truncated cumulative value.
static float s_imguiScrollF = 0.f;

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

    // Enable bgfx debug text overlay unconditionally for this sample app.
    // Renderer::init() only enables it in debug builds; demos always want it.
    bgfx::setDebug(BGFX_DEBUG_TEXT);

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

    // -- ImGui ----------------------------------------------------------------
    // View 15 (kViewImGui) is reserved for the ImGui overlay; see ViewIds.h.
    imguiCreate(16.f);

    {
        ImGuiIO& io = ImGui::GetIO();

        // Restrict window dragging to the title bar so the content area can
        // receive scroll and click events normally.
        io.ConfigWindowsMoveFromTitleBarOnly = true;

        // The bgfx imgui backend is compiled without USE_ENTRY so it leaves
        // io.KeyMap[] empty.  Wire up the nav keys manually so keyboard
        // scrolling works (arrow keys, Page Up/Down).
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

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    glfwSetScrollCallback(glfwHandle, [](GLFWwindow*, double /*xoff*/, double yoff)
                          { s_imguiScrollF += static_cast<float>(yoff); });

    // On Retina (HiDPI) displays glfwGetCursorPos returns logical pixels while
    // glfwGetFramebufferSize returns physical pixels.  We pass the framebuffer
    // size as ImGui's display size, so cursor coordinates must be scaled to
    // physical pixels before being forwarded to imguiBeginFrame, otherwise
    // ImGui's hit-testing is offset by the DPI scale factor.
    float s_contentScaleX = 1.f, s_contentScaleY = 1.f;
    glfwGetWindowContentScale(glfwHandle, &s_contentScaleX, &s_contentScaleY);

    renderer.endFrame();  // flush resource uploads before loading

    // -- Start async helmet load ----------------------------------------------
    auto helmetHandle = assets.load<GltfAsset>("DamagedHelmet.glb");

    // -- ECS ------------------------------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;
    bool helmetSpawned = false;

    // -- Debug texture panel --------------------------------------------------
    engine::debug::DebugTexturePanel texPanel;

    // -- Input ----------------------------------------------------------------
    GlfwInputBackend inputBackend(glfwHandle);
    InputSystem inputSys(inputBackend);
    InputState inputState;

    bool mouseCaptured = false;
    bool skipMouseFrame = false;

    // -- Light ----------------------------------------------------------------
    // Direction FROM the surface TOWARD the light (see fs_pbr.sc).
    // Key light: upper-right, shining toward -Z to match the DamagedHelmet
    // visor orientation (front-face normals point in -Z).
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(0.5f, 0.8f, -1.0f));
    constexpr float kLightIntens = 12.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};

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

        // Feed ImGui first so WantCaptureMouse is up-to-date before we decide
        // whether to capture the mouse for camera control.
        {
            double mx, my;
            glfwGetCursorPos(glfwHandle, &mx, &my);
            uint8_t imguiButtons = 0;
            if (!mouseCaptured)
            {
                if (glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
                    imguiButtons |= IMGUI_MBUT_LEFT;
                if (glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
                    imguiButtons |= IMGUI_MBUT_RIGHT;
                if (glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
                    imguiButtons |= IMGUI_MBUT_MIDDLE;

                // Feed keyboard nav state so arrow/page keys scroll the panel.
                // Only relevant when ImGui has focus (not during camera capture).
                ImGuiIO& io = ImGui::GetIO();
                static const int kNavKeys[] = {GLFW_KEY_UP,        GLFW_KEY_DOWN, GLFW_KEY_PAGE_UP,
                                               GLFW_KEY_PAGE_DOWN, GLFW_KEY_HOME, GLFW_KEY_END};
                for (int k : kNavKeys)
                    io.KeysDown[k] = (glfwGetKey(glfwHandle, k) == GLFW_PRESS);
            }
            else
            {
                mx = -1.0;
                my = -1.0;
            }
            imguiBeginFrame(static_cast<int32_t>(mx * s_contentScaleX),
                            static_cast<int32_t>(my * s_contentScaleY), imguiButtons,
                            static_cast<int32_t>(s_imguiScrollF), static_cast<uint16_t>(fbW),
                            static_cast<uint16_t>(fbH), -1, kViewImGui);
        }

        // Only capture game mouse when ImGui is not handling it.
        if (!mouseCaptured && !ImGui::GetIO().WantCaptureMouse &&
            (inputState.isMouseButtonPressed(MouseButton::Left) ||
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

            // Scale down roughness so metallic panels show visible specular.
            // The DamagedHelmet ORM texture has roughness near 1.0 everywhere;
            // multiplying by 0.55 brings the smoother areas to ~0.3 where GGX
            // produces a clear specular peak from a single directional light.
            reg.view<MaterialComponent>().each(
                [&](EntityID, MaterialComponent& mc)
                {
                    auto* mat = res.getMaterialMut(mc.material);
                    if (mat)
                        mat->roughness = 0.55f;
                });

            // Populate the debug panel.  DamagedHelmet textures load in this order
            // (per glTF image array index): albedo(0→ID1), ORM(1→ID2),
            // emissive(2→ID3), occlusion(3→ID4), normal(4→ID5).
            static const char* kTexNames[] = {"Albedo", "ORM (G=rough B=metal)", "Emissive",
                                              "Occlusion", "Normal"};
            const uint32_t n = res.textureCount();
            for (uint32_t i = 1; i <= n; ++i)
            {
                bgfx::TextureHandle th = res.getTexture(i);
                const char* name = (i <= 5) ? kTexNames[i - 1] : "Texture";
                char label[64];
                snprintf(label, sizeof(label), "[%u] %s", i, name);
                texPanel.add(th, label);
            }
        }
        else if (helmetState == AssetState::Failed)
        {
            static bool printed = false;
            if (!printed)
            {
                printed = true;
                fprintf(stderr, "helmet_demo: asset load failed: %s\n",
                        assets.error(helmetHandle).c_str());
            }
        }

        // -- Transform system: compose local TRS → world matrices -------------
        transformSys.update(reg);

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
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), shadow.atlasTexture(), W, H, 0.05f, 50.f,
        };
        frame.camPos[0] = cam.pos.x;
        frame.camPos[1] = cam.pos.y;
        frame.camPos[2] = cam.pos.z;
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

        // -- ImGui texture panel ----------------------------------------------
        texPanel.show();
        imguiEndFrame();

        // -- Flip -------------------------------------------------------------
        renderer.endFrame();
    }

    // -- Cleanup --------------------------------------------------------------
    assets.release(helmetHandle);

    imguiDestroy();

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
