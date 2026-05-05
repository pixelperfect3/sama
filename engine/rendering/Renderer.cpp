#include "engine/rendering/Renderer.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#ifdef __ANDROID__
#include <android/log.h>
#include <android/native_window.h>
#endif

#include <cstdio>

#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#ifdef __ANDROID__
#include "engine/rendering/AndroidVulkanFormatProbe.h"
#endif

namespace engine::rendering
{

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

    // Single-threaded mode: bgfx::renderFrame() must be called exactly once
    // before bgfx::init() to prevent bgfx from spawning its own render thread.
    if (!desc.headless)
        bgfx::renderFrame();

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
    if (!headless_)
    {
        postProcess_.submit(renderSettings_.postProcess, uniforms_);
    }

    bgfx::frame();
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
