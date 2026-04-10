#pragma once

// ---------------------------------------------------------------------------
// SlugFont
//
// Third IFont backend. Implements Eric Lengyel's GPU algorithm for rendering
// glyphs directly from quadratic Bezier curves with no atlas (see the paper
// "GPU Centered Glyph Rendering", Lengyel 2017). The reference C++ + HLSL
// implementation is MIT-licensed at https://github.com/EricLengyel/Slug.
//
// Patent status: Lengyel's original patent was dedicated to the public domain
// in March 2026, so the algorithm is now free to implement. This engine's
// implementation is written from the paper + the MIT reference shaders.
//
// This is a minimum-viable first cut:
//   - Loads ASCII printable range (32..126) from a TTF via FreeType2.
//   - Walks each glyph's outline with FT_Outline_Decompose and records its
//     list of line and quadratic curve segments (cubics are approximated as
//     a single quadratic for v1; most TrueType fonts are natively quadratic).
//   - Packs the curve list for every glyph into one RGBA32F buffer texture
//     uploaded to bgfx. Each curve is three Vec2 control points (p0, p1, p2);
//     line segments are stored as a degenerate quadratic with p1 = (p0+p2)/2.
//   - Reports per-glyph `GlyphMetrics` (uvRect unused; size/offset/advance in
//     pixels at the font's nominal size) and a parallel `GlyphSlugData` map
//     that holds the (curveOffset, curveCount) pair into the curve buffer.
//   - Exposes the slug shader program via `program()` so a Slug-aware
//     UiRenderer can submit glyph quads with it.
//
// Integration note — vertex layout coordination:
//   The existing UiRenderer (owned by the bitmap agent) batches all glyph
//   quads from one text command into a single draw call. Per-glyph curve
//   offset/count metadata must flow from SlugFont to the slug fragment
//   shader. The first-cut strategy encodes (curveOffset, curveCount) into
//   the vertex color's alpha/extra channel of glyph quads. The slug shader
//   reads these from `v_color0.zw` instead of using them for blending.
//   Wiring this into UiRenderer's vertex layout and text submission path
//   is left as a follow-up (see docs/SLUG_NEXT_STEPS.md).
//
// Build note:
//   When FreeType2 is not found by CMake, SAMA_HAS_FREETYPE is left
//   undefined and SlugFont compiles as a stub: loadFromFile() returns false
//   and all lookups return empty. Tests skip gracefully in this mode.
// ---------------------------------------------------------------------------

#include <ankerl/unordered_dense.h>
#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

#include "engine/ui/IFont.h"

namespace engine::ui
{

class SlugFont final : public IFont
{
public:
    SlugFont() = default;
    ~SlugFont() override;

    SlugFont(const SlugFont&) = delete;
    SlugFont& operator=(const SlugFont&) = delete;

    // Loads a TrueType/OpenType file and builds the curve buffer. Returns
    // false on any FreeType error or when FreeType is unavailable at build
    // time. On success, `program()` is a valid bgfx program unless running
    // on the Noop renderer (headless tests).
    bool loadFromFile(const char* ttfPath, float pixelSize);

    // Destroys all bgfx handles and clears the glyph maps. Safe to call
    // multiple times.
    void shutdown();

    // ------------------------------------------------------------------
    // IFont interface
    // ------------------------------------------------------------------

    FontRenderer renderer() const noexcept override
    {
        return FontRenderer::Slug;
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
        return BGFX_INVALID_HANDLE;
    }
    bgfx::ProgramHandle program() const noexcept override
    {
        return program_;
    }
    void bindResources() const override;

    // ------------------------------------------------------------------
    // Slug-specific accessors (used by a Slug-aware UiRenderer path or by
    // tests that need to verify the curve buffer was built correctly).
    // ------------------------------------------------------------------

    struct GlyphSlugData
    {
        uint32_t curveOffset = 0;  // first curve index in curveBuffer_
        uint32_t curveCount = 0;   // number of curves belonging to this glyph
    };

    const GlyphSlugData* getGlyphSlugData(uint32_t codepoint) const;
    uint32_t totalCurveCount() const noexcept
    {
        return totalCurveCount_;
    }
    bool isLoaded() const noexcept
    {
        return loaded_;
    }

private:
    // Each curve is 3 x Vec2 = 6 floats packed as 1.5 RGBA32F texels. We pad
    // to 2 full texels (8 floats) per curve so the shader can address curves
    // linearly without unpacking headaches. Layout per curve in the buffer
    // texel stream:
    //   texel[2n+0] = (p0.x, p0.y, p1.x, p1.y)
    //   texel[2n+1] = (p2.x, p2.y, pad,  pad)
    static constexpr uint32_t kFloatsPerCurve = 8;

    ankerl::unordered_dense::map<uint32_t, GlyphMetrics> glyphs_;
    ankerl::unordered_dense::map<uint32_t, GlyphSlugData> slugData_;
    std::vector<float> curveBuffer_;

    bgfx::TextureHandle curveBufferTexture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_curveBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_slugParams_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;

    float lineHeight_ = 0.f;
    float nominalSize_ = 0.f;
    uint32_t totalCurveCount_ = 0;
    bool loaded_ = false;
};

}  // namespace engine::ui
