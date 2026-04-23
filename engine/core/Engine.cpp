#include "engine/core/Engine.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "engine/input/InputSystem.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ViewIds.h"

#ifndef __ANDROID__
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

#include "engine/input/desktop/GlfwInputBackend.h"
#include "engine/platform/desktop/GlfwWindow.h"
#else
#include <android/configuration.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <bgfx/platform.h>

#include "engine/input/android/AndroidInputBackend.h"
#include "engine/platform/android/AndroidFileSystem.h"
#include "engine/platform/android/AndroidGlobals.h"
#include "engine/platform/android/AndroidGyro.h"
#include "engine/platform/android/AndroidWindow.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SamaEngine", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SamaEngine", __VA_ARGS__)
#endif

namespace engine::core
{

Engine::Engine() = default;

void Engine::setClearColor(uint32_t rgba)
{
    clearColor_ = rgba;
}

Engine::~Engine()
{
    if (initialized_)
    {
        shutdown();
    }
}

// ===========================================================================
// Shared helper: create default textures, load shaders, init shadow renderer.
// Called by both init() and initAndroid().
// ===========================================================================

// (Default textures are now created via RenderResources::createDefaultTextures().)

// ===========================================================================
// Desktop (GLFW) implementation
// ===========================================================================

#ifndef __ANDROID__

bool Engine::init(const EngineDesc& desc)
{
    // -- Window -----------------------------------------------------------
    window_ = platform::createWindow({desc.windowWidth, desc.windowHeight, desc.windowTitle});
    if (!window_)
        return false;

    auto* glfwWin = static_cast<platform::GlfwWindow*>(window_.get());
    glfwHandle_ = glfwWin->glfwHandle();

    // -- Renderer ---------------------------------------------------------
    {
        rendering::RendererDesc rd;
        rd.nativeWindowHandle = window_->nativeWindowHandle();
        rd.nativeDisplayHandle = window_->nativeDisplayHandle();
        rd.width = desc.windowWidth;
        rd.height = desc.windowHeight;
        rd.headless = false;
        if (!renderer_.init(rd))
            return false;
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);

    // -- Shader programs --------------------------------------------------
    pbrProg_ = rendering::loadPbrProgram();
    shadowProg_ = rendering::loadShadowProgram();
    skinnedPbrProg_ = rendering::loadSkinnedPbrProgram();
    skinnedShadowProg_ = rendering::loadSkinnedShadowProgram();

    // -- Default textures -------------------------------------------------
    resources_.createDefaultTextures();

    // -- Shadow renderer --------------------------------------------------
    {
        rendering::ShadowDesc sd;
        sd.resolution = desc.shadowResolution;
        sd.cascadeCount = desc.shadowCascades;
        shadow_.init(sd);
    }

    // -- ImGui ------------------------------------------------------------
    imguiCreate(16.f);

    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = true;

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    glfwSetWindowUserPointer(glfwHandle_, this);
    glfwSetScrollCallback(glfwHandle_,
                          [](GLFWwindow* win, double /*xoff*/, double yoff)
                          {
                              auto* eng = static_cast<Engine*>(glfwGetWindowUserPointer(win));
                              if (eng)
                              {
                                  eng->imguiScrollAccum() += static_cast<float>(yoff);
                              }
                          });

    // -- DPI content scale ------------------------------------------------
    glfwGetWindowContentScale(glfwHandle_, &contentScaleX_, &contentScaleY_);

    // -- Input ------------------------------------------------------------
    inputBackend_ = std::make_unique<input::GlfwInputBackend>(glfwHandle_);
    inputSys_ = std::make_unique<input::InputSystem>(*inputBackend_);

    // -- Frame arena ------------------------------------------------------
    frameArena_ = std::make_unique<memory::FrameArena>(desc.frameArenaSize);

    // -- Timing -----------------------------------------------------------
    prevTime_ = glfwGetTime();

    // Flush initial resource uploads.
    renderer_.endFrame();

    initialized_ = true;
    return true;
}

void Engine::shutdown()
{
    if (!initialized_)
        return;

    imguiDestroy();

    shadow_.shutdown();

    if (bgfx::isValid(pbrProg_))
        bgfx::destroy(pbrProg_);
    if (bgfx::isValid(shadowProg_))
        bgfx::destroy(shadowProg_);
    if (bgfx::isValid(skinnedPbrProg_))
        bgfx::destroy(skinnedPbrProg_);
    if (bgfx::isValid(skinnedShadowProg_))
        bgfx::destroy(skinnedShadowProg_);

    resources_.destroyAll();

    renderer_.endFrame();
    renderer_.shutdown();

    // Reset unique_ptrs (input, frame arena).
    inputSys_.reset();
    inputBackend_.reset();
    frameArena_.reset();

    window_.reset();

    initialized_ = false;
}

bool Engine::beginFrame(float& outDt)
{
    if (window_->shouldClose())
        return false;

    // -- Timing -----------------------------------------------------------
    double now = glfwGetTime();
    outDt = static_cast<float>(std::min(now - prevTime_, 0.05));
    prevTime_ = now;

    // -- Events -----------------------------------------------------------
    window_->pollEvents();

    // -- Resize -----------------------------------------------------------
    int fbW, fbH;
    glfwGetFramebufferSize(glfwHandle_, &fbW, &fbH);
    if ((fbW != static_cast<int>(fbW_) || fbH != static_cast<int>(fbH_)) && fbW > 0 && fbH > 0)
    {
        renderer_.resize(static_cast<uint32_t>(fbW), static_cast<uint32_t>(fbH));
        fbW_ = static_cast<uint16_t>(fbW);
        fbH_ = static_cast<uint16_t>(fbH);
    }
    if (fbW <= 0 || fbH <= 0)
    {
        // Minimized -- still need to call endFrame to keep bgfx happy.
        renderer_.endFrame();
        outDt = 0.f;
        return true;  // not closing, just minimized
    }

    // -- Input ------------------------------------------------------------
    inputSys_->update(inputState_);

    // -- ImGui begin frame ------------------------------------------------
    {
        double mx, my;
        glfwGetCursorPos(glfwHandle_, &mx, &my);

        uint8_t imguiButtons = 0;
        if (glfwGetMouseButton(glfwHandle_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
            imguiButtons |= IMGUI_MBUT_LEFT;
        if (glfwGetMouseButton(glfwHandle_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
            imguiButtons |= IMGUI_MBUT_RIGHT;
        if (glfwGetMouseButton(glfwHandle_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
            imguiButtons |= IMGUI_MBUT_MIDDLE;

        // Feed keyboard nav state for ImGui via the modern AddKeyEvent API.
        {
            ImGuiIO& io = ImGui::GetIO();
            struct GlfwImGuiKeyMapping
            {
                int glfwKey;
                ImGuiKey imguiKey;
            };
            static const GlfwImGuiKeyMapping kNavKeys[] = {
                {GLFW_KEY_UP, ImGuiKey_UpArrow},     {GLFW_KEY_DOWN, ImGuiKey_DownArrow},
                {GLFW_KEY_PAGE_UP, ImGuiKey_PageUp}, {GLFW_KEY_PAGE_DOWN, ImGuiKey_PageDown},
                {GLFW_KEY_HOME, ImGuiKey_Home},      {GLFW_KEY_END, ImGuiKey_End},
            };
            for (const auto& mapping : kNavKeys)
                io.AddKeyEvent(mapping.imguiKey,
                               glfwGetKey(glfwHandle_, mapping.glfwKey) == GLFW_PRESS);
        }

        imguiBeginFrame(static_cast<int32_t>(mx * contentScaleX_),
                        static_cast<int32_t>(my * contentScaleY_), imguiButtons,
                        static_cast<int32_t>(imguiScrollF_), fbW_, fbH_, -1, rendering::kViewImGui);
    }

    // -- View 0 clear / rect / touch --------------------------------------
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor_, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, fbW_, fbH_);
    bgfx::touch(0);

    return true;
}

void Engine::endFrame()
{
    imguiEndFrame();

    if (frameArena_)
        frameArena_->reset();

    renderer_.endFrame();
}

bool Engine::imguiWantsMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

#else  // __ANDROID__

// ===========================================================================
// Android implementation
// ===========================================================================

void Engine::handleAndroidCmd(struct android_app* app, int32_t cmd)
{
    auto* engine = static_cast<Engine*>(app->userData);
    if (!engine)
        return;

    switch (cmd)
    {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window != nullptr)
            {
                engine->androidWindow_->setNativeWindow(app->window);

                // Query display density from AConfiguration.
                AConfiguration* config = AConfiguration_new();
                AConfiguration_fromAssetManager(config, app->activity->assetManager);
                int32_t density = AConfiguration_getDensity(config);
                AConfiguration_delete(config);

                if (density > 0)
                {
                    engine->androidWindow_->setDensity(density);
                    float scale = engine->androidWindow_->contentScale();
                    engine->contentScaleX_ = scale;
                    engine->contentScaleY_ = scale;
                    LOGI("Display density: %d dpi (scale %.2f)", density, scale);
                }

                // If bgfx was already initialized (window recreation after
                // orientation change), reset with the new window handle.
                if (engine->renderer_.isInitialized())
                {
                    uint32_t w = engine->androidWindow_->width();
                    uint32_t h = engine->androidWindow_->height();
                    LOGI("Window recreated: %ux%u — resetting bgfx", w, h);
                    bgfx::PlatformData pd;
                    pd.ndt = nullptr;
                    pd.nwh = app->window;
                    bgfx::setPlatformData(pd);
                    engine->renderer_.resize(w, h);
                    engine->fbW_ = static_cast<uint16_t>(w);
                    engine->fbH_ = static_cast<uint16_t>(h);
                }
            }
            break;

        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            engine->androidWindow_->clearNativeWindow();
            break;

        case APP_CMD_GAINED_FOCUS:
            LOGI("APP_CMD_GAINED_FOCUS");
            engine->focused_ = true;
            break;

        case APP_CMD_LOST_FOCUS:
            LOGI("APP_CMD_LOST_FOCUS");
            engine->focused_ = false;
            break;

        case APP_CMD_CONFIG_CHANGED:
            LOGI("APP_CMD_CONFIG_CHANGED");
            engine->androidWindow_->updateSize();
            break;
    }
}

int32_t Engine::handleAndroidInput(struct android_app* app, AInputEvent* event)
{
    auto* engine = static_cast<Engine*>(app->userData);
    if (!engine || !engine->inputBackend_)
        return 0;

    auto* backend = static_cast<input::AndroidInputBackend*>(engine->inputBackend_.get());
    bool handled = backend->processEvent(event);
    return handled ? 1 : 0;
}

bool Engine::initAndroid(struct android_app* app, const EngineDesc& desc)
{
    androidApp_ = app;
    androidWindow_ = std::make_unique<platform::AndroidWindow>();
    androidGyro_ = std::make_unique<platform::AndroidGyro>();
    androidFileSystem_ = std::make_unique<platform::AndroidFileSystem>(app->activity->assetManager);

    // Store the AAssetManager globally so ShaderLoader can access it.
    platform::setAssetManager(app->activity->assetManager);

    // Register callbacks.
    app->userData = this;
    app->onAppCmd = handleAndroidCmd;
    app->onInputEvent = handleAndroidInput;

    LOGI("Sama Engine — Android init starting");

    // Wait for the native window to become available.
    while (!androidWindow_->isReady())
    {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollAll(-1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
        {
            if (source != nullptr)
                source->process(app, source);
            if (app->destroyRequested)
                return false;
            if (androidWindow_->isReady())
                break;
        }
    }

    // -- Renderer (bgfx with Vulkan via ANativeWindow) --------------------
    {
        bgfx::PlatformData pd;
        pd.ndt = nullptr;
        pd.nwh = androidWindow_->nativeWindow();
        pd.context = nullptr;
        pd.backBuffer = nullptr;
        pd.backBufferDS = nullptr;
        bgfx::setPlatformData(pd);

        rendering::RendererDesc rd;
        rd.nativeWindowHandle = androidWindow_->nativeWindow();
        rd.nativeDisplayHandle = nullptr;
        rd.width = androidWindow_->width();
        rd.height = androidWindow_->height();
        rd.headless = false;
        if (!renderer_.init(rd))
        {
            LOGE("Renderer init failed");
            return false;
        }
    }

    fbW_ = static_cast<uint16_t>(androidWindow_->width());
    fbH_ = static_cast<uint16_t>(androidWindow_->height());

    // -- Shader programs --------------------------------------------------
    pbrProg_ = rendering::loadPbrProgram();
    shadowProg_ = rendering::loadShadowProgram();
    skinnedPbrProg_ = rendering::loadSkinnedPbrProgram();
    skinnedShadowProg_ = rendering::loadSkinnedShadowProgram();

    // -- Default textures -------------------------------------------------
    resources_.createDefaultTextures();

    // -- Shadow renderer --------------------------------------------------
    {
        rendering::ShadowDesc sd;
        sd.resolution = desc.shadowResolution;
        sd.cascadeCount = desc.shadowCascades;
        shadow_.init(sd);
    }

    // -- Input (AndroidInputBackend via IInputBackend interface) -----------
    inputBackend_ = std::make_unique<input::AndroidInputBackend>();
    inputSys_ = std::make_unique<input::InputSystem>(*inputBackend_);

    // -- Gyroscope --------------------------------------------------------
    ALooper* looper = ALooper_forThread();
    if (looper && androidGyro_->init(looper))
    {
        androidGyro_->setEnabled(true);
    }

    // -- Frame arena ------------------------------------------------------
    frameArena_ = std::make_unique<memory::FrameArena>(desc.frameArenaSize);

    // -- Timing -----------------------------------------------------------
    using Clock = std::chrono::steady_clock;
    auto now = Clock::now();
    prevTime_ = std::chrono::duration<double>(now.time_since_epoch()).count();

    // Flush initial resource uploads.
    renderer_.endFrame();

    initialized_ = true;
    LOGI("Sama Engine — Android init complete (%ux%u)", fbW_, fbH_);
    return true;
}

void Engine::shutdown()
{
    if (!initialized_)
        return;

    shadow_.shutdown();

    if (bgfx::isValid(pbrProg_))
        bgfx::destroy(pbrProg_);
    if (bgfx::isValid(shadowProg_))
        bgfx::destroy(shadowProg_);
    if (bgfx::isValid(skinnedPbrProg_))
        bgfx::destroy(skinnedPbrProg_);
    if (bgfx::isValid(skinnedShadowProg_))
        bgfx::destroy(skinnedShadowProg_);

    resources_.destroyAll();

    renderer_.endFrame();
    renderer_.shutdown();

    inputSys_.reset();
    inputBackend_.reset();
    frameArena_.reset();

    if (androidGyro_)
    {
        androidGyro_->shutdown();
        androidGyro_.reset();
    }
    androidFileSystem_.reset();
    androidWindow_.reset();

    if (androidApp_)
    {
        androidApp_->userData = nullptr;
        androidApp_ = nullptr;
    }

    initialized_ = false;
    LOGI("Sama Engine — Android shutdown complete");
}

bool Engine::beginFrame(float& outDt)
{
    // -- Poll Android events ----------------------------------------------
    int events;
    struct android_poll_source* source;

    for (;;)
    {
        // Recompute timeout each iteration: block when not ready to render,
        // non-blocking (0) when the window is up and focused.
        int timeout = (androidWindow_->isReady() && focused_) ? 0 : -1;
        if (ALooper_pollAll(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) < 0)
            break;
        if (source != nullptr)
            source->process(androidApp_, source);
        if (androidApp_->destroyRequested)
            return false;
    }

    if (androidApp_->destroyRequested)
        return false;

    // If window is not ready (e.g. during surface transitions), skip frame.
    if (!androidWindow_->isReady())
    {
        outDt = 0.f;
        return true;
    }

    // -- Resize -----------------------------------------------------------
    uint32_t w = androidWindow_->width();
    uint32_t h = androidWindow_->height();
    if ((w != static_cast<uint32_t>(fbW_) || h != static_cast<uint32_t>(fbH_)) && w > 0 && h > 0)
    {
        renderer_.resize(w, h);
        fbW_ = static_cast<uint16_t>(w);
        fbH_ = static_cast<uint16_t>(h);
    }

    // -- Timing -----------------------------------------------------------
    using Clock = std::chrono::steady_clock;
    auto now = Clock::now();
    double nowSec = std::chrono::duration<double>(now.time_since_epoch()).count();
    outDt = static_cast<float>(std::min(nowSec - prevTime_, 0.05));
    prevTime_ = nowSec;

    // -- Input ------------------------------------------------------------
    inputSys_->update(inputState_);

    // -- Gyroscope --------------------------------------------------------
    if (androidGyro_)
    {
        androidGyro_->update(inputState_);
    }

    // -- Renderer begin (direct-to-backbuffer) ----------------------------
    // Matches all desktop demos: Engine sets up a direct-render frame and any
    // app that wants post-processing calls renderer().beginFrame() +
    // renderer().postProcess().submit() itself. Post-process shaders are
    // available on Android (see android/compile_shaders.sh) so apps can opt
    // in identically to desktop.
    renderer_.beginFrameDirect();

    // -- View 0 clear / rect / touch --------------------------------------
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor_, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, fbW_, fbH_);
    bgfx::touch(0);

    return true;
}

void Engine::endFrame()
{
    if (frameArena_)
        frameArena_->reset();

    renderer_.endFrame();
}

bool Engine::imguiWantsMouse() const
{
    // ImGui is not available on Android.
    return false;
}

#endif  // __ANDROID__

}  // namespace engine::core
