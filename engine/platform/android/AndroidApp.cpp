#include "engine/platform/android/AndroidApp.h"

#include <android/configuration.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include "engine/platform/android/AndroidFileSystem.h"
#include "engine/platform/android/AndroidInput.h"
#include "engine/platform/android/AndroidWindow.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SamaEngine", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SamaEngine", __VA_ARGS__)

// ---------------------------------------------------------------------------
// SoLoud Android audio backend note:
//
// SoLoud supports Android natively via OpenSL ES or AAudio.  When
// integrating audio, initialize SoLoud with the appropriate backend:
//
//   // API 24+ (OpenSL ES — widest compatibility):
//   soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF,
//               SoLoud::Soloud::OPENSLES);
//
//   // API 26+ (AAudio — lower latency):
//   soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF,
//               SoLoud::Soloud::AAUDIO);
//
// TODO(audio): Detect API level at runtime and choose the best backend.
// ---------------------------------------------------------------------------

namespace engine::platform
{

namespace
{

struct AndroidAppState
{
    AndroidWindow window;
    AndroidInput input;
    engine::input::InputState inputState;
    std::unique_ptr<AndroidFileSystem> fileSystem;

    bool focused = false;
    bool bgfxInitialized = false;
};

static void handleAppCmd(struct android_app* app, int32_t cmd)
{
    auto* state = static_cast<AndroidAppState*>(app->userData);
    if (!state)
        return;

    switch (cmd)
    {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window != nullptr)
            {
                state->window.setNativeWindow(app->window);

                // Query display density from AConfiguration.
                AConfiguration* config = AConfiguration_new();
                AConfiguration_fromAssetManager(config, app->activity->assetManager);
                int32_t density = AConfiguration_getDensity(config);
                AConfiguration_delete(config);

                if (density > 0)
                {
                    state->window.setDensity(density);
                    LOGI("Display density: %d dpi (scale %.2f)", density,
                         state->window.contentScale());
                }

                if (!state->window.isReady())
                {
                    LOGE(
                        "Invalid window dimensions after "
                        "setNativeWindow");
                }
            }
            else
            {
                LOGE(
                    "APP_CMD_INIT_WINDOW received but "
                    "app->window is null");
            }
            break;

        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            if (state->bgfxInitialized)
            {
                bgfx::shutdown();
                state->bgfxInitialized = false;
            }
            state->window.clearNativeWindow();
            break;

        case APP_CMD_GAINED_FOCUS:
            LOGI("APP_CMD_GAINED_FOCUS");
            state->focused = true;
            break;

        case APP_CMD_LOST_FOCUS:
            LOGI("APP_CMD_LOST_FOCUS");
            state->focused = false;
            break;

        case APP_CMD_CONFIG_CHANGED:
            LOGI("APP_CMD_CONFIG_CHANGED");
            // Window size may have changed (e.g. orientation).
            state->window.updateSize();
            break;
    }
}

static int32_t handleInput(struct android_app* app, AInputEvent* event)
{
    auto* state = static_cast<AndroidAppState*>(app->userData);
    if (!state)
        return 0;
    return state->input.handleInputEvent(event, state->inputState);
}

static bool initBgfx(AndroidAppState& state)
{
    bgfx::PlatformData pd;
    pd.ndt = nullptr;
    pd.nwh = state.window.nativeWindow();
    pd.context = nullptr;
    pd.backBuffer = nullptr;
    pd.backBufferDS = nullptr;
    bgfx::setPlatformData(pd);

    bgfx::Init init;
    init.type = bgfx::RendererType::Vulkan;
    init.resolution.width = state.window.width();
    init.resolution.height = state.window.height();
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init))
    {
        LOGE("bgfx::init() failed");
        return false;
    }

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, state.window.width(), state.window.height());

    LOGI("bgfx initialized: %ux%u, Vulkan", state.window.width(), state.window.height());
    state.bgfxInitialized = true;
    return true;
}

}  // namespace

void runAndroidApp(struct android_app* app)
{
    auto* state = new AndroidAppState();
    app->userData = state;
    app->onAppCmd = handleAppCmd;
    app->onInputEvent = handleInput;

    // Create the Android file system backed by the APK's asset manager.
    state->fileSystem = std::make_unique<AndroidFileSystem>(app->activity->assetManager);

    LOGI("Sama Engine — Android bootstrap starting");

    while (!app->destroyRequested)
    {
        // Process pending events.  Block if we have no window (nothing to
        // render), poll without blocking when rendering is active.
        int events;
        struct android_poll_source* source;
        int timeout = (state->window.isReady() && state->focused) ? 0 : -1;

        while (ALooper_pollAll(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
        {
            if (source != nullptr)
            {
                source->process(app, source);
            }

            if (app->destroyRequested)
            {
                break;
            }
        }

        if (app->destroyRequested)
        {
            break;
        }

        // Initialize bgfx once the window is available.
        if (state->window.isReady() && !state->bgfxInitialized)
        {
            if (!initBgfx(*state))
            {
                LOGE("Failed to initialize bgfx — exiting");
                break;
            }
        }

        // Render a frame (clear-color only for Phase A/B).
        if (state->bgfxInitialized && state->window.isReady() && state->focused)
        {
            bgfx::touch(0);
            bgfx::frame();
        }

        // End-of-frame input housekeeping.
        state->input.endFrame(state->inputState);
    }

    // Clean up.
    if (state->bgfxInitialized)
    {
        bgfx::shutdown();
        state->bgfxInitialized = false;
    }

    state->fileSystem.reset();
    app->userData = nullptr;
    delete state;

    LOGI("Sama Engine — Android bootstrap exiting");
}

}  // namespace engine::platform

// ---------------------------------------------------------------------------
// NativeActivity entry point — called by android_native_app_glue.
// ---------------------------------------------------------------------------
void android_main(struct android_app* app)
{
    engine::platform::runAndroidApp(app);
}
