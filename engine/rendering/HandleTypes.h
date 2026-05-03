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

// Opaque shader-program handle.  Boundary conversion to/from
// bgfx::ProgramHandle happens inside engine_rendering / engine_ui only.
// See RenderPass.cpp for the layout sanity static_asserts.
struct ProgramHandle
{
    uint16_t idx;
};

// Opaque texture handle (2D / cube — bgfx unifies them under a single type).
// Boundary conversion to/from bgfx::TextureHandle happens inside
// engine_rendering / engine_ui only.
struct TextureHandle
{
    uint16_t idx;
};

// Opaque uniform handle.  Apps don't usually touch uniforms directly, but
// engine internals (font / material / post-process code) do, so expose the
// alias so we don't have to do another pass once those headers shake out.
struct UniformHandle
{
    uint16_t idx;
};

inline constexpr FrameBufferHandle kInvalidFramebuffer{UINT16_MAX};
inline constexpr ProgramHandle kInvalidProgram{UINT16_MAX};
inline constexpr TextureHandle kInvalidTexture{UINT16_MAX};
inline constexpr UniformHandle kInvalidUniform{UINT16_MAX};

[[nodiscard]] constexpr bool isValid(FrameBufferHandle h)
{
    return h.idx != UINT16_MAX;
}

[[nodiscard]] constexpr bool isValid(ProgramHandle h)
{
    return h.idx != UINT16_MAX;
}

[[nodiscard]] constexpr bool isValid(TextureHandle h)
{
    return h.idx != UINT16_MAX;
}

[[nodiscard]] constexpr bool isValid(UniformHandle h)
{
    return h.idx != UINT16_MAX;
}

}  // namespace engine::rendering
