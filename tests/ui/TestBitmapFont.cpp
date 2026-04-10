#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/rendering/Renderer.h"
#include "engine/ui/BitmapFont.h"
#include "engine/ui/DefaultFont.h"
#include "engine/ui/IFont.h"
#include "engine/ui/Measure.h"

using namespace engine::ui;

// ---------------------------------------------------------------------------
// HeadlessBgfx — RAII bgfx init (Noop renderer) so BitmapFont can call
// bgfx::createTexture2D / loadSpriteProgram without segfaulting. Mirrors
// the helper in tests/rendering/TestUi.cpp.
// ---------------------------------------------------------------------------

namespace
{

struct HeadlessBgfx
{
    engine::rendering::Renderer renderer;

    HeadlessBgfx()
    {
        engine::rendering::RendererDesc desc{};
        desc.headless = true;
        desc.width = 256;
        desc.height = 256;
        REQUIRE(renderer.init(desc));
    }

    ~HeadlessBgfx()
    {
        shutdownDefaultFont();  // release any cached atlas before bgfx dies
        renderer.shutdown();
    }
};

}  // namespace

// ===========================================================================
// BitmapFont: debug-font creation + glyph lookup + measurement
//
// Runs under the bgfx Noop renderer (no window / GPU). createDebugFont
// still populates the glyph map and lineHeight/nominalSize, so the tests
// exercise the pure-data path without touching bgfx::createTexture2D
// output.
// ===========================================================================

TEST_CASE("BitmapFont debug font populates printable ASCII range", "[ui][font]")
{
    HeadlessBgfx ctx;
    BitmapFont font;
    REQUIRE(font.createDebugFont());

    // 96 printable ASCII glyphs (0x20..0x7F).
    REQUIRE(font.glyphCount() == 96);

    // Space is present but has zero visible size, non-zero advance.
    const auto* space = font.getGlyph(' ');
    REQUIRE(space != nullptr);
    REQUIRE(space->size.x == Catch::Approx(0.f));
    REQUIRE(space->advance > 0.f);

    // A printable letter exists and has non-zero quad dimensions.
    const auto* A = font.getGlyph('A');
    REQUIRE(A != nullptr);
    REQUIRE(A->size.x > 0.f);
    REQUIRE(A->size.y > 0.f);
    REQUIRE(A->advance > 0.f);
    REQUIRE(A->uvRect.z > A->uvRect.x);
    REQUIRE(A->uvRect.w > A->uvRect.y);

    // Non-printable codepoint returns nullptr.
    REQUIRE(font.getGlyph(0x01) == nullptr);

    REQUIRE(font.nominalSize() > 0.f);
    REQUIRE(font.lineHeight() > 0.f);
    REQUIRE(font.renderer() == FontRenderer::Bitmap);
}

TEST_CASE("BitmapFont getKerning returns zero for unknown pairs", "[ui][font]")
{
    HeadlessBgfx ctx;
    BitmapFont font;
    REQUIRE(font.createDebugFont());
    REQUIRE(font.getKerning('A', 'V') == Catch::Approx(0.f));
    REQUIRE(font.getKerning('x', 'y') == Catch::Approx(0.f));
}

TEST_CASE("measureText advances by glyph count at nominal size", "[ui][font]")
{
    HeadlessBgfx ctx;
    BitmapFont font;
    REQUIRE(font.createDebugFont());

    const auto zero = measureText(&font, font.nominalSize(), "");
    REQUIRE(zero.x == Catch::Approx(0.f));

    const auto single = measureText(&font, font.nominalSize(), "A");
    REQUIRE(single.x == Catch::Approx(font.getGlyph('A')->advance));
    REQUIRE(single.y == Catch::Approx(font.lineHeight()));

    // Monospace debug font: "Hello" == 5 * advance('A').
    const float expected = 5.f * font.getGlyph('A')->advance;
    const auto hello = measureText(&font, font.nominalSize(), "Hello");
    REQUIRE(hello.x == Catch::Approx(expected));

    // Halving the font size halves the width.
    const auto halfHello = measureText(&font, font.nominalSize() * 0.5f, "Hello");
    REQUIRE(halfHello.x == Catch::Approx(expected * 0.5f));
}

TEST_CASE("defaultFont lazy-initialises a BitmapFont", "[ui][font]")
{
    HeadlessBgfx ctx;
    IFont* f = defaultFont();
    REQUIRE(f != nullptr);
    REQUIRE(f->renderer() == FontRenderer::Bitmap);
    REQUIRE(f->getGlyph('a') != nullptr);

    // Second call returns the same instance.
    IFont* f2 = defaultFont();
    REQUIRE(f2 == f);
}
