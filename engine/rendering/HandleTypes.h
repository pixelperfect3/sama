#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// HandleTypes — opaque, bgfx-free aliases for the rendering boundary.
//
// Games and other engine consumers should use these types instead of
// bgfx::ViewId / bgfx::FrameBufferHandle so that <bgfx/bgfx.h> never has to
// be transitively included from public engine headers.
//
// Layout is required to match bgfx exactly — RenderPass.cpp asserts the
// sizes / alignments at compile time so the boundary conversion stays a
// no-op reinterpret of the underlying uint16_t.
// ---------------------------------------------------------------------------

namespace engine::rendering
{

using ViewId = uint16_t;  // same underlying type as bgfx::ViewId

// Opaque framebuffer handle.  Conversion to/from bgfx::FrameBufferHandle
// happens inside engine_rendering only (see RenderPass.cpp).
struct FrameBufferHandle
{
    uint16_t idx;
};

inline constexpr FrameBufferHandle kInvalidFramebuffer{UINT16_MAX};

[[nodiscard]] constexpr bool isValid(FrameBufferHandle h)
{
    return h.idx != UINT16_MAX;
}

}  // namespace engine::rendering
