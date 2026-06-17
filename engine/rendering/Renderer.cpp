#include "engine/rendering/Renderer.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#ifdef __ANDROID__
#include <android/log.h>
#include <android/native_window.h>
#endif

#include <chrono>
#include <cstdarg>
#include <cstdio>

#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#ifdef __ANDROID__
#include "engine/rendering/AndroidVulkanFormatProbe.h"
#endif

namespace engine::rendering
{

#ifdef __ANDROID__
namespace
{

// bgfx::CallbackI implementation that forwards bgfx's internal BX_TRACE +
// fatal output to Android logcat.  Without this, bgfx prints to its own
// debug-text overlay (off in production) and to a callback we hadn't
// wired — so its decisive "Running in single-threaded mode" /
// "Running in multi-threaded mode" line at init was invisible.
//
// See bgfx.cpp:2169 BX_TRACE("Running in %s-threaded mode", ...) — that's
// the conclusive evidence for whether bgfx's render thread was spawned.
// All other capability flags + caps bits report build-time facts, not
// runtime engagement.
//
// Default-implements the non-trace virtuals so existing behaviour (no
// shader cache, no screenshot, no capture) is preserved.
class BgfxLogcatCallback : public bgfx::CallbackI
{
public:
    ~BgfxLogcatCallback() override = default;

    void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code,
               const char* str) override
    {
        __android_log_print(ANDROID_LOG_FATAL, "SamaEngineBgfx", "FATAL %d at %s:%u: %s",
                            static_cast<int>(code), filePath != nullptr ? filePath : "?", line,
                            str != nullptr ? str : "?");
    }

    void traceVargs(const char* /*filePath*/, uint16_t /*line*/, const char* format,
                    va_list argList) override
    {
        // bgfx traces are noisy at startup (~50-100 lines) so log at INFO
        // not VERBOSE — that matches the engine's own info level and stays
        // visible without explicit filtering.  The "Running in
        // single-/multi-threaded mode" line is the one we're hunting; it
        // fires once at init.
        char buf[512];
        vsnprintf(buf, sizeof(buf), format, argList);
        __android_log_print(ANDROID_LOG_INFO, "SamaEngineBgfx", "%s", buf);
    }

    void profilerBegin(const char* /*name*/, uint32_t /*abgr*/, const char* /*filePath*/,
                       uint16_t /*line*/) override
    {
    }
    void profilerBeginLiteral(const char* /*name*/, uint32_t /*abgr*/, const char* /*filePath*/,
                              uint16_t /*line*/) override
    {
    }
    void profilerEnd() override {}

    uint32_t cacheReadSize(uint64_t /*id*/) override
    {
        return 0;
    }
    bool cacheRead(uint64_t /*id*/, void* /*data*/, uint32_t /*size*/) override
    {
        return false;
    }
    void cacheWrite(uint64_t /*id*/, const void* /*data*/, uint32_t /*size*/) override {}

    void screenShot(const char* /*filePath*/, uint32_t /*width*/, uint32_t /*height*/,
                    uint32_t /*pitch*/, bgfx::TextureFormat::Enum /*format*/,
                    const void* /*data*/, uint32_t /*size*/, bool /*yflip*/) override
    {
    }
    void captureBegin(uint32_t /*width*/, uint32_t /*height*/, uint32_t /*pitch*/,
                      bgfx::TextureFormat::Enum /*format*/, bool /*yflip*/) override
    {
    }
    void captureEnd() override {}
    void captureFrame(const void* /*data*/, uint32_t /*size*/) override {}
};

// Single static instance — bgfx::init copies the pointer, not the object,
// so the callback must outlive bgfx itself.  Living for the process is
// the simplest correct lifetime.
BgfxLogcatCallback g_bgfxLogcatCallback;

}  // namespace
#endif  // __ANDROID__

bool Renderer::init(const RendererDesc& desc)
{
    if (initialized_)
    {
        return false;
    }

    headless_ = desc.headless;

    bgfx::Init init;

    if (desc.headless)
    {
        init.type = bgfx::RendererType::Noop;
    }
    else
    {
#if defined(__APPLE__)
        init.type = bgfx::RendererType::Metal;
#else
        init.type = bgfx::RendererType::Vulkan;
#endif
        init.platformData.nwh = desc.nativeWindowHandle;
        init.platformData.ndt = desc.nativeDisplayHandle;
    }

#ifdef __ANDROID__
    // Route bgfx's internal BX_TRACE output into Android logcat under
    // tag SamaEngineBgfx.  The decisive line we need to see at init is
    // bgfx.cpp:2169 "Running in single-threaded mode" / "Running in
    // multi-threaded mode" — without this callback wired it goes
    // nowhere on Android.  Static lifetime is fine; bgfx stores the
    // pointer, not the object.  See the class definition at the top
    // of this file for the rationale.
    init.callback = &g_bgfxLogcatCallback;
#endif

    // Threading mode selection.
    //
    // bgfx::renderFrame() called BEFORE bgfx::init() forces single-threaded
    // mode — the render thread is never spawned and the calling thread does
    // command-buffer encoding, submit, GPU wait, and present serially.
    // Without that pre-init call, bgfx spawns a separate render thread and
    // bgfx::frame() becomes an asynchronous hand-off via a lock-free ring,
    // returning as soon as the queue accepts the frame.  See
    // docs/NOTES.md "bgfx threading mode — multi-threaded default" for the
    // measurement story (Pixel 9 saves ~20 ms/frame on the game thread
    // going multi-threaded).
    //
    // The headless / Noop renderer ignores threading mode; the early-out
    // matches the previous behaviour for the Noop unit-test path.
    if (!desc.headless && desc.singleThreaded)
    {
        bgfx::renderFrame();
    }

    init.resolution.width = desc.width;
    init.resolution.height = desc.height;
    init.resolution.reset = BGFX_RESET_VSYNC;

#ifdef __ANDROID__
    // Android Vulkan surfaces typically support RGBA8, not BGRA8 (the bgfx default).
    // Mali GPUs report VK_FORMAT_R8G8B8A8_UNORM; using BGRA8 causes swapchain creation
    // to fail and bgfx silently falls back to OpenGL ES.
    //
    // We probe the Vulkan device for its supported swapchain formats BEFORE
    // bgfx::init runs, so a future device or HDR rendering target that needs
    // a different format (e.g. RGB10A2) gets the right swapchain.  RGBA8 is
    // mandatory per the Android CDD so the probe always returns something
    // usable; on any failure path it falls back to RGBA8 anyway.
    // See engine/rendering/AndroidVulkanFormatProbe.h.
    init.resolution.formatColor =
        probeAndroidSwapchainFormat(static_cast<ANativeWindow*>(desc.nativeWindowHandle));
    __android_log_print(ANDROID_LOG_INFO, "SamaEngine", "Vulkan swapchain format: %s",
                        bgfxSwapchainFormatName(init.resolution.formatColor));
#ifdef SAMA_ANDROID_DEBUG_LAYERS
    // Enable Vulkan validation layer (requires libVkLayer_khronos_validation.so
    // packaged in the APK at lib/<abi>/).  bgfx auto-loads VK_LAYER_KHRONOS_validation
    // when init.debug is true.  Gated behind SAMA_ANDROID_DEBUG_LAYERS so prod
    // builds don't ship the validation layer.
    init.debug = true;
#endif
#endif

#ifdef __ANDROID__
    __android_log_print(4, "SamaEngine", "bgfx::init type=%d nwh=%p w=%u h=%u", (int)init.type,
                        init.platformData.nwh, init.resolution.width, init.resolution.height);
#endif

    if (!bgfx::init(init))
    {
#ifdef __ANDROID__
        __android_log_print(6, "SamaEngine", "bgfx::init() FAILED");
#endif
        return false;
    }

#ifdef __ANDROID__
    {
        auto actualType = bgfx::getRendererType();
        __android_log_print(4, "SamaEngine", "bgfx renderer: %s",
                            bgfx::getRendererName(actualType));
        if (actualType != bgfx::RendererType::Vulkan)
        {
            __android_log_print(6, "SamaEngine",
                                "FATAL: Expected Vulkan but got %s. "
                                "Sama requires Vulkan on Android.",
                                bgfx::getRendererName(actualType));
            return false;
        }

        // Log the actual threading mode bgfx is running in so the shipping
        // build can verify multi-threaded is engaged at runtime.  Without
        // this, a silent fallback to single-threaded (rare but possible if
        // a future bgfx Android path can't spawn its render thread) would
        // be invisible until someone runs perf_smoke comparison.
        // BGFX_CAPS_RENDERER_MULTITHREADED is set when bgfx is BUILT with
        // BGFX_CONFIG_MULTITHREADED=1 (a build-time fact, not runtime
        // engagement) — keep the line because it's still the right thing
        // to check first when investigating a threading-related slowdown.
        const auto* caps = bgfx::getCaps();
        const bool multithreadedCompiled =
            (caps->supported & BGFX_CAPS_RENDERER_MULTITHREADED) != 0;
        __android_log_print(4, "SamaEngine",
                            "bgfx threading: built-multithreaded=%d, "
                            "engine-requested-mode=%s",
                            multithreadedCompiled ? 1 : 0,
                            desc.singleThreaded ? "single" : "multi");
    }
#endif

#if !defined(NDEBUG) && !defined(__ANDROID__)
    bgfx::setDebug(BGFX_DEBUG_TEXT);
#endif

    // Clear view 0 with a dark purple color and depth
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x443355FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(desc.width),
                      static_cast<uint16_t>(desc.height));

    uniforms_.init();

    // PostProcessSystem allocates GPU framebuffers that the Noop renderer
    // cannot create.  Skip in headless mode to avoid bgfx debug assertions.
    if (!desc.headless)
    {
        postProcess_.init(static_cast<uint16_t>(desc.width), static_cast<uint16_t>(desc.height));
    }

    // Apply built-in view labels for GPU debugger / perf overlay readability.
    // Safe to call in headless mode — bgfx accepts setViewName on the Noop
    // renderer, it simply has no observable effect.
    setupDefaultViewNames();

    initialized_ = true;
    return true;
}

void Renderer::setupDefaultViewNames()
{
    for (ViewId i = 0; i < kMaxShadowViews; ++i)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "Shadow %u", static_cast<unsigned>(i));
        bgfx::setViewName(static_cast<bgfx::ViewId>(kViewShadowBase + i), buf);
    }
    bgfx::setViewName(kViewDepth, "Depth Prepass");
    bgfx::setViewName(kViewOpaque, "Opaque");
    bgfx::setViewName(kViewTransparent, "Transparent");
    bgfx::setViewName(kViewUi, "UI 3D");
    bgfx::setViewName(kViewImGui, "ImGui");
    // kViewGameUi (48) and kViewDebugHud (49) are owned by the game /
    // DebugHud; UiRenderer::render and DebugHud::end label those.
    // Post-process sub-pass views (16..47) are labelled by PostProcessSystem
    // when each sub-pass is allocated.
}

void Renderer::beginFrame()
{
    if (!initialized_)
    {
        return;
    }

    // Redirect opaque and transparent passes into the HDR scene framebuffer
    // so that the post-process chain can read it.  Skip in headless mode
    // where postProcess_ was not initialised and sceneFb() is invalid.
    if (!headless_)
    {
        const auto sceneFb = postProcess_.resources().sceneFb();
        RenderPass(kViewOpaque).framebuffer({sceneFb.idx});
        RenderPass(kViewTransparent).framebuffer({sceneFb.idx});
    }

    // Ensure view 0 is submitted even when the frame has no shadow draw calls.
    RenderPass(kViewShadowBase).touch();
}

void Renderer::endFrame()
{
    if (!initialized_)
    {
        return;
    }

    // Auto-submit the post-process chain.  The PBR shader (fs_pbr.sc) writes
    // linear HDR; the tonemap pass converts to sRGB-gamma LDR for the
    // backbuffer.  Skip in headless mode (Noop renderer has no shaders).
    //
    // Each phase is timed separately so games and the perf overlay can
    // distinguish "post-process CPU cost" from "bgfx::frame() wait" — on
    // single-threaded bgfx (the default on Sama today) the latter includes
    // command-buffer recording AND vsync / GPU fence wait, all charged to
    // the game thread.  Cost of the chrono calls is ~50 ns each on M-series;
    // not measurable on a real-world frame budget.
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    if (!headless_)
    {
        postProcess_.submit(renderSettings_.postProcess, uniforms_);
    }
    const auto t1 = Clock::now();

    bgfx::frame();
    const auto t2 = Clock::now();

    using FloatMs = std::chrono::duration<float, std::milli>;
    frameStats_.postProcessSubmitMs = std::chrono::duration_cast<FloatMs>(t1 - t0).count();
    frameStats_.bgfxFrameMs = std::chrono::duration_cast<FloatMs>(t2 - t1).count();

#ifdef __ANDROID__
    // Diagnostic: dump bgfx's own thread-wait counters every 120 frames.
    // The pair (waitSubmit, waitRender) disambiguates the "high
    // bgfx::frame()" investigation (see docs/NOTES.md "Correction
    // (2026-06-16)" + the follow-up entry).  Heavy bgfx::frameMs +
    // large waitSubmit = game thread is back-pressured by render thread
    // (which is itself either GPU-bound or compositor-bound).  Heavy
    // bgfx::frameMs + small waitSubmit = the cost is in bgfx's internal
    // bookkeeping (cmd-list build), not the hand-off.
    //
    // Also dumps gpuTime / cpuTime (also in microseconds) so the
    // integrating team can see whether GPU work itself is the limiter.
    //
    // Gated behind frameCounter to keep logcat noise bounded — 120
    // frames ≈ 2 s at 60 fps, plenty for diagnosis without spam.
    static uint32_t s_frameLogCounter = 0;
    if ((++s_frameLogCounter % 120) == 0)
    {
        const bgfx::Stats* stats = bgfx::getStats();
        if (stats != nullptr)
        {
            const double cpuToMs = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
            const double gpuToMs = 1000.0 / static_cast<double>(stats->gpuTimerFreq);
            __android_log_print(
                4, "SamaEngineBgfxStats",
                "frame=%u bgfx::frameMs=%.2f | waitSubmit=%.2f ms | waitRender=%.2f ms | "
                "cpu=%.2f ms gpu=%.2f ms | numDraws=%u",
                s_frameLogCounter, frameStats_.bgfxFrameMs,
                static_cast<double>(stats->waitSubmit) * cpuToMs,
                static_cast<double>(stats->waitRender) * cpuToMs,
                static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) * cpuToMs,
                static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * gpuToMs,
                static_cast<uint32_t>(stats->numDraw));
        }
    }
#endif
}

RenderSettings Renderer::makeDefaultSettings()
{
    // Cheapest valid post-process: tonemap + sRGB gamma only.  Bloom, SSAO,
    // and FXAA each cost extra view IDs and shader work, so they are off by
    // default — game code opts in via setRenderSettings() during init.
    RenderSettings settings;
    settings.postProcess.bloom.enabled = false;
    settings.postProcess.ssao.enabled = false;
    settings.postProcess.fxaaEnabled = false;
    settings.postProcess.toneMapper = ToneMapper::ACES;
    // Defense-in-depth: the struct default in RenderSettings.h is already
    // `false` (see docs/PERF_AUDIT_2026-05-25.md item #R-audit), but call it
    // out here too so a future reader of this default-settings function
    // doesn't need to chase the header to learn the chosen TBDR policy.
    settings.depthPrepassEnabled = false;
    return settings;
}

void Renderer::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    if (!headless_)
    {
        postProcess_.shutdown();
    }
    uniforms_.destroy();

    bgfx::shutdown();
    initialized_ = false;
    headless_ = false;
}

void Renderer::resize(uint32_t w, uint32_t h)
{
    if (!initialized_)
    {
        return;
    }

    bgfx::reset(w, h, BGFX_RESET_VSYNC);
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(w), static_cast<uint16_t>(h));

    if (!headless_)
    {
        postProcess_.resize(static_cast<uint16_t>(w), static_cast<uint16_t>(h));
    }
}

bool Renderer::isHeadless() const
{
    return headless_;
}

}  // namespace engine::rendering
