#include <catch2/catch_test_macros.hpp>

#include "engine/ui/UiCanvas.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiNode.h"
#include "engine/ui/widgets/UiButton.h"
#include "engine/ui/widgets/UiImage.h"
#include "engine/ui/widgets/UiPanel.h"
#include "engine/ui/widgets/UiProgressBar.h"
#include "engine/ui/widgets/UiSlider.h"
#include "engine/ui/widgets/UiText.h"

using namespace engine::ui;

// ---------------------------------------------------------------------------
// UiCanvas: tree structure
// ---------------------------------------------------------------------------

TEST_CASE("UiCanvas: create and verify root", "[ui]")
{
    UiCanvas canvas(1280, 720);
    REQUIRE(canvas.root() != nullptr);
    REQUIRE(canvas.root()->parent() == nullptr);
    REQUIRE(canvas.root()->children().empty());
}

TEST_CASE("UiCanvas: add child, verify tree structure", "[ui]")
{
    UiCanvas canvas(1280, 720);
    auto* panel = canvas.createNode<UiPanel>("myPanel");

    REQUIRE(panel != nullptr);
    REQUIRE(panel->id() != 0);
    REQUIRE(std::string(panel->name()) == "myPanel");
    REQUIRE(panel->parent() == nullptr);

    canvas.root()->addChild(panel);
    REQUIRE(panel->parent() == canvas.root());
    REQUIRE(canvas.root()->children().size() == 1);
    REQUIRE(canvas.root()->children()[0] == panel);
}

TEST_CASE("UiCanvas: remove child, verify parent/children pointers", "[ui]")
{
    UiCanvas canvas(1280, 720);
    auto* panel = canvas.createNode<UiPanel>("panel");
    canvas.root()->addChild(panel);

    REQUIRE(canvas.root()->children().size() == 1);

    canvas.root()->removeChild(panel->id());
    REQUIRE(canvas.root()->children().empty());
    REQUIRE(panel->parent() == nullptr);
}

TEST_CASE("UiCanvas: multiple children", "[ui]")
{
    UiCanvas canvas(1280, 720);
    auto* a = canvas.createNode<UiPanel>("a");
    auto* b = canvas.createNode<UiPanel>("b");
    auto* c = canvas.createNode<UiPanel>("c");

    canvas.root()->addChild(a);
    canvas.root()->addChild(b);
    canvas.root()->addChild(c);

    REQUIRE(canvas.root()->children().size() == 3);
    REQUIRE(canvas.root()->children()[0] == a);
    REQUIRE(canvas.root()->children()[1] == b);
    REQUIRE(canvas.root()->children()[2] == c);
}

TEST_CASE("UiCanvas: destroyNode removes from tree", "[ui]")
{
    UiCanvas canvas(1280, 720);
    auto* panel = canvas.createNode<UiPanel>("panel");
    canvas.root()->addChild(panel);

    REQUIRE(canvas.root()->children().size() == 1);
    canvas.destroyNode(panel);
    REQUIRE(canvas.root()->children().empty());
}

// ---------------------------------------------------------------------------
// Widget construction
// ---------------------------------------------------------------------------

TEST_CASE("UiPanel: constructs with defaults", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* panel = canvas.createNode<UiPanel>("bg");

    REQUIRE(panel != nullptr);
    REQUIRE(panel->cornerRadius == 0.f);
    REQUIRE(panel->color.w == 1.0f);
    REQUIRE(panel->visible == true);
}

TEST_CASE("UiButton: constructs with defaults", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* btn = canvas.createNode<UiButton>("btn");

    REQUIRE(btn != nullptr);
    REQUIRE(btn->label.empty());
    REQUIRE(btn->fontSize == 16.f);
    REQUIRE(btn->cornerRadius == 4.f);
}

TEST_CASE("UiText: constructs with defaults", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* txt = canvas.createNode<UiText>("txt");

    REQUIRE(txt != nullptr);
    REQUIRE(txt->text.empty());
    REQUIRE(txt->fontSize == 16.f);
    REQUIRE(txt->align == TextAlign::Left);
}

TEST_CASE("UiImage: constructs with defaults", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* img = canvas.createNode<UiImage>("img");

    REQUIRE(img != nullptr);
    REQUIRE(!bgfx::isValid(img->texture));
    REQUIRE(img->preserveAspect == false);
}

TEST_CASE("UiSlider: constructs with defaults", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* slider = canvas.createNode<UiSlider>("slider");

    REQUIRE(slider != nullptr);
    REQUIRE(slider->value == 0.f);
    REQUIRE(slider->trackHeight == 4.f);
    REQUIRE(slider->thumbSize == 16.f);
}

TEST_CASE("UiProgressBar: constructs with defaults", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* bar = canvas.createNode<UiProgressBar>("bar");

    REQUIRE(bar != nullptr);
    REQUIRE(bar->value == 0.f);
}

// ---------------------------------------------------------------------------
// UiDrawList
// ---------------------------------------------------------------------------

TEST_CASE("UiDrawList: drawRect adds a command", "[ui]")
{
    UiDrawList drawList;
    drawList.drawRect({10.f, 20.f}, {100.f, 50.f}, {1.f, 0.f, 0.f, 1.f});

    REQUIRE(drawList.commands().size() == 1);
    REQUIRE(drawList.commands()[0].type == UiDrawCmd::Rect);
    REQUIRE(drawList.commands()[0].position.x == 10.f);
    REQUIRE(drawList.commands()[0].size.x == 100.f);
    REQUIRE(drawList.commands()[0].color.x == 1.f);
}

TEST_CASE("UiDrawList: drawText adds a command", "[ui]")
{
    UiDrawList drawList;
    drawList.drawText({5.f, 5.f}, "hello", {1.f, 1.f, 1.f, 1.f});

    REQUIRE(drawList.commands().size() == 1);
    REQUIRE(drawList.commands()[0].type == UiDrawCmd::Text);
    REQUIRE(drawList.commands()[0].text != nullptr);
}

TEST_CASE("UiDrawList: clear empties it", "[ui]")
{
    UiDrawList drawList;
    drawList.drawRect({0.f, 0.f}, {10.f, 10.f}, {1.f, 1.f, 1.f, 1.f});
    drawList.drawRect({0.f, 0.f}, {20.f, 20.f}, {0.f, 0.f, 0.f, 1.f});
    REQUIRE(drawList.commands().size() == 2);

    drawList.clear();
    REQUIRE(drawList.commands().empty());
}

// ---------------------------------------------------------------------------
// Canvas update: draw list generation
// ---------------------------------------------------------------------------

TEST_CASE("Canvas update generates draw commands from visible nodes", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* panel = canvas.createNode<UiPanel>("panel");
    panel->anchor.min = {0.f, 0.f};
    panel->anchor.max = {1.f, 1.f};
    canvas.root()->addChild(panel);

    canvas.update();

    // Panel should produce at least one draw command.
    REQUIRE(!canvas.drawList().commands().empty());
}

TEST_CASE("Hidden nodes (visible=false) produce no draw commands", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* panel = canvas.createNode<UiPanel>("hidden");
    panel->visible = false;
    panel->anchor.min = {0.f, 0.f};
    panel->anchor.max = {1.f, 1.f};
    canvas.root()->addChild(panel);

    canvas.update();

    // Root draws nothing (transparent container), hidden panel skipped.
    // Only the root's onDraw fires (which emits nothing).
    REQUIRE(canvas.drawList().commands().empty());
}

TEST_CASE("Nested nodes: parent with 3 children, draw order", "[ui]")
{
    UiCanvas canvas(800, 600);

    auto* parent = canvas.createNode<UiPanel>("parent");
    parent->anchor.min = {0.f, 0.f};
    parent->anchor.max = {1.f, 1.f};
    canvas.root()->addChild(parent);

    auto* child1 = canvas.createNode<UiPanel>("child1");
    child1->anchor.min = {0.f, 0.f};
    child1->anchor.max = {0.33f, 1.f};

    auto* child2 = canvas.createNode<UiPanel>("child2");
    child2->anchor.min = {0.33f, 0.f};
    child2->anchor.max = {0.66f, 1.f};

    auto* child3 = canvas.createNode<UiPanel>("child3");
    child3->anchor.min = {0.66f, 0.f};
    child3->anchor.max = {1.f, 1.f};

    parent->addChild(child1);
    parent->addChild(child2);
    parent->addChild(child3);

    canvas.update();

    // Parent draws first, then children in order.
    // Each UiPanel emits 1 draw command (no border).
    // Total: parent + child1 + child2 + child3 = 4 commands.
    REQUIRE(canvas.drawList().commands().size() == 4);
}

// ---------------------------------------------------------------------------
// Layout computation
// ---------------------------------------------------------------------------

TEST_CASE("Layout: root fills screen", "[ui]")
{
    UiCanvas canvas(1280, 720);
    canvas.update();

    const auto& r = canvas.root()->rect();
    REQUIRE(r.position.x == 0.f);
    REQUIRE(r.position.y == 0.f);
    REQUIRE(r.size.x == 1280.f);
    REQUIRE(r.size.y == 720.f);
}

TEST_CASE("Layout: centered button via anchors and offsets", "[ui]")
{
    UiCanvas canvas(1280, 720);
    auto* btn = canvas.createNode<UiButton>("btn");
    btn->anchor.min = {0.5f, 0.5f};
    btn->anchor.max = {0.5f, 0.5f};
    btn->offsetMin = {-100.f, -25.f};
    btn->offsetMax = {100.f, 25.f};
    canvas.root()->addChild(btn);

    canvas.update();

    const auto& r = btn->rect();
    // Position: (640 - 100, 360 - 25) = (540, 335)
    REQUIRE(r.position.x == 540.f);
    REQUIRE(r.position.y == 335.f);
    // Size: 200 x 50
    REQUIRE(r.size.x == 200.f);
    REQUIRE(r.size.y == 50.f);
}

TEST_CASE("Layout: full-width panel with margins", "[ui]")
{
    UiCanvas canvas(1280, 720);
    auto* panel = canvas.createNode<UiPanel>("wide");
    panel->anchor.min = {0.f, 0.f};
    panel->anchor.max = {1.f, 0.f};
    panel->offsetMin = {10.f, 10.f};
    panel->offsetMax = {-10.f, 60.f};
    canvas.root()->addChild(panel);

    canvas.update();

    const auto& r = panel->rect();
    REQUIRE(r.position.x == 10.f);
    REQUIRE(r.position.y == 10.f);
    // Width: (1280 - 10) - 10 = 1260
    REQUIRE(r.size.x == 1260.f);
    // Height: 60 - 10 = 50
    REQUIRE(r.size.y == 50.f);
}

TEST_CASE("Canvas setScreenSize marks layout dirty", "[ui]")
{
    UiCanvas canvas(800, 600);
    auto* panel = canvas.createNode<UiPanel>("panel");
    panel->anchor.min = {0.f, 0.f};
    panel->anchor.max = {1.f, 1.f};
    canvas.root()->addChild(panel);

    canvas.update();
    REQUIRE(panel->rect().size.x == 800.f);

    canvas.setScreenSize(1920, 1080);
    canvas.update();
    REQUIRE(panel->rect().size.x == 1920.f);
    REQUIRE(panel->rect().size.y == 1080.f);
}
