// UI panel screenshot test.
// Scene: orthographic 320x240 canvas with two colored UiPanel widgets
// (red and blue), rendered via the retained-mode UiRenderer (Phase 3).

#include <catch2/catch_test_macros.hpp>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#include "engine/ui/UiCanvas.h"
#include "engine/ui/UiRenderer.h"
#include "engine/ui/widgets/UiPanel.h"

TEST_CASE("screenshot: UI panel rendering", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;

    engine::ui::UiCanvas canvas(fx.width(), fx.height());
    engine::ui::UiRenderer renderer;
    renderer.init();

    // Create two colored panels.
    auto* red = canvas.createNode<engine::ui::UiPanel>("red");
    red->anchor = {{0, 0}, {0, 0}};
    red->offsetMin = {20, 20};
    red->offsetMax = {70, 70};
    red->color = {1, 0, 0, 1};

    auto* blue = canvas.createNode<engine::ui::UiPanel>("blue");
    blue->anchor = {{0, 0}, {0, 0}};
    blue->offsetMin = {100, 100};
    blue->offsetMax = {200, 130};
    blue->color = {0, 0, 1, 1};

    canvas.root()->addChild(red);
    canvas.root()->addChild(blue);
    canvas.update();

    // Set up the view.
    const auto viewId = engine::rendering::kViewGameUi;
    engine::rendering::RenderPass(viewId)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x202020ff);

    renderer.render(canvas.drawList(), viewId, fx.width(), fx.height());

    auto pixels = fx.captureFrame();
    renderer.shutdown();

    REQUIRE(
        engine::screenshot::compareOrUpdateGolden("ui_panels", pixels, fx.width(), fx.height()));
}
