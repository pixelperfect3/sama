// ---------------------------------------------------------------------------
// TestSlugFont — v1 coverage for the Slug IFont backend.
//
// engine_tests runs without a real bgfx device (no window), so bgfx
// reports RendererType::Noop. SlugFont::loadFromFile is written to still
// parse the TTF and build its curve buffer in that mode; only the GPU
// resource creation (texture, uniforms, program) is skipped. The test
// relies on that behaviour to validate the font loader without a GPU.
//
// When FreeType is not available at build time (SAMA_HAS_FREETYPE == 0)
// the whole test is SKIPped via a REQUIRE early-out so CI stays green.
// ---------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "engine/ui/SlugFont.h"

using namespace engine::ui;

namespace
{

// Keep the TTF path relative to the repo root — tests are launched from
// the build dir, so walk up until we find the assets directory. This is
// cheaper than threading a runtime path through CMake configure-time.
std::filesystem::path findJetBrainsMono()
{
    namespace fs = std::filesystem;
    fs::path cwd = fs::current_path();
    for (int i = 0; i < 6; ++i)
    {
        fs::path candidate = cwd / "assets" / "fonts" / "default" / "JetBrainsMono-Regular.ttf";
        if (fs::exists(candidate))
            return candidate;
        if (!cwd.has_parent_path())
            break;
        cwd = cwd.parent_path();
    }
    return {};
}

}  // namespace

TEST_CASE("SlugFont: reports Slug renderer identity", "[ui][slug]")
{
    SlugFont font;
    REQUIRE(font.renderer() == FontRenderer::Slug);
}

TEST_CASE("SlugFont: atlasTexture is always invalid (no atlas)", "[ui][slug]")
{
    SlugFont font;
    REQUIRE_FALSE(bgfx::isValid(font.atlasTexture()));
}

#if SAMA_HAS_FREETYPE

TEST_CASE("SlugFont: loads JetBrains Mono and builds curve buffer", "[ui][slug]")
{
    const auto ttf = findJetBrainsMono();
    if (ttf.empty())
    {
        WARN("JetBrainsMono-Regular.ttf not found — skipping Slug load test");
        return;
    }

    SlugFont font;
    const bool ok = font.loadFromFile(ttf.string().c_str(), 24.f);
    REQUIRE(ok);
    REQUIRE(font.isLoaded());
    REQUIRE(font.nominalSize() == 24.f);
    REQUIRE(font.lineHeight() > 0.f);
    REQUIRE(font.totalCurveCount() > 0);

    // The letter 'A' must be present with a positive advance and some
    // quadratic curves.
    const GlyphMetrics* a = font.getGlyph(static_cast<uint32_t>('A'));
    REQUIRE(a != nullptr);
    REQUIRE(a->advance > 0.f);
    REQUIRE(a->size.x > 0.f);
    REQUIRE(a->size.y > 0.f);

    const SlugFont::GlyphSlugData* aSlug = font.getGlyphSlugData(static_cast<uint32_t>('A'));
    REQUIRE(aSlug != nullptr);
    REQUIRE(aSlug->curveCount > 0);

    // Spot-check another printable to make sure the whole ASCII range
    // loaded (not just the first entry).
    const GlyphMetrics* zero = font.getGlyph(static_cast<uint32_t>('0'));
    REQUIRE(zero != nullptr);
    REQUIRE(zero->advance > 0.f);
}

TEST_CASE("SlugFont: missing glyphs return nullptr cleanly", "[ui][slug]")
{
    const auto ttf = findJetBrainsMono();
    if (ttf.empty())
    {
        WARN("JetBrainsMono-Regular.ttf not found — skipping Slug load test");
        return;
    }

    SlugFont font;
    REQUIRE(font.loadFromFile(ttf.string().c_str(), 24.f));

    // U+1F600 (emoji) is definitely not in the loaded printable-ASCII range.
    REQUIRE(font.getGlyph(0x1F600u) == nullptr);
    REQUIRE(font.getGlyphSlugData(0x1F600u) == nullptr);
    REQUIRE(font.getKerning('A', 'V') == 0.f);
}

#else  // SAMA_HAS_FREETYPE

TEST_CASE("SlugFont: stub mode when FreeType unavailable", "[ui][slug]")
{
    SlugFont font;
    REQUIRE_FALSE(font.loadFromFile("ignored.ttf", 24.f));
    REQUIRE_FALSE(font.isLoaded());
}

#endif  // SAMA_HAS_FREETYPE
