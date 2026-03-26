#include "engine/rendering/Renderer.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"

namespace engine::rendering
{

bool Renderer::init(const RendererDesc& desc)
{
    if (initialized_)
    {
        return false;
    }

    headless_ = desc.headless;

    // Single-threaded mode: bgfx::renderFrame() must be called exactly once
    // before bgfx::init() to prevent bgfx from spawning its own render thread.
    if (!desc.headless)
        bgfx::renderFrame();

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

    init.resolution.width = desc.width;
    init.resolution.height = desc.height;
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init))
    {
        return false;
    }

#if !defined(NDEBUG)
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

    initialized_ = true;
    return true;
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
        RenderPass(kViewOpaque).framebuffer(postProcess_.resources().sceneFb());
        RenderPass(kViewTransparent).framebuffer(postProcess_.resources().sceneFb());
    }

    // Ensure view 0 is submitted even when the frame has no shadow draw calls.
    RenderPass(kViewShadowBase).touch();
}

void Renderer::beginFrameDirect()
{
    if (!initialized_)
        return;

    // kViewOpaque writes directly to the backbuffer — bypass post-processing.
    RenderPass(kViewOpaque).framebuffer(BGFX_INVALID_HANDLE);
    RenderPass(kViewShadowBase).touch();
}

void Renderer::endFrame()
{
    if (!initialized_)
    {
        return;
    }

    bgfx::frame();
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
