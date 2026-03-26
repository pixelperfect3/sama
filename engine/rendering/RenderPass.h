#pragma once

#include <bgfx/bgfx.h>

#include "engine/math/Types.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// RenderPass — fluent builder for configuring a bgfx view.
//
// Each setter calls the corresponding bgfx::setView* immediately, so there
// is no deferred "commit" step.  The fluent interface keeps all configuration
// for a single pass in one chain, making it impossible to accidentally set
// state on the wrong view ID.
//
// Example — shadow pass:
//   RenderPass(kViewShadowBase)
//       .framebuffer(shadowFb)
//       .rect(0, 0, 2048, 2048)
//       .clearDepth()
//       .transform(lightView, lightProj);
//
// Example — opaque pass (renders to backbuffer):
//   RenderPass(kViewOpaque)
//       .framebuffer(BGFX_INVALID_HANDLE)
//       .rect(0, 0, w, h)
//       .clearColorAndDepth(0x87CEEBFF)
//       .transform(camView, camProj);
// ---------------------------------------------------------------------------

class RenderPass
{
public:
    explicit RenderPass(bgfx::ViewId viewId) : viewId_(viewId) {}

    // Bind a framebuffer.  Pass BGFX_INVALID_HANDLE to render to the backbuffer.
    RenderPass& framebuffer(bgfx::FrameBufferHandle fb);

    // Set the viewport rectangle (pixels, top-left origin).
    RenderPass& rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    // Clear colour and depth to the given values.
    RenderPass& clearColorAndDepth(uint32_t rgba, float depth = 1.f);

    // Clear depth only — typical for shadow passes.
    RenderPass& clearDepth(float depth = 1.f);

    // Upload view and projection matrices.
    RenderPass& transform(const math::Mat4& view, const math::Mat4& proj);

    // Ensure bgfx processes this view even when no draw calls are submitted.
    RenderPass& touch();

    [[nodiscard]] bgfx::ViewId viewId() const
    {
        return viewId_;
    }

private:
    bgfx::ViewId viewId_;
};

}  // namespace engine::rendering
