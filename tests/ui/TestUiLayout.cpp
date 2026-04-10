#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/UiCanvas.h"
#include "engine/ui/UiEvent.h"
#include "engine/ui/UiNode.h"
#include "engine/ui/widgets/UiButton.h"
#include "engine/ui/widgets/UiPanel.h"
#include "engine/ui/widgets/UiSlider.h"

using namespace engine::ui;

// ===========================================================================
// Layout tests
// ===========================================================================

TEST_CASE("Layout: child anchored (0,0)-(1,1) with inset offsets", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* child = canvas.createNode<UiPanel>("child");
    child->anchor.min = {0.f, 0.f};
    child->anchor.max = {1.f, 1.f};
    child->offsetMin = {10.f, 10.f};
    child->offsetMax = {-10.f, -10.f};
    canvas.root()->addChild(child);

    canvas.update();

    const auto& r = child->rect();
    REQUIRE(r.position.x == 10.f);
    REQUIRE(r.position.y == 10.f);
    REQUIRE(r.size.x == 780.f);
    REQUIRE(r.size.y == 580.f);
}

TEST_CASE("Layout: centered button anchor(0.5,0.5)-(0.5,0.5)", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* btn = canvas.createNode<UiButton>("btn");
    btn->anchor.min = {0.5f, 0.5f};
    btn->anchor.max = {0.5f, 0.5f};
    btn->offsetMin = {-50.f, -15.f};
    btn->offsetMax = {50.f, 15.f};
    canvas.root()->addChild(btn);

    canvas.update();

    const auto& r = btn->rect();
    REQUIRE(r.position.x == 350.f);
    REQUIRE(r.position.y == 285.f);
    REQUIRE(r.size.x == 100.f);
    REQUIRE(r.size.y == 30.f);
}

TEST_CASE("Layout: nested -- parent fills root, child fills parent minus offsets", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* parent = canvas.createNode<UiPanel>("parent");
    parent->anchor.min = {0.f, 0.f};
    parent->anchor.max = {1.f, 1.f};
    canvas.root()->addChild(parent);

    auto* child = canvas.createNode<UiPanel>("child");
    child->anchor.min = {0.f, 0.f};
    child->anchor.max = {1.f, 1.f};
    child->offsetMin = {5.f, 5.f};
    child->offsetMax = {-5.f, -5.f};
    parent->addChild(child);

    canvas.update();

    const auto& pr = parent->rect();
    REQUIRE(pr.position.x == 0.f);
    REQUIRE(pr.position.y == 0.f);
    REQUIRE(pr.size.x == 800.f);
    REQUIRE(pr.size.y == 600.f);

    const auto& cr = child->rect();
    REQUIRE(cr.position.x == 5.f);
    REQUIRE(cr.position.y == 5.f);
    REQUIRE(cr.size.x == 790.f);
    REQUIRE(cr.size.y == 590.f);
}

// ===========================================================================
// Event dispatch tests
// ===========================================================================

TEST_CASE("Event dispatch: click inside button fires onClick", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* btn = canvas.createNode<UiButton>("btn");
    btn->anchor.min = {0.f, 0.f};
    btn->anchor.max = {0.f, 0.f};
    btn->offsetMin = {100.f, 100.f};
    btn->offsetMax = {200.f, 140.f};
    canvas.root()->addChild(btn);

    canvas.update();

    bool clicked = false;
    btn->onClick = [&](UiNode& /*sender*/) { clicked = true; };

    UiEvent event{};
    event.type = UiEventType::MouseDown;
    event.position = {150.f, 120.f};
    event.button = 0;

    bool consumed = canvas.dispatchEvent(event);

    REQUIRE(clicked);
    REQUIRE(consumed);
}

TEST_CASE("Event dispatch: click outside button does NOT fire onClick", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* btn = canvas.createNode<UiButton>("btn");
    btn->anchor.min = {0.f, 0.f};
    btn->anchor.max = {0.f, 0.f};
    btn->offsetMin = {100.f, 100.f};
    btn->offsetMax = {200.f, 140.f};
    canvas.root()->addChild(btn);

    canvas.update();

    bool clicked = false;
    btn->onClick = [&](UiNode& /*sender*/) { clicked = true; };

    UiEvent event{};
    event.type = UiEventType::MouseDown;
    event.position = {50.f, 50.f};  // outside button bounds
    event.button = 0;

    bool consumed = canvas.dispatchEvent(event);

    REQUIRE_FALSE(clicked);
    REQUIRE_FALSE(consumed);
}

TEST_CASE("Event dispatch: overlapping nodes -- front node consumes event", "[ui]")
{
    UiCanvas canvas(800, 600);

    // Back node -- added first, renders behind.
    auto* back = canvas.createNode<UiButton>("back");
    back->anchor.min = {0.f, 0.f};
    back->anchor.max = {0.f, 0.f};
    back->offsetMin = {100.f, 100.f};
    back->offsetMax = {300.f, 200.f};
    canvas.root()->addChild(back);

    // Front node -- added second, renders on top.
    auto* front = canvas.createNode<UiButton>("front");
    front->anchor.min = {0.f, 0.f};
    front->anchor.max = {0.f, 0.f};
    front->offsetMin = {150.f, 120.f};
    front->offsetMax = {250.f, 180.f};
    canvas.root()->addChild(front);

    canvas.update();

    bool backClicked = false;
    bool frontClicked = false;
    back->onClick = [&](UiNode& /*sender*/) { backClicked = true; };
    front->onClick = [&](UiNode& /*sender*/) { frontClicked = true; };

    UiEvent event{};
    event.type = UiEventType::MouseDown;
    event.position = {200.f, 150.f};  // inside both nodes
    event.button = 0;

    bool consumed = canvas.dispatchEvent(event);

    REQUIRE(frontClicked);
    REQUIRE_FALSE(backClicked);
    REQUIRE(consumed);
}

TEST_CASE("Event dispatch: hidden node is not hit-testable", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* btn = canvas.createNode<UiButton>("btn");
    btn->anchor.min = {0.f, 0.f};
    btn->anchor.max = {0.f, 0.f};
    btn->offsetMin = {100.f, 100.f};
    btn->offsetMax = {200.f, 140.f};
    btn->visible = false;
    canvas.root()->addChild(btn);

    canvas.update();

    bool clicked = false;
    btn->onClick = [&](UiNode& /*sender*/) { clicked = true; };

    UiEvent event{};
    event.type = UiEventType::MouseDown;
    event.position = {150.f, 120.f};
    event.button = 0;

    bool consumed = canvas.dispatchEvent(event);

    REQUIRE_FALSE(clicked);
    REQUIRE_FALSE(consumed);
}

TEST_CASE("Event dispatch: non-interactable node is skipped", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* btn = canvas.createNode<UiButton>("btn");
    btn->anchor.min = {0.f, 0.f};
    btn->anchor.max = {0.f, 0.f};
    btn->offsetMin = {100.f, 100.f};
    btn->offsetMax = {200.f, 140.f};
    btn->interactable = false;
    canvas.root()->addChild(btn);

    canvas.update();

    bool clicked = false;
    btn->onClick = [&](UiNode& /*sender*/) { clicked = true; };

    UiEvent event{};
    event.type = UiEventType::MouseDown;
    event.position = {150.f, 120.f};
    event.button = 0;

    bool consumed = canvas.dispatchEvent(event);

    REQUIRE_FALSE(clicked);
    REQUIRE_FALSE(consumed);
}

TEST_CASE("UiSlider: mouse down sets value from position", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* slider = canvas.createNode<UiSlider>("slider");
    slider->anchor.min = {0.f, 0.f};
    slider->anchor.max = {0.f, 0.f};
    slider->offsetMin = {100.f, 100.f};
    slider->offsetMax = {300.f, 130.f};
    canvas.root()->addChild(slider);

    canvas.update();

    float reported = -1.f;
    slider->onValueChanged = [&](UiSlider& /*s*/, float v) { reported = v; };

    // Click at the midpoint (x=200) of a 200px-wide slider starting at x=100.
    UiEvent event{};
    event.type = UiEventType::MouseDown;
    event.position = {200.f, 115.f};
    event.button = 0;

    bool consumed = canvas.dispatchEvent(event);

    REQUIRE(consumed);
    REQUIRE(slider->value == Catch::Approx(0.5f));
    REQUIRE(reported == Catch::Approx(0.5f));
}
