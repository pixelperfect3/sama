# Default UI font assets

This directory holds the engine's built-in UI font assets. Each font is
represented by a pair of files:

- `<Name>-<Variant>.fnt` / `.png` — bitmap font (BMFont format).
- `<Name>-<Variant>-msdf.json` / `-msdf.png` — multi-channel signed distance
  field font (msdf-atlas-gen format).

At runtime the engine picks between the two at startup based on GPU tier
(see `docs/GAME_LAYER_ARCHITECTURE.md` §5).

## Regenerating the MSDF atlas

The MSDF atlas is produced by
[msdf-atlas-gen](https://github.com/Chlumsky/msdf-atlas-gen). It is not a
build-time dependency — the JSON + PNG are checked into git and consumed
directly by `engine::ui::MsdfFont::loadFromFile()`.

Install on macOS:

```sh
brew install msdf-atlas-gen
```

Then, from a directory containing `JetBrainsMono-Regular.ttf`, run:

```sh
msdf-atlas-gen \
    -font JetBrainsMono-Regular.ttf \
    -size 24 \
    -pxrange 4 \
    -format png \
    -type msdf \
    -imageout assets/fonts/default/JetBrainsMono-Regular-msdf.png \
    -json     assets/fonts/default/JetBrainsMono-Regular-msdf.json \
    -charset  ascii
```

Key flags:

- `-size 24` — pixels per em. The shader reconstructs to any on-screen size;
  24 is the nominal baseline.
- `-pxrange 4` — distance range in atlas pixels. Must match the
  `distanceRange_` field the fragment shader reads from `u_msdfParams.x`.
- `-type msdf` — multi-channel (RGB) signed distance field; the default.
- `-charset ascii` — ASCII printable range. Swap for a custom charset file
  when you need wider coverage.

Commit both output files. `MsdfFont` reads JSON first (metrics, glyph rects,
kerning) then decodes the PNG atlas via stb_image and uploads it as an
RGBA8 texture with bilinear filtering.

## Current status

The JetBrains Mono MSDF atlas has **not** been generated in this checkout
because `msdf-atlas-gen` is not available in the build environment. Until a
developer runs the command above and checks in the result, `MsdfFont::
loadFromFile()` called against these paths will fail cleanly (returning
false) and callers should fall back to the bitmap font.
