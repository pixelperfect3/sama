#include "engine/platform/android/AndroidApp.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SamaEngine", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SamaEngine", __VA_ARGS__)

namespace engine::platform
{

namespace
{

struct AndroidAppState
{
    bool windowReady = false;
    bool focused = false;
    bool bgfxInitialized = false;
    uint32_t width = 0;
    uint32_t height = 0;
};

static void handleAppCmd(struct android_app* app, int32_t cmd)
{
    auto* state = static_cast<AndroidAppState*>(app->userData);

    switch (cmd)
    {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window != nullptr)
            {
                state->width = static_cast<uint32_t>(ANativeWindow_getWidth(app->window));
                state->height = static_cast<uint32_t>(ANativeWindow_getHeight(app->window));
                state->windowReady = true;
            }
            break;

        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            state->windowReady = false;
            if (state->bgfxInitialized)
            {
                bgfx::shutdown();
                state->bgfxInitialized = false;
            }
            break;

        case APP_CMD_GAINED_FOCUS:
            LOGI("APP_CMD_GAINED_FOCUS");
            state->focused = true;
            break;

        case APP_CMD_LOST_FOCUS:
            LOGI("APP_CMD_LOST_FOCUS");
            state->focused = false;
            break;
    }
}

static bool initBgfx(struct android_app* app, AndroidAppState& state)
{
    bgfx::PlatformData pd;
    pd.ndt = nullptr;
    pd.nwh = app->window;
    pd.context = nullptr;
    pd.backBuffer = nullptr;
    pd.backBufferDS = nullptr;
    bgfx::setPlatformData(pd);

    bgfx::Init init;
    init.type = bgfx::RendererType::Vulkan;
    init.resolution.width = state.width;
    init.resolution.height = state.height;
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init))
    {
        LOGE("bgfx::init() failed");
        return false;
    }

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, state.width, state.height);

    LOGI("bgfx initialized: %ux%u, Vulkan", state.width, state.height);
    state.bgfxInitialized = true;
    return true;
}

}  // namespace

void runAndroidApp(struct android_app* app)
{
    AndroidAppState state;
    app->userData = &state;
    app->onAppCmd = handleAppCmd;

    LOGI("Sama Engine — Android bootstrap starting");

    while (!app->destroyRequested)
    {
        // Process pending events.  Block if we have no window (nothing to
        // render), poll without blocking when rendering is active.
        int events;
        struct android_poll_source* source;
        int timeout = (state.windowReady && state.focused) ? 0 : -1;

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
        if (state.windowReady && !state.bgfxInitialized)
        {
            if (!initBgfx(app, state))
            {
                LOGE("Failed to initialize bgfx — exiting");
                break;
            }
        }

        // Render a frame (clear-color only for Phase A).
        if (state.bgfxInitialized && state.windowReady && state.focused)
        {
            bgfx::touch(0);
            bgfx::frame();
        }
    }

    // Clean up.
    if (state.bgfxInitialized)
    {
        bgfx::shutdown();
        state.bgfxInitialized = false;
    }

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
