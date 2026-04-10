// Additional UI screenshot tests covering widgets and layout combinations
// that the existing TestSsUiPanel and TestSsUiText cases don't exercise.
//
// Each TEST_CASE constructs a small UiCanvas, lays out a few widgets via
// the engine::ui retained-mode tree, runs canvas.update() to compute
// layout, then submits the resulting draw list to the offscreen capture
// target. Goldens are stored in tests/golden/ and compared with the
// shared GoldenCompare helper.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/rendering/ViewIds.h"
#include "engine/ui/BitmapFont.h"
#include "engine/ui/MsdfFont.h"
#include "engine/ui/UiCanvas.h"
#include "engine/ui/UiEvent.h"
#include "engine/ui/UiRenderer.h"
#include "engine/ui/widgets/UiButton.h"
#include "engine/ui/widgets/UiPanel.h"
#include "engine/ui/widgets/UiProgressBar.h"
#include "engine/ui/widgets/UiSlider.h"
#include "engine/ui/widgets/UiText.h"

namespace
{

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

// Helper: clear + viewport setup for the offscreen UI view.
void prepareUiView(engine::screenshot::ScreenshotFixture& fx, uint32_t clearColor = 0x101018ff)
{
    const auto viewId = engine::rendering::kViewGameUi;
    bgfx::setViewFrameBuffer(viewId, fx.captureFb());
    bgfx::setViewRect(viewId, 0, 0, fx.width(), fx.height());
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor, 1.0f, 0);
}

}  // namespace

// ---------------------------------------------------------------------------
// Anchor + offset layout: a panel anchored to all four edges with margins
// should resize to fill the canvas minus its margin offsets. Verifies the
// Phase 2 layout system end-to-end.
// ---------------------------------------------------------------------------

TEST_CASE("screenshot: UI anchor stretch", "[screenshot][ui]")
{
    engine::screenshot::ScreenshotFixture fx;

    engine::ui::UiCanvas canvas(fx.width(), fx.height());
    engine::ui::UiRenderer renderer;
    renderer.init();

    // Outer panel: stretched across the canvas with a 16-pixel margin.
    auto* outer = canvas.createNode<engine::ui::UiPanel>("outer");
    outer->anchor = {{0.f, 0.f}, {1.f, 1.f}};
    outer->offsetMin = {16.f, 16.f};
    outer->offsetMax = {-16.f, -16.f};
    outer->color = {0.2f, 0.25f, 0.35f, 1.f};

    // Inner panel: nested, anchored to the outer top-right with a fixed size.
    auto* inner = canvas.createNode<engine::ui::UiPanel>("inner");
    inner->anchor = {{1.f, 0.f}, {1.f, 0.f}};
    inner->offsetMin = {-90.f, 12.f};
    inner->offsetMax = {-12.f, 60.f};
    inner->color = {0.9f, 0.6f, 0.2f, 1.f};

    // Bottom-center bar: stretches horizontally near the bottom edge.
    auto* bar = canvas.createNode<engine::ui::UiPanel>("bar");
    bar->anchor = {{0.1f, 1.f}, {0.9f, 1.f}};
    bar->offsetMin = {0.f, -40.f};
    bar->offsetMax = {0.f, -12.f};
    bar->color = {0.4f, 0.85f, 0.45f, 1.f};

    canvas.root()->addChild(outer);
    outer->addChild(inner);
    canvas.root()->addChild(bar);
    canvas.update();

    prepareUiView(fx);
    renderer.render(canvas.drawList(), engine::rendering::kViewGameUi, fx.width(), fx.height());

    auto pixels = fx.captureFrame();
    renderer.shutdown();

    REQUIRE(engine::screenshot::compareOrUpdateGolden("ui_anchor_stretch", pixels, fx.width(),
                                                      fx.height()));
}

// ---------------------------------------------------------------------------
// Progress bar at three fill levels (0%, 50%, 100%). Verifies the
// UiProgressBar widget renders both its background and its fill rect.
// ---------------------------------------------------------------------------

TEST_CASE("screenshot: UI progress bars", "[screenshot][ui]")
{
    engine::screenshot::ScreenshotFixture fx;

    engine::ui::UiCanvas canvas(fx.width(), fx.height());
    engine::ui::UiRenderer renderer;
    renderer.init();

    auto makeBar = [&](float yTop, float fill)
    {
        auto* bar = canvas.createNode<engine::ui::UiProgressBar>("bar");
        bar->anchor = {{0.f, 0.f}, {0.f, 0.f}};
        bar->offsetMin = {20.f, yTop};
        bar->offsetMax = {300.f, yTop + 30.f};
        bar->value = fill;
        bar->bgColor = {0.15f, 0.15f, 0.2f, 1.f};
        bar->fillColor = {0.2f, 0.85f, 0.4f, 1.f};
        canvas.root()->addChild(bar);
    };

    makeBar(40.f, 0.0f);
    makeBar(90.f, 0.5f);
    makeBar(140.f, 1.0f);

    canvas.update();

    prepareUiView(fx);
    renderer.render(canvas.drawList(), engine::rendering::kViewGameUi, fx.width(), fx.height());

    auto pixels = fx.captureFrame();
    renderer.shutdown();

    REQUIRE(engine::screenshot::compareOrUpdateGolden("ui_progress_bars", pixels, fx.width(),
                                                      fx.height()));
}

// ---------------------------------------------------------------------------
// Button states (Normal / Hovered / Pressed). The widget exposes its draw
// state directly; no event dispatch needed.
// ---------------------------------------------------------------------------

TEST_CASE("screenshot: UI button states", "[screenshot][ui]")
{
    engine::screenshot::ScreenshotFixture fx;

    engine::ui::UiCanvas canvas(fx.width(), fx.height());
    engine::ui::UiRenderer renderer;
    renderer.init();

    enum DesiredState
    {
        Normal,
        Hovered,
        Pressed
    };

    auto makeButton = [&](float yTop, DesiredState state, const char* label)
    {
        auto* b = canvas.createNode<engine::ui::UiButton>("btn");
        b->anchor = {{0.f, 0.f}, {0.f, 0.f}};
        b->offsetMin = {40.f, yTop};
        b->offsetMax = {200.f, yTop + 36.f};
        b->normalColor = {0.3f, 0.4f, 0.7f, 1.f};
        b->hoverColor = {0.45f, 0.55f, 0.85f, 1.f};
        b->pressedColor = {0.2f, 0.3f, 0.5f, 1.f};
        b->cornerRadius = 6.f;
        b->label = label;

        // Drive UiButton::state_ via synthetic events since the field is
        // private. Normal = no event, Hovered = MouseEnter, Pressed =
        // MouseEnter then MouseDown.
        if (state == Hovered || state == Pressed)
        {
            engine::ui::UiEvent enter{};
            enter.type = engine::ui::UiEventType::MouseEnter;
            b->onEvent(enter);
        }
        if (state == Pressed)
        {
            engine::ui::UiEvent down{};
            down.type = engine::ui::UiEventType::MouseDown;
            down.button = 0;
            b->onEvent(down);
        }
        canvas.root()->addChild(b);
    };

    makeButton(30.f, Normal, "Normal");
    makeButton(80.f, Hovered, "Hovered");
    makeButton(130.f, Pressed, "Pressed");

    canvas.update();

    prepareUiView(fx);
    renderer.render(canvas.drawList(), engine::rendering::kViewGameUi, fx.width(), fx.height());

    auto pixels = fx.captureFrame();
    renderer.shutdown();

    REQUIRE(engine::screenshot::compareOrUpdateGolden("ui_button_states", pixels, fx.width(),
                                                      fx.height()));
}

// ---------------------------------------------------------------------------
// Composite HUD scene: a colored background panel plus a UiText label
// rendered through MSDF (or BitmapFont if MSDF assets are missing). This
// catches regressions in the joint rect+text submission path.
// ---------------------------------------------------------------------------

TEST_CASE("screenshot: UI composite HUD", "[screenshot][ui][text]")
{
    engine::screenshot::ScreenshotFixture fx;

    engine::ui::UiCanvas canvas(fx.width(), fx.height());
    engine::ui::UiRenderer renderer;
    renderer.init();

    // Background bar.
    auto* bar = canvas.createNode<engine::ui::UiPanel>("hudbar");
    bar->anchor = {{0.f, 0.f}, {1.f, 0.f}};
    bar->offsetMin = {0.f, 0.f};
    bar->offsetMax = {0.f, 64.f};
    bar->color = {0.05f, 0.08f, 0.15f, 1.f};
    canvas.root()->addChild(bar);

    // HP bar inside the strip.
    auto* hp = canvas.createNode<engine::ui::UiProgressBar>("hp");
    hp->anchor = {{0.f, 0.f}, {0.f, 0.f}};
    hp->offsetMin = {16.f, 16.f};
    hp->offsetMax = {120.f, 32.f};
    hp->value = 0.7f;
    hp->bgColor = {0.2f, 0.05f, 0.05f, 1.f};
    hp->fillColor = {0.85f, 0.2f, 0.2f, 1.f};
    canvas.root()->addChild(hp);

    // Title label — try MSDF first, fall back to bitmap.
    engine::ui::BitmapFont bitmap;
    engine::ui::MsdfFont msdf;
    engine::ui::IFont* font = nullptr;
    const std::string j = findRepoFile("assets/fonts/JetBrainsMono-msdf.json");
    const std::string p = findRepoFile("assets/fonts/JetBrainsMono-msdf.png");
    if (!j.empty() && !p.empty() && msdf.loadFromFile(j.c_str(), p.c_str()))
    {
        font = &msdf;
    }
    else
    {
        REQUIRE(bitmap.createDebugFont());
        font = &bitmap;
    }

    auto* title = canvas.createNode<engine::ui::UiText>("title");
    title->anchor = {{0.f, 0.f}, {0.f, 0.f}};
    title->offsetMin = {140.f, 14.f};
    title->offsetMax = {300.f, 38.f};
    title->text = "HUD READY";
    title->color = {1.f, 1.f, 1.f, 1.f};
    title->fontSize = 18.f;
    title->font = font;
    canvas.root()->addChild(title);

    canvas.update();

    prepareUiView(fx, 0x202028ff);
    renderer.render(canvas.drawList(), engine::rendering::kViewGameUi, fx.width(), fx.height());

    auto pixels = fx.captureFrame();
    renderer.shutdown();
    msdf.shutdown();
    bitmap.shutdown();

    REQUIRE(engine::screenshot::compareOrUpdateGolden("ui_composite_hud", pixels, fx.width(),
                                                      fx.height()));
}
