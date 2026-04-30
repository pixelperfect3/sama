#pragma once

#include "engine/math/Types.h"
#include "engine/rendering/HandleTypes.h"

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
// This header is intentionally bgfx-free — game code and other engine
// modules may include it without dragging in <bgfx/bgfx.h>.  All bgfx
// calls live in RenderPass.cpp, where engine::rendering::FrameBufferHandle
// is converted to bgfx::FrameBufferHandle via the bit-identical layout
// guarded by static_asserts in that translation unit.
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
//       .framebuffer()                  // default = kInvalidFramebuffer
//       .rect(0, 0, w, h)
//       .clearColorAndDepth(0x87CEEBFF)
//       .transform(camView, camProj);
// ---------------------------------------------------------------------------

class RenderPass
{
public:
    explicit RenderPass(ViewId viewId) : viewId_(viewId) {}

    // Bind a framebuffer.  Default kInvalidFramebuffer renders to the backbuffer.
    RenderPass& framebuffer(FrameBufferHandle fb = kInvalidFramebuffer);

    // Set the viewport rectangle (pixels, top-left origin).
    RenderPass& rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    // Clear colour and depth to the given values.
    RenderPass& clearColorAndDepth(uint32_t rgba, float depth = 1.f);

    // Clear depth only — typical for shadow passes.
    RenderPass& clearDepth(float depth = 1.f);

    // Clear colour only — typical for UI overlays that want to wipe the
    // backbuffer without disturbing depth (or when the view has no depth
    // attachment).
    RenderPass& clearColor(uint32_t rgba);

    // Reset the persistent clear state to BGFX_CLEAR_NONE.
    //
    // Rationale: bgfx's setViewClear flags persist across frames.  If a
    // previous frame configured the view with CLEAR_COLOR_AND_DEPTH and a
    // later frame "unconfigures" it (e.g. when transitioning game states),
    // the stale clear flags can wipe the framebuffer at the start of the
    // next frame and silently break rendering.  Calling clearNone() on a
    // view explicitly tells bgfx "do not clear anything for this view".
    // This was originally needed to fix an Android return-to-title bug
    // where stale CLEAR_COLOR_AND_DEPTH wiped the opaque pass.
    RenderPass& clearNone();

    // Upload view and projection matrices.
    RenderPass& transform(const math::Mat4& view, const math::Mat4& proj);

    // Ensure bgfx processes this view even when no draw calls are submitted.
    RenderPass& touch();

    // Set the GPU-debugger / perf-overlay label for this view.
    // Wraps bgfx::setViewName.  The label string is copied internally by bgfx.
    RenderPass& name(const char* label);

    [[nodiscard]] ViewId viewId() const
    {
        return viewId_;
    }

private:
    ViewId viewId_;
};

}  // namespace engine::rendering
