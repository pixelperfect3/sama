#pragma once

#include "engine/math/Types.h"

namespace engine::ui
{

// ---------------------------------------------------------------------------
// GlyphMetrics
//
// Per-glyph atlas record returned by IFont::getGlyph(). The same struct is
// used by all font backends (BitmapFont, MsdfFont) that share the
// "atlas + per-glyph quad" rendering model. Slug fonts use a different,
// curve-based path and ignore the uvRect/atlas fields entirely (see
// SlugFont::getGlyph for details).
//
// Coordinates are in logical pixels at the font's nominal size. The renderer
// scales by (requestedSize / nominalSize) at draw time.
// ---------------------------------------------------------------------------

struct GlyphMetrics
{
    math::Vec4 uvRect{0.f, 0.f, 0.f, 0.f};  // {u0, v0, u1, v1} in atlas
    math::Vec2 size{0.f, 0.f};              // glyph quad size in pixels
    math::Vec2 offset{0.f, 0.f};            // bearing offset from cursor (left, top)
    float advance = 0.f;                    // horizontal advance to next glyph
};

}  // namespace engine::ui
