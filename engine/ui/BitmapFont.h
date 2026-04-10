#pragma once

#include <ankerl/unordered_dense.h>
#include <bgfx/bgfx.h>

#include <cstdint>

#include "engine/ui/GlyphMetrics.h"
#include "engine/ui/IFont.h"

namespace engine::ui
{

// ---------------------------------------------------------------------------
// BitmapFont
//
// Atlas-based font backend. Loads glyph metrics from a BMFont ".fnt" text
// file and the corresponding single-page atlas PNG. Bitmap glyphs are just
// textured quads with alpha, so the backend reuses the existing sprite
// shader (vs_sprite/fs_sprite).
//
// Also supports a procedural debug-font path (createDebugFont) that builds
// a 128-glyph ASCII monospace atlas in memory from an embedded 8x8 bitmap
// font. This is the default fallback when no ".fnt" asset is available,
// and is what engine::ui::defaultFont() uses.
// ---------------------------------------------------------------------------

class BitmapFont final : public IFont
{
public:
    BitmapFont() = default;
    ~BitmapFont() override;

    BitmapFont(const BitmapFont&) = delete;
    BitmapFont& operator=(const BitmapFont&) = delete;

    // Load BMFont text-format ".fnt" + PNG atlas from disk. Returns true on
    // success.
    bool loadFromFile(const char* fntPath, const char* atlasPath);

    // Build a procedural 8x8 ASCII monospace font directly from an embedded
    // public-domain bitmap. No filesystem access required — this is the
    // default font used when no asset is provided. The glyph quads are
    // emitted at 16px nominal size (8x8 source upscaled 2x in UV-rect
    // coordinates so text looks readable at small sizes).
    bool createDebugFont();

    void shutdown();

    FontRenderer renderer() const noexcept override
    {
        return FontRenderer::Bitmap;
    }
    const GlyphMetrics* getGlyph(uint32_t codepoint) const override;
    float getKerning(uint32_t left, uint32_t right) const override;
    float lineHeight() const noexcept override
    {
        return lineHeight_;
    }
    float nominalSize() const noexcept override
    {
        return nominalSize_;
    }
    bgfx::TextureHandle atlasTexture() const noexcept override
    {
        return atlas_;
    }
    bgfx::ProgramHandle program() const noexcept override
    {
        return program_;
    }

    // Number of glyphs currently in the map (tests).
    std::size_t glyphCount() const noexcept
    {
        return glyphs_.size();
    }

private:
    ankerl::unordered_dense::map<uint32_t, GlyphMetrics> glyphs_;
    ankerl::unordered_dense::map<uint64_t, float> kerning_;
    float lineHeight_ = 0.f;
    float nominalSize_ = 0.f;
    bgfx::TextureHandle atlas_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bool ownsProgram_ = false;  // BitmapFont owns a ref to the sprite program.
};

}  // namespace engine::ui
