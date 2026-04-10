#pragma once

#include "engine/math/Types.h"

namespace engine::ui
{

class IFont;

// ---------------------------------------------------------------------------
// measureText
//
// Walks `text` glyph-by-glyph and returns {width, height} in logical
// pixels at the given font size. If `font` is null, the engine default
// font is used. Unknown codepoints are skipped. Height is always the
// font's line height scaled by (fontSize / nominalSize).
// ---------------------------------------------------------------------------

math::Vec2 measureText(const IFont* font, float fontSize, const char* text);

}  // namespace engine::ui
