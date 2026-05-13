#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

// ---------------------------------------------------------------------------
// SelectionOutline -- pure helpers that build the bgfx state and stencil
// masks for the editor's two-pass selection-outline render.
//
// Pass 1 (stencil-fill):   render the selected mesh into the HDR scene FB's
//                          D24S8 stencil with REF=1, OP_PASS=REPLACE.  No
//                          color writes; depth test ON so only the visible
//                          surface marks stencil = 1.
//
// Pass 2 (outline draw):   re-render the selected mesh inflated along its
//                          normal with stencil_test = NOT_EQUAL 1, painting
//                          a solid silhouette band wherever stencil != 1.
//                          Depth test OFF so the outline pokes through any
//                          geometry that occludes the entity (the whole
//                          point: "where is my selection when the gizmo is
//                          hidden by a wall?").
//
// The helpers are kept as constexpr functions in a header so they link into
// engine_tests without pulling in the editor's Cocoa surface — see
// tests/editor/TestSelectionOutline.cpp.  The bgfx-defined STATE / STENCIL
// macros are #defines that fold into integer literals at compile time, so
// constexpr is safe.
// ---------------------------------------------------------------------------

namespace engine::editor
{

// State for the stencil-fill draw.
//   - WRITE_Z, WRITE_A both off: the only side-effect is stencil.  Color
//     writes are off because we don't want to alter the tonemapped scene
//     image; only the stencil bit-plane matters here.
//   - No DEPTH_TEST flag (bgfx: absence = always pass).  Depth test ON
//     would only mark stencil where the selected mesh is the *closest*
//     geometry — so any pixels where another object occludes the
//     selection would stay stencil=0, and pass 2's inflated silhouette
//     would then paint over those occluder pixels because NOT_EQUAL 1
//     passes there too (the "ground selected, cube turns yellow" bug
//     this comment block exists to prevent regressing).  We want stencil
//     marked wherever the mesh projects into screen space regardless of
//     occlusion; pass 2 then computes (inflated silhouette ∖ original
//     silhouette) as exactly the band that should glow.
//   - CULL_CW matches the engine's default winding for static meshes.
//     This is critical: without back-face culling, with depth test off,
//     the back faces would also project and stencil-mark a region
//     larger than the visible silhouette.
[[nodiscard]] constexpr uint64_t outlineStencilFillState() noexcept
{
    return BGFX_STATE_CULL_CW | BGFX_STATE_MSAA;
}

// Stencil mask for the stencil-fill draw.
//   TEST_ALWAYS              — every fragment that survives depth test
//                              passes the stencil test...
//   FUNC_REF(1) FUNC_RMASK   — ...and writes the reference value (1) into
//                              the stencil byte (RMASK is the test mask;
//                              all 8 bits are tested even though only the
//                              low bit is used).
//   OP_FAIL_S/Z_KEEP         — leave stencil unchanged on stencil-/depth-
//                              fail (a depth-failed pixel is occluded).
//   OP_PASS_Z_REPLACE        — write the REF value (1) on success.
[[nodiscard]] constexpr uint32_t outlineStencilFillStencilFront() noexcept
{
    return BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0xff) |
           BGFX_STENCIL_OP_FAIL_S_KEEP | BGFX_STENCIL_OP_FAIL_Z_KEEP |
           BGFX_STENCIL_OP_PASS_Z_REPLACE;
}

// State for the outline draw.
//   - WRITE_RGB / WRITE_A on: we paint visible silhouette pixels.
//   - WRITE_Z OFF: the outline must not update depth (it would otherwise
//     occlude things behind it for any subsequent draw that reads depth).
//   - No DEPTH_TEST flag: bgfx treats absence of a DEPTH_TEST_* bit as
//     "always pass" — the outline therefore renders even when occluded by
//     other geometry, which is exactly what we want for a visibility cue
//     ("where is my selection right now?").
//   - CULL_CCW so we keep the back-face inflation visible from the front
//     and discard the front faces; combined with stencil_test=NOT_EQUAL 1
//     this produces a clean silhouette.  Front-face culling here would
//     also work but flips the visual when the mesh has open boundaries.
[[nodiscard]] constexpr uint64_t outlineDrawState() noexcept
{
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_CULL_CCW | BGFX_STATE_MSAA;
}

// Stencil mask for the outline draw.
//   TEST_NOTEQUAL FUNC_REF(1) FUNC_RMASK(0xff)  — pass only where stencil
//   != 1 (i.e., outside the visible mesh footprint marked by pass 1).
//   OP_*_KEEP — never modify stencil; the outline pass is purely a
//   consumer.  Keeping stencil untouched also lets the same FB be reused
//   for further passes if we ever stack effects.
[[nodiscard]] constexpr uint32_t outlineDrawStencilFront() noexcept
{
    return BGFX_STENCIL_TEST_NOTEQUAL | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0xff) |
           BGFX_STENCIL_OP_FAIL_S_KEEP | BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_KEEP;
}

}  // namespace engine::editor
