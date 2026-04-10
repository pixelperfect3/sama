# UI font assets

This directory holds default UI fonts that ship with the engine.

## ChunkFive-Regular.ttf

Slab-serif display face by The League of Movable Type. SIL Open Font License
(see `ChunkFive-OFL.md`). Free to use, modify, and redistribute. Used as the
default TTF source for `engine::ui::SlugFont` and as the source any developer
can run through `msdf-atlas-gen` to produce a default MSDF atlas.

Source: https://www.theleagueofmoveabletype.com/chunkfive

## Generating an MSDF atlas (optional)

`engine::ui::MsdfFont::loadFromFile()` expects a JSON + PNG pair produced by
[msdf-atlas-gen](https://github.com/Chlumsky/msdf-atlas-gen). To generate
the default MSDF atlas from ChunkFive:

```sh
brew install msdf-atlas-gen
msdf-atlas-gen \
    -font assets/fonts/ChunkFive-Regular.ttf \
    -size 24 \
    -pxrange 4 \
    -format png \
    -type msdf \
    -imageout assets/fonts/ChunkFive-msdf.png \
    -json     assets/fonts/ChunkFive-msdf.json \
    -charset  ascii
```

Until those files exist, `MsdfFont::loadFromFile()` returns false cleanly
and callers fall back to the bitmap default.

## Museo Sans

Was requested by the user, but not committed: Museo Sans 500 has a
restrictive EULA from exljbris that allows free personal/commercial use of
the font but does **not** permit redistributing the font file in a public
repository. If you want Museo Sans in this engine, download it manually
from https://www.fontsquirrel.com/fonts/museo-sans, place
`MuseoSans_500.otf` in this directory, and the test app's font path
resolver will pick it up if you swap `ChunkFive-Regular.ttf` for it in
`apps/ui_test/UiTestApp.cpp`.
