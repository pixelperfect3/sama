#pragma once

namespace engine::ui
{

class IFont;

// ---------------------------------------------------------------------------
// defaultFont()
//
// Lazy-initialised singleton returning the engine's fallback font. The
// first call constructs a BitmapFont::createDebugFont() atlas — a 96-glyph
// ASCII monospace font packed in-memory from an embedded 8x8 bitmap. The
// pointer is valid for the lifetime of the bgfx context; callers must not
// store it across a bgfx shutdown/reinit.
//
// Widgets (UiText, UiButton) fall back to this when their `font` field is
// null, and UiRenderer does the same when a Text command carries no font.
// ---------------------------------------------------------------------------

IFont* defaultFont();

// Explicitly destroy the cached default font. Called by engine shutdown
// paths that want deterministic bgfx resource cleanup before the context
// dies. Safe to call multiple times.
void shutdownDefaultFont();

}  // namespace engine::ui
