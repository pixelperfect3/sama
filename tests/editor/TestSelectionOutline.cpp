// ----------------------------------------------------------------------------
// TestSelectionOutline -- unit tests for the bgfx state / stencil masks used
// by the editor's two-pass selection outline (`editor/SelectionOutline.h`).
//
// The helpers live in a header (constexpr functions over bgfx integer
// macros) so they link into engine_tests without dragging in the editor's
// Cocoa surface.  The tests pin the contract that drives EditorApp's
// stencil-fill + inflated outline passes; getting any of these bits wrong
// silently breaks the visual (no outline, double outline, outline through
// floors, etc.).
// ----------------------------------------------------------------------------

#include <bgfx/bgfx.h>

#include <catch2/catch_test_macros.hpp>

#include "editor/SelectionOutline.h"

using engine::editor::outlineDrawState;
using engine::editor::outlineDrawStencilFront;
using engine::editor::outlineStencilFillState;
using engine::editor::outlineStencilFillStencilFront;

TEST_CASE("SelectionOutline: stencil-fill state writes neither color nor depth",
          "[editor][selection_outline]")
{
    const uint64_t s = outlineStencilFillState();

    // Color writes must be disabled — the stencil pass exists only to
    // populate the stencil buffer; touching color would draw the mesh
    // twice and ruin the look.
    CHECK((s & BGFX_STATE_WRITE_RGB) == 0);
    CHECK((s & BGFX_STATE_WRITE_A) == 0);

    // Depth writes off too — the regular opaque pass already populated
    // depth; the stencil pass should not perturb it.
    CHECK((s & BGFX_STATE_WRITE_Z) == 0);
}

TEST_CASE("SelectionOutline: stencil-fill state respects depth occlusion",
          "[editor][selection_outline]")
{
    const uint64_t s = outlineStencilFillState();

    // Depth test ON — without it, the back face of the selected mesh would
    // also mark stencil = 1, causing the outline pass to skip pixels behind
    // the mesh and breaking visibility-aware silhouettes.  Must be LEQUAL
    // (not LESS) because the opaque pass already wrote the selected mesh's
    // depth, so DEPTH_TEST_LESS would fail every fragment and stencil
    // would never get written — see the comment in SelectionOutline.h.
    CHECK((s & BGFX_STATE_DEPTH_TEST_LEQUAL) == BGFX_STATE_DEPTH_TEST_LEQUAL);
    // Specifically not LESS — if anyone ever reverts to LESS, the test
    // catches it before the editor ships with no outline.
    CHECK((s & BGFX_STATE_DEPTH_TEST_MASK) != BGFX_STATE_DEPTH_TEST_LESS);
}

TEST_CASE("SelectionOutline: stencil-fill replaces stencil with reference value 1",
          "[editor][selection_outline]")
{
    const uint32_t st = outlineStencilFillStencilFront();

    // Test always passes (we want every depth-passing fragment to write).
    CHECK((st & BGFX_STENCIL_TEST_MASK) == BGFX_STENCIL_TEST_ALWAYS);

    // Reference value is 1 — the outline pass tests for NOT_EQUAL 1, so
    // changing this constant in either place silently breaks the masking.
    CHECK((st & BGFX_STENCIL_FUNC_REF_MASK) == BGFX_STENCIL_FUNC_REF(1));

    // Pass-Z op = REPLACE so the reference value lands in the stencil byte
    // when the fragment passes both stencil and depth tests.
    CHECK((st & BGFX_STENCIL_OP_PASS_Z_MASK) == BGFX_STENCIL_OP_PASS_Z_REPLACE);

    // Stencil-fail and depth-fail KEEP the existing stencil value — a
    // depth-occluded fragment is *not* part of the visible silhouette and
    // must not contribute to the mask.
    CHECK((st & BGFX_STENCIL_OP_FAIL_S_MASK) == BGFX_STENCIL_OP_FAIL_S_KEEP);
    CHECK((st & BGFX_STENCIL_OP_FAIL_Z_MASK) == BGFX_STENCIL_OP_FAIL_Z_KEEP);
}

TEST_CASE("SelectionOutline: outline-draw state writes color but not depth",
          "[editor][selection_outline]")
{
    const uint64_t s = outlineDrawState();

    // The whole point of the outline pass is to paint silhouette pixels.
    CHECK((s & BGFX_STATE_WRITE_RGB) == BGFX_STATE_WRITE_RGB);
    CHECK((s & BGFX_STATE_WRITE_A) == BGFX_STATE_WRITE_A);

    // ...but NOT depth.  Writing depth here would block any subsequent
    // draw that reads depth (notably, the next frame's PBR pass on a
    // dirty viewport would have stale outline depth in its inputs).
    CHECK((s & BGFX_STATE_WRITE_Z) == 0);
}

TEST_CASE("SelectionOutline: outline-draw state has no DEPTH_TEST_* bit set",
          "[editor][selection_outline]")
{
    const uint64_t s = outlineDrawState();

    // bgfx treats absence of any DEPTH_TEST_* bit as "always pass".  The
    // outline must render even where occluded so the user can see what
    // they have selected when it is hidden behind a wall, the floor, or
    // the gizmo from a glancing angle.
    CHECK((s & BGFX_STATE_DEPTH_TEST_MASK) == 0);
}

TEST_CASE("SelectionOutline: outline-draw stencil tests NOT_EQUAL 1 and never writes",
          "[editor][selection_outline]")
{
    const uint32_t st = outlineDrawStencilFront();

    // Pass only where stencil != 1 — i.e., outside the visible mesh
    // footprint that pass 1 marked.  This is what produces the
    // silhouette-band shape from the inflated geometry.
    CHECK((st & BGFX_STENCIL_TEST_MASK) == BGFX_STENCIL_TEST_NOTEQUAL);
    CHECK((st & BGFX_STENCIL_FUNC_REF_MASK) == BGFX_STENCIL_FUNC_REF(1));

    // Outline pass is purely a consumer — it must never modify stencil
    // (otherwise the next frame's stencil-fill on an idle viewport would
    // start from a polluted state, since BGFX_CLEAR_STENCIL only fires on
    // viewportDirty=true frames).
    CHECK((st & BGFX_STENCIL_OP_FAIL_S_MASK) == BGFX_STENCIL_OP_FAIL_S_KEEP);
    CHECK((st & BGFX_STENCIL_OP_FAIL_Z_MASK) == BGFX_STENCIL_OP_FAIL_Z_KEEP);
    CHECK((st & BGFX_STENCIL_OP_PASS_Z_MASK) == BGFX_STENCIL_OP_PASS_Z_KEEP);
}

TEST_CASE("SelectionOutline: stencil reference values match between passes",
          "[editor][selection_outline]")
{
    // The two passes coordinate via the stencil REF(1) constant.  This
    // test catches the "I changed REF in one place but forgot the other"
    // bug at compile-time of the test binary, well before anyone notices
    // the outline rendered nowhere or everywhere.
    const uint32_t fillRef = outlineStencilFillStencilFront() & BGFX_STENCIL_FUNC_REF_MASK;
    const uint32_t drawRef = outlineDrawStencilFront() & BGFX_STENCIL_FUNC_REF_MASK;
    CHECK(fillRef == drawRef);
    CHECK(fillRef == BGFX_STENCIL_FUNC_REF(1));
}
