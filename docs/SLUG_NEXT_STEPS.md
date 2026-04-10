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
- `tests/ui/TestSlugFont.cpp` (4 test cases, 19 assertions). Loads the
  bundled ChunkFive TTF, verifies glyph metrics + per-glyph curve data.
- `assets/fonts/ChunkFive-Regular.ttf` (SIL OFL, The League of Movable
  Type) checked in as the default TTF source.

## What's working now (post end-to-end integration)

- All 95 ASCII glyphs render through UiRenderer when SlugFont is the
  active backend (verified by `tests/golden/ui_text_slug.png`).
- Per-glyph curve range is set as a uniform (one draw call per glyph).
- Glyph quads land at the correct vertical position (the y-down
  line-relative metric convention now matches BitmapFont/MsdfFont).
- The curve buffer texture and dimensions uniform are bound through
  `SlugFont::bindResources()` and `SlugFont::setCurrentGlyph()`.

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

7. ~~**UiRenderer vertex layout coordination.**~~ **DONE** (commit on
   2026-04-10). Picked the layout-agnostic option: UiRenderer dispatches
   to a `renderSlugText` helper when `font->renderer() == FontRenderer::Slug`,
   submitting one draw per glyph. The per-vertex TEXCOORD0 carries
   font-space corners (instead of an atlas UV) so the slug fragment
   shader can compute its glyph-local position without a vertex layout
   change. Slower than batching but unblocks end-to-end rendering;
   batched submission with a vertex attribute is a follow-up if perf
   becomes an issue.
8. ~~**`u_slugCurvesDim` population.**~~ **DONE** (same commit).
   `SlugFont::bindResources()` now sets `u_slugCurvesDim` from the
   stored curve texture dimensions, and the per-glyph `u_slugParams`
   uniform is set via the new `SlugFont::setCurrentGlyph(offset, count)`
   method called by `UiRenderer::renderSlugText` before each submit.
9. ~~**End-to-end smoke test.**~~ **DONE** —
   `tests/screenshot/TestSsUiText.cpp` already covers Slug; the golden
   `tests/golden/ui_text_slug.png` was regenerated to capture the
   working render. Vector-at-any-angle test (rotation, perspective)
   still pending.

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
