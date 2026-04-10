#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

#include "engine/ui/GlyphMetrics.h"

namespace engine::ui
{

// ---------------------------------------------------------------------------
// FontRenderer
//
// Identifies which backend produced a font. Used by UiRenderer to decide
// shader program / vertex layout / per-batch resource binding.
//
// Selected globally at startup based on GPU tier (see GAME_LAYER_ARCHITECTURE
// §5 "Runtime selection by device tier"):
//   - Bitmap : low-end mobile, development, predictable cost
//   - Msdf   : mid-range mobile, single shader swap from bitmap
//   - Slug   : desktop / VR / high-end mobile, vector-perfect at any scale
// ---------------------------------------------------------------------------

enum class FontRenderer : uint8_t
{
    Bitmap,
    Msdf,
    Slug
};

// ---------------------------------------------------------------------------
// IFont
//
// Common interface for every text-rendering backend. UiText / UiButton hold
// `IFont*` and never know which concrete backend they're using. UiRenderer
// asks the font for its program + texture(s) and submits accordingly.
//
// Lifetime: fonts are owned by the asset/UI subsystem; widgets store raw
// non-owning pointers. A nullptr font means "use the global default font".
// ---------------------------------------------------------------------------

class IFont
{
public:
    virtual ~IFont() = default;

    // Backend identity (so UiRenderer can pick the right shader / vertex
    // layout if it differs from the textured-rect default).
    virtual FontRenderer renderer() const noexcept = 0;

    // Glyph lookup. Returns nullptr if the codepoint is missing — caller
    // typically substitutes the replacement glyph (codepoint 0xFFFD).
    virtual const GlyphMetrics* getGlyph(uint32_t codepoint) const = 0;

    // Optional kerning between two adjacent glyphs. Returns 0.f if no
    // kerning pair exists or the backend doesn't support kerning.
    virtual float getKerning(uint32_t left, uint32_t right) const = 0;

    // Vertical line advance at the font's nominal size, in pixels.
    virtual float lineHeight() const noexcept = 0;

    // Nominal pixel size the font was generated at. UiRenderer scales each
    // glyph quad by (requestedSize / nominalSize) when emitting vertices.
    virtual float nominalSize() const noexcept = 0;

    // Atlas texture for atlas-backed backends (Bitmap, MSDF). Slug returns
    // BGFX_INVALID_HANDLE — its glyph data lives in a buffer texture bound
    // via bindResources() instead.
    virtual bgfx::TextureHandle atlasTexture() const noexcept = 0;

    // Shader program to use when submitting glyph quads from this font.
    // Each backend owns its own program (sprite shader for Bitmap, MSDF
    // shader for Msdf, slug shader for Slug).
    virtual bgfx::ProgramHandle program() const noexcept = 0;

    // Bind any backend-specific resources (uniforms, additional textures,
    // curve buffers) before submission. Called by UiRenderer once per
    // text batch, after setVertexBuffer/setIndexBuffer and before submit().
    // Default no-op covers Bitmap and MSDF, which only need the atlas
    // texture (already set by UiRenderer via the s_texture sampler).
    virtual void bindResources() const {}
};

}  // namespace engine::ui
