// UI text rendering screenshot tests — one screenshot per IFont backend.
//
// Each test case loads a font (BitmapFont via the embedded debug atlas,
// MsdfFont via the bundled JetBrains Mono atlas, SlugFont via the
// JetBrains Mono TTF), draws the same sample text into a UiDrawList,
// and submits it through UiRenderer onto the offscreen capture target.
//
// The Slug case is allowed to render as nothing (or noisy junk) until the
// UiRenderer integration in SLUG_NEXT_STEPS.md §7 lands; the goal is just
// to lock the rendering path against regressions and to give us a visible
// before/after when the integration finally ships.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#include "engine/ui/BitmapFont.h"
#include "engine/ui/IFont.h"
#include "engine/ui/MsdfFont.h"
#include "engine/ui/SlugFont.h"
#include "engine/ui/UiCanvas.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiRenderer.h"

namespace
{

// Walk up from the current working directory until we find an `assets/`
// sibling. Tests are launched from the build dir so we usually need one
// or two levels up; six is a safe upper bound.
std::string findRepoFile(const char* relPath)
{
    namespace fs = std::filesystem;
    fs::path cwd = fs::current_path();
    for (int i = 0; i < 6; ++i)
    {
        fs::path candidate = cwd / relPath;
        if (fs::exists(candidate))
            return candidate.string();
        if (!cwd.has_parent_path())
            break;
        cwd = cwd.parent_path();
    }
    return {};
}

void renderSample(engine::ui::IFont* font, const char* goldenName,
                  engine::screenshot::ScreenshotFixture& fx)
{
    engine::ui::UiCanvas canvas(fx.width(), fx.height());
    engine::ui::UiRenderer renderer;
    renderer.init();

    auto& dl = canvas.drawList();

    // Three sizes so the screenshot exercises font scaling at title,
    // body, and small sizes. Different colors so any sampler/UV bug
    // shows up clearly.
    dl.drawText({12.f, 12.f}, "Sama Engine", {1.f, 1.f, 1.f, 1.f}, font, 24.f);
    dl.drawText({12.f, 56.f}, "The quick brown fox jumps over the lazy dog",
                {0.85f, 0.85f, 1.f, 1.f}, font, 14.f);
    dl.drawText({12.f, 92.f}, "0123456789  !@#$%^&*()", {1.f, 0.85f, 0.5f, 1.f}, font, 16.f);
    dl.drawText({12.f, 132.f}, "abc ABC def DEF ghi GHI", {0.6f, 1.f, 0.6f, 1.f}, font, 18.f);
    dl.drawText({12.f, 180.f}, "Multi-size + multi-color sample", {1.f, 0.7f, 0.7f, 1.f}, font,
                12.f);

    const auto viewId = engine::rendering::kViewGameUi;
    engine::rendering::RenderPass(viewId)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x101018ff);

    renderer.render(dl, viewId, fx.width(), fx.height());

    auto pixels = fx.captureFrame();
    renderer.shutdown();

    REQUIRE(engine::screenshot::compareOrUpdateGolden(goldenName, pixels, fx.width(), fx.height()));
}

}  // namespace

TEST_CASE("screenshot: UI text — BitmapFont (embedded debug atlas)", "[screenshot][ui][text]")
{
    engine::screenshot::ScreenshotFixture fx;

    engine::ui::BitmapFont font;
    REQUIRE(font.createDebugFont());

    renderSample(&font, "ui_text_bitmap", fx);

    font.shutdown();
}

TEST_CASE("screenshot: UI text — MsdfFont (JetBrains Mono atlas)", "[screenshot][ui][text]")
{
    engine::screenshot::ScreenshotFixture fx;

    const std::string jsonPath = findRepoFile("assets/fonts/JetBrainsMono-msdf.json");
    const std::string pngPath = findRepoFile("assets/fonts/JetBrainsMono-msdf.png");
    if (jsonPath.empty() || pngPath.empty())
    {
        WARN("JetBrains Mono MSDF atlas not found — skipping MSDF text screenshot test");
        return;
    }

    engine::ui::MsdfFont font;
    REQUIRE(font.loadFromFile(jsonPath.c_str(), pngPath.c_str()));

    renderSample(&font, "ui_text_msdf", fx);

    font.shutdown();
}

#if SAMA_HAS_FREETYPE

TEST_CASE("screenshot: UI text — SlugFont (JetBrains Mono TTF)", "[screenshot][ui][text]")
{
    engine::screenshot::ScreenshotFixture fx;

    const std::string ttfPath = findRepoFile("assets/fonts/JetBrainsMono-Regular.ttf");
    if (ttfPath.empty())
    {
        WARN("JetBrains Mono TTF not found — skipping Slug text screenshot test");
        return;
    }

    engine::ui::SlugFont font;
    REQUIRE(font.loadFromFile(ttfPath.c_str(), 24.f));

    // Slug's UiRenderer integration is incomplete (SLUG_NEXT_STEPS.md §7).
    // The golden image will currently capture whatever the partial path
    // produces — likely an empty or noisy frame — but we still want the
    // test to lock that state so any future change shows up as a delta.
    renderSample(&font, "ui_text_slug", fx);

    font.shutdown();
}

#endif  // SAMA_HAS_FREETYPE
