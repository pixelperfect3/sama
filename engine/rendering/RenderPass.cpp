#include "engine/rendering/RenderPass.h"

#include <glm/gtc/type_ptr.hpp>

namespace engine::rendering
{

RenderPass& RenderPass::framebuffer(bgfx::FrameBufferHandle fb)
{
    bgfx::setViewFrameBuffer(viewId_, fb);
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

}  // namespace engine::rendering
