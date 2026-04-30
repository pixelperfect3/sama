#include "engine/rendering/RenderPass.h"

#include <bgfx/bgfx.h>

#include <glm/gtc/type_ptr.hpp>

#include "engine/rendering/HandleTypes.h"

namespace engine::rendering
{

// Layout sanity checks — engine::rendering::FrameBufferHandle MUST be
// bit-identical to bgfx::FrameBufferHandle so the boundary conversion
// (FrameBufferHandle{h.idx} <-> bgfx::FrameBufferHandle{h.idx}) is a no-op.
static_assert(sizeof(engine::rendering::FrameBufferHandle) == sizeof(bgfx::FrameBufferHandle));
static_assert(alignof(engine::rendering::FrameBufferHandle) == alignof(bgfx::FrameBufferHandle));

RenderPass& RenderPass::framebuffer(FrameBufferHandle fb)
{
    bgfx::setViewFrameBuffer(viewId_, bgfx::FrameBufferHandle{fb.idx});
    return *this;
}

RenderPass& RenderPass::rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    bgfx::setViewRect(viewId_, x, y, w, h);
    return *this;
}

RenderPass& RenderPass::clearColorAndDepth(uint32_t rgba, float depth)
{
    bgfx::setViewClear(viewId_, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, depth, 0);
    return *this;
}

RenderPass& RenderPass::clearDepth(float depth)
{
    bgfx::setViewClear(viewId_, BGFX_CLEAR_DEPTH, 0, depth, 0);
    return *this;
}

RenderPass& RenderPass::clearColor(uint32_t rgba)
{
    bgfx::setViewClear(viewId_, BGFX_CLEAR_COLOR, rgba, 1.f, 0);
    return *this;
}

RenderPass& RenderPass::clearNone()
{
    bgfx::setViewClear(viewId_, BGFX_CLEAR_NONE);
    return *this;
}

RenderPass& RenderPass::transform(const math::Mat4& view, const math::Mat4& proj)
{
    bgfx::setViewTransform(viewId_, glm::value_ptr(view), glm::value_ptr(proj));
    return *this;
}

RenderPass& RenderPass::touch()
{
    bgfx::touch(viewId_);
    return *this;
}

RenderPass& RenderPass::name(const char* label)
{
    bgfx::setViewName(viewId_, label);
    return *this;
}

}  // namespace engine::rendering
