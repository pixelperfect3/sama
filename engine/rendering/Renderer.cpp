#include "engine/rendering/Renderer.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

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

    initialized_ = true;
    return true;
}

void Renderer::beginFrame()
{
    if (!initialized_)
    {
        return;
    }

    // Touch view 0 to ensure it is submitted even when the frame has no draw calls
    bgfx::touch(0);
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
}

bool Renderer::isHeadless() const
{
    return headless_;
}

}  // namespace engine::rendering
