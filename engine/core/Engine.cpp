#include "engine/core/Engine.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "engine/input/InputSystem.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ViewIds.h"

#if !defined(__ANDROID__) && !ENGINE_IS_IOS
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

#include "engine/input/desktop/GlfwInputBackend.h"
#include "engine/platform/desktop/GlfwWindow.h"
#elif defined(__ANDROID__)
#include <android/configuration.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <bgfx/platform.h>

#include "engine/audio/NullAudioEngine.h"
#include "engine/audio/SoLoudAudioEngine.h"
#include "engine/input/android/AndroidInputBackend.h"
#include "engine/platform/android/AndroidFileSystem.h"
#include "engine/platform/android/AndroidGlobals.h"
#include "engine/platform/android/AndroidGyro.h"
#include "engine/platform/android/AndroidTierDetect.h"
#include "engine/platform/android/AndroidWindow.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SamaEngine", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SamaEngine", __VA_ARGS__)
#else  // iOS
#include <bgfx/platform.h>

#include <cstdio>

#include "engine/audio/NullAudioEngine.h"
#include "engine/audio/SoLoudAudioEngine.h"
#include "engine/input/ios/IosInputBackend.h"
#include "engine/platform/ios/IosFileSystem.h"
#include "engine/platform/ios/IosGyro.h"
#include "engine/platform/ios/IosTierDetect.h"
#include "engine/platform/ios/IosTouchInput.h"
#include "engine/platform/ios/IosWindow.h"
#endif

namespace engine::core
{

namespace
{
// Boundary helper — Engine stores programs as the bgfx-free
// engine::rendering::ProgramHandle wrapper; bgfx::destroy still wants the
// real bgfx handle.  Layout is asserted bit-identical in RenderPass.cpp so
// the conversion is a no-op reinterpret of the underlying uint16_t.
inline void destroyProgram(rendering::ProgramHandle& h)
{
    if (rendering::isValid(h))
    {
        bgfx::destroy(bgfx::ProgramHandle{h.idx});
        h = rendering::kInvalidProgram;
    }
}
}  // namespace

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

#if !defined(__ANDROID__) && !ENGINE_IS_IOS

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

    destroyProgram(pbrProg_);
    destroyProgram(shadowProg_);
    destroyProgram(skinnedPbrProg_);
    destroyProgram(skinnedShadowProg_);

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

#elif defined(__ANDROID__)  // __ANDROID__

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
                // orientation change OR resume from background), reset with
                // the new window handle.  Vulkan's VkSurfaceKHR is tied to
                // the previous ANativeWindow* and bgfx needs the new handle
                // before the next swapchain image is acquired.
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
            // Surface is going away (user backgrounded the app or rotated
            // the device).  Clear our cached pointer so beginFrame() blocks
            // on ALooper_pollAll until the next APP_CMD_INIT_WINDOW.  The
            // bgfx swap chain is invalid past this point — pushing frames
            // would crash inside vkAcquireNextImageKHR.  bgfx itself is
            // left running so we can rebind a new surface in
            // APP_CMD_INIT_WINDOW without a full re-init.
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

        case APP_CMD_PAUSE:
            // Activity moving to the background.  Pause audio so the user
            // doesn't hear the game while another app is foreground; disable
            // gyro polling to save battery; flip the paused_ flag so
            // beginFrame() blocks on ALooper_pollAll(-1) instead of busy
            // looping.  Mirrors iOS applicationWillResignActive in
            // engine/platform/ios/IosApp.mm.
            LOGI("APP_CMD_PAUSE");
            engine->paused_ = true;
            if (engine->audio_)
            {
                engine->audio_->setPauseAll(true);
            }
            if (engine->androidGyro_)
            {
                engine->androidGyro_->setEnabled(false);
            }
            break;

        case APP_CMD_RESUME:
            // Activity returning to the foreground.  Re-enable gyro and
            // unpause audio.  Note the bgfx surface is rebuilt separately
            // via APP_CMD_TERM_WINDOW / APP_CMD_INIT_WINDOW — those fire
            // around the pause/resume pair when the surface is invalidated
            // (always on background, sometimes not on app switcher).
            LOGI("APP_CMD_RESUME");
            engine->paused_ = false;
            if (engine->androidGyro_)
            {
                engine->androidGyro_->setEnabled(true);
            }
            if (engine->audio_)
            {
                engine->audio_->setPauseAll(false);
            }
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

    // -- Device tier detection --------------------------------------------
    // Mirrors the iOS branch: classify the device (RAM via /proc/meminfo +
    // GPU substring) and log the result so the tier choice is visible in
    // logcat.  GameRunner's runAndroid(configPath) overload feeds the same
    // detection result into ProjectConfig::activeTier when the project
    // didn't specify one (or used the "auto" sentinel) — this call is
    // purely informational so the user can see what was picked even when
    // the project pinned a tier explicitly.  We pass an empty GPU name
    // here because bgfx hasn't been init'd yet (it requires the native
    // window which we're about to wait for); the RAM signal alone is
    // sufficient to distinguish the three tier buckets on real devices.
    {
        const uint64_t ramMb = platform::android::androidTotalRamMb();
        const auto tier = platform::android::detectAndroidTier();
        LOGI("Tier detected: %s (RAM %llu MB)", platform::android::androidTierLogName(tier),
             static_cast<unsigned long long>(ramMb));
    }

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

    // -- Audio (SoLoud via miniaudio -> AAudio / OpenSL ES) ---------------
    // miniaudio's NULL-context init auto-selects the best available Android
    // backend at runtime: AAudio on API 26+ (lower latency, modern), with
    // an automatic fall-back to OpenSL ES on older devices.  Both backends
    // are dlopen()'d (MA_NO_RUNTIME_LINKING is not defined) so we don't
    // need to link libaaudio.so / libOpenSLES.so at compile time.  AAudio
    // requires no manifest permission for output-only playback.
    //
    // On emulator images without an audio route SoLoudAudioEngine::init()
    // returns false and we fall back to NullAudioEngine so games can call
    // engine.audio() unconditionally.  Mirrors the iOS path above.
    {
        auto soloud = std::make_unique<audio::SoLoudAudioEngine>();
        if (soloud->init())
        {
            LOGI("Audio: SoLoud (miniaudio) initialised");
            audio_ = std::move(soloud);
        }
        else
        {
            LOGE("Audio: SoLoud init failed — falling back to NullAudioEngine");
            soloud.reset();
            auto null_audio = std::make_unique<audio::NullAudioEngine>();
            null_audio->init();
            audio_ = std::move(null_audio);
        }
    }

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

    destroyProgram(pbrProg_);
    destroyProgram(shadowProg_);
    destroyProgram(skinnedPbrProg_);
    destroyProgram(skinnedShadowProg_);

    resources_.destroyAll();

    renderer_.endFrame();
    renderer_.shutdown();

    // Audio: shut down before tearing down platform pointers.  Both backends
    // (SoLoud + Null) are safe to shutdown() even if init failed.
    if (audio_)
    {
        audio_->shutdown();
        audio_.reset();
    }

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
        // Recompute timeout each iteration: block when not ready to render
        // (no surface, no focus, or activity paused), non-blocking (0)
        // otherwise.  Blocking on ALooper_pollAll is the canonical Android
        // idle pattern — it lets the OS suspend the thread until the next
        // event, saving battery while the activity is in the background.
        bool canRender = androidWindow_->isReady() && focused_ && !paused_;
        int timeout = canRender ? 0 : -1;
        if (ALooper_pollAll(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) < 0)
            break;
        if (source != nullptr)
            source->process(androidApp_, source);
        if (androidApp_->destroyRequested)
            return false;
    }

    if (androidApp_->destroyRequested)
        return false;

    // If window is not ready (e.g. during surface transitions) or the
    // activity is paused, skip the frame.  GameRunner's loop will see
    // outDt == 0 and avoid advancing fixed-step physics, but it'll still
    // call beginFrame() again so we keep draining APP_CMD_* events and
    // detect APP_CMD_RESUME / APP_CMD_INIT_WINDOW promptly.
    if (!androidWindow_->isReady() || paused_)
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

#else  // ENGINE_IS_IOS

// ===========================================================================
// iOS implementation
//
// Mirrors the Android branch: the platform layer (IosWindow, IosTouchInput,
// IosGyro, IosFileSystem) is owned by IosApp / the application delegate and
// passed in to initIos() via raw pointers.  The Engine does not take
// ownership; it just stores back-pointers and drains them every frame.
//
// Unlike Android, there is no event poll loop here — UIKit pumps the run
// loop and the IosApp delegate calls Engine::beginFrame / endFrame from its
// CADisplayLink callback once per refresh.  beginFrame is therefore
// non-blocking: it just queries the IosWindow for the current drawable size,
// updates the InputState from the touch / gyro helpers, and returns.
// ===========================================================================

bool Engine::initIos(platform::ios::IosWindow* window, platform::ios::IosTouchInput* touch,
                     platform::ios::IosGyro* gyro, platform::ios::IosFileSystem* fs,
                     const EngineDesc& desc)
{
    iosWindow_ = window;
    iosTouch_ = touch;
    iosGyro_ = gyro;
    iosFileSystem_ = fs;

    // -- Device tier detection --------------------------------------------
    // Classify the host device (sysctl hw.machine + NSProcessInfo
    // physicalMemory) and log the result so the tier choice is visible in
    // the simulator console / device logs.  The actual tier-driven render
    // settings flow through ProjectConfig::activeTier in GameRunner — this
    // call is the canonical detection point per IosTierDetect.h.
    {
        const auto tier = platform::ios::detectIosTier();
        std::fprintf(stderr, "[Sama][iOS] tier detected: %s (machine=%s)\n",
                     platform::ios::iosTierLogName(tier), platform::ios::iosMachineIdentifier());
    }

    if (!iosWindow_ || !iosWindow_->isReady())
    {
        // No CAMetalLayer to render into — refuse to init bgfx.  IosApp must
        // call setNativeWindow() before initIos().
        return false;
    }

    // -- Renderer (bgfx via CAMetalLayer) ---------------------------------
    {
        bgfx::PlatformData pd;
        pd.ndt = nullptr;
        pd.nwh = iosWindow_->nativeLayer();
        pd.context = nullptr;
        pd.backBuffer = nullptr;
        pd.backBufferDS = nullptr;
        bgfx::setPlatformData(pd);

        rendering::RendererDesc rd;
        rd.nativeWindowHandle = iosWindow_->nativeLayer();
        rd.nativeDisplayHandle = nullptr;
        rd.width = iosWindow_->width();
        rd.height = iosWindow_->height();
        rd.headless = false;
        if (!renderer_.init(rd))
        {
            return false;
        }
    }

    fbW_ = static_cast<uint16_t>(iosWindow_->width());
    fbH_ = static_cast<uint16_t>(iosWindow_->height());
    contentScaleX_ = iosWindow_->contentScale();
    contentScaleY_ = iosWindow_->contentScale();

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

    // -- Input ------------------------------------------------------------
    // IosInputBackend is the IInputBackend that InputSystem polls.  The
    // touch helper (IosTouchInput) is a separate, parallel path that mutates
    // InputState directly via UITouch overlays — bind it here so events
    // start landing in our InputState even before InputSystem polls.
    if (iosWindow_->nativeView())
    {
        inputBackend_ = std::make_unique<input::IosInputBackend>(iosWindow_->nativeView());
        inputSys_ = std::make_unique<input::InputSystem>(*inputBackend_);
    }
    if (iosTouch_)
    {
        iosTouch_->bindState(&inputState_);
    }

    // -- Frame arena ------------------------------------------------------
    frameArena_ = std::make_unique<memory::FrameArena>(desc.frameArenaSize);

    // -- Audio (SoLoud via miniaudio -> CoreAudio) ------------------------
    // SoLoud's miniaudio backend automatically picks the CoreAudio path on
    // Apple platforms; no iOS-specific configuration is required here.  On
    // the simulator audio routing may be unavailable, in which case
    // SoLoudAudioEngine::init() returns false and we fall back to the
    // NullAudioEngine so games can still call audio.play() without crashing
    // (silence is acceptable on the simulator).
    {
        auto soloud = std::make_unique<audio::SoLoudAudioEngine>();
        if (soloud->init())
        {
            audio_ = std::move(soloud);
        }
        else
        {
            // SoLoud failed to open an output device -- surface as silence.
            soloud.reset();
            auto null_audio = std::make_unique<audio::NullAudioEngine>();
            null_audio->init();
            audio_ = std::move(null_audio);
        }
    }

    // -- Timing -----------------------------------------------------------
    using Clock = std::chrono::steady_clock;
    auto now = Clock::now();
    prevTime_ = std::chrono::duration<double>(now.time_since_epoch()).count();

    // Flush initial resource uploads.
    renderer_.endFrame();

    initialized_ = true;
    return true;
}

void Engine::shutdown()
{
    if (!initialized_)
        return;

    shadow_.shutdown();

    destroyProgram(pbrProg_);
    destroyProgram(shadowProg_);
    destroyProgram(skinnedPbrProg_);
    destroyProgram(skinnedShadowProg_);

    resources_.destroyAll();

    renderer_.endFrame();
    renderer_.shutdown();

    // Audio: shut down before tearing down platform pointers.  Both backends
    // (SoLoud + Null) are safe to shutdown() even if init failed.
    if (audio_)
    {
        audio_->shutdown();
        audio_.reset();
    }

    inputSys_.reset();
    inputBackend_.reset();
    frameArena_.reset();

    if (iosTouch_)
    {
        iosTouch_->bindState(nullptr);
    }
    iosWindow_ = nullptr;
    iosTouch_ = nullptr;
    iosGyro_ = nullptr;
    iosFileSystem_ = nullptr;

    initialized_ = false;
}

bool Engine::beginFrame(float& outDt)
{
    // -- Surface readiness ------------------------------------------------
    // The IosApp delegate pauses the CADisplayLink while in the background;
    // by the time we get here the layer is generally valid.  Defensive check
    // just in case onFrame fires during a layout transition.
    if (!iosWindow_ || !iosWindow_->isReady())
    {
        outDt = 0.f;
        return true;
    }

    // -- Resize -----------------------------------------------------------
    uint32_t w = iosWindow_->width();
    uint32_t h = iosWindow_->height();
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
    if (inputSys_)
    {
        inputSys_->update(inputState_);
    }

    // -- Gyroscope --------------------------------------------------------
    if (iosGyro_)
    {
        iosGyro_->update(inputState_);
    }

    // -- Renderer begin (direct-to-backbuffer) ----------------------------
    renderer_.beginFrameDirect();

    // -- View 0 clear / rect / touch --------------------------------------
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor_, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, fbW_, fbH_);
    bgfx::touch(0);

    return true;
}

void Engine::endFrame()
{
    // Mirror the Android per-frame contract: clear edge flags / promote
    // touches now that the game has consumed them.  IosTouchInput owns the
    // promotion logic; call it before the bgfx submit so InputState is
    // consistent for any "did we just submit a frame?" checks the game does.
    if (iosTouch_)
    {
        iosTouch_->endFrame(inputState_);
    }

    if (frameArena_)
        frameArena_->reset();

    renderer_.endFrame();
}

bool Engine::imguiWantsMouse() const
{
    // ImGui is not available on iOS.
    return false;
}

#endif  // platform branch

}  // namespace engine::core
