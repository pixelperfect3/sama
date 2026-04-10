# Slug Font Backend — Next Steps

This document tracks work still pending on the `SlugFont` IFont backend
(Eric Lengyel's "GPU Centered Glyph Rendering"). The v1 implementation
landed in this worktree and is deliberately minimum-viable; see
`engine/ui/SlugFont.{h,cpp}` and `engine/shaders/{vs,fs}_slug.sc` for the
code, and the commit history of this branch for the full delivery.

## What the v1 ships

- FreeType2 TTF loader (`SAMA_HAS_FREETYPE` gated; stubs out cleanly when
  FreeType is not found).
- Extracts printable ASCII (32..126) glyph outlines via
  `FT_Outline_Decompose` into a flat `std::vector<float>` of quadratic
  Bezier control points (8 floats per curve, 2 RGBA32F texels).
- Uploads the curve data to a square-ish RGBA32F 2D texture.
- Slug shader pair (`vs_slug.sc` / `fs_slug.sc`) with a simple,
  unoptimized per-pixel ray-vs-curve winding evaluator. No banding, no
  AA, no dilation.
- `loadSlugProgram()` in `ShaderLoader.{h,cpp}` (Metal / SPIRV / GLSL /
  ESSL variants compiled by `shaderc`).
- `tests/ui/TestSlugFont.cpp` (4 test cases, 19 assertions). Loads
  JetBrains Mono, verifies glyph metrics + per-glyph curve data.
- `assets/fonts/default/JetBrainsMono-Regular.ttf` checked in.

## What's left

### Shader / rendering

1. **Band table.** The paper's core optimization is a horizontal band
   structure per glyph so each fragment only tests curves that overlap
   its scanline. The v1 loops over *every* curve in the glyph. Needs a
   second per-glyph buffer of `(bandCurveOffset, bandCurveCount)` pairs
   keyed by row.
2. **Proper antialiasing.** v1 returns a hard 0/1 coverage. Real Slug
   computes the exact horizontal coverage integral within a pixel
   window. Port this from the reference.
3. **Dynamic vertex dilation.** Slug's vertex shader expands the glyph
   quad by half a pixel so that antialiased edges aren't clipped. Add.
4. **Cubic curves.** `ftCubicTo` currently collapses a cubic to a single
   quadratic. TrueType almost never hits this but PostScript/CFF fonts
   will produce awful results. Use de Casteljau subdivision.
5. **Kerning.** `getKerning` is stubbed to 0. Wire through
   `FT_Get_Kerning` and cache pair advances in a hash map.
6. **Full Unicode.** v1 loads printable ASCII (95 glyphs). Needs a code
   path that loads glyphs lazily on first use, or a configurable range
   table (Latin, Latin-1 Supplement, CJK subranges...).

### Integration

7. **UiRenderer vertex layout coordination.** SlugFont lives as a
   standalone backend right now; UiRenderer has no Slug-aware submit
   path. The cleanest fix is to add an extra vertex attribute
   (`TEXCOORD1` carrying `vec2(curveOffset, curveCount)`) that bitmap
   and MSDF backends simply ignore. Alternatively, keep the current
   layout and submit one draw per glyph, setting `u_slugParams` each
   time — slower but layout-agnostic. Decision pending on which way to
   go; this is blocked on the bitmap agent's final vertex layout.
8. **`u_slugCurvesDim` population.** The fragment shader expects a
   uniform describing the curve texture dimensions. SlugFont allocates
   the uniform handle but does not currently set it because there is
   no Slug-aware UiRenderer path yet. Wire that up once (7) lands.
9. **End-to-end smoke test.** No rendered screenshot exists for Slug
   yet. Add a screenshot test once the UiRenderer integration is done,
   preferably rendering the letter `A` at several scales / rotations to
   prove the "vector-perfect at any angle" property.

### Operational

10. **FreeType as a FetchContent dependency.** Today the build uses
    `find_package(Freetype)`, which works on dev machines with
    Homebrew but will fail in sandboxed CI. Swap to a
    `FetchContent_Declare` of `VER-2-13-2` once the slow build cost
    (adds ~30s configure time) is acceptable, or keep the system-only
    path and require CI to install freetype via apt / brew.
11. **Cubic bezier approximation quality.** Current midpoint
    approximation loses shape fidelity; revisit when cubic-heavy fonts
    (CFF / OTF) are in scope.

## Rationale — why the v1 stops here

Per the task brief, the Slug backend is estimated at 2-3 weeks with the
reference implementation in hand. One session can reasonably ship:
loading, curve packing, shader stubs, and a unit test that proves the
loader works end to end. Shipping a half-working integrated renderer
that then needs to be undone later is strictly worse than shipping a
self-contained backend that other agents can integrate at their pace.

The v1 is written so that none of the other font backends, the
UiRenderer, the widgets, or the editor needs to change for the Slug
code to compile or for `[slug]` tests to pass. This is by design.
