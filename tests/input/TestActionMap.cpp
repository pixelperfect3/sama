#include <catch2/catch_test_macros.hpp>

#include "engine/input/ActionMap.h"
#include "engine/input/Key.h"

using namespace engine::input;

TEST_CASE("ActionMap: unbound key returns empty string", "[input]")
{
    ActionMap map;
    REQUIRE(map.keyAction(Key::Space) == "");
}

TEST_CASE("ActionMap: bindKey / keyAction round-trip", "[input]")
{
    ActionMap map;
    map.bindKey(Key::Space, "jump");
    REQUIRE(map.keyAction(Key::Space) == "jump");
}

TEST_CASE("ActionMap: rebinding a key overwrites the old binding", "[input]")
{
    ActionMap map;
    map.bindKey(Key::W, "move_forward");
    map.bindKey(Key::W, "sprint");
    REQUIRE(map.keyAction(Key::W) == "sprint");
}

TEST_CASE("ActionMap: multiple keys can share the same action", "[input]")
{
    ActionMap map;
    map.bindKey(Key::W, "move_forward");
    map.bindKey(Key::Up, "move_forward");
    REQUIRE(map.keyAction(Key::W) == "move_forward");
    REQUIRE(map.keyAction(Key::Up) == "move_forward");
}

TEST_CASE("ActionMap: unbound mouse button returns empty string", "[input]")
{
    ActionMap map;
    REQUIRE(map.mouseButtonAction(MouseButton::Left) == "");
}

TEST_CASE("ActionMap: bindMouseButton / mouseButtonAction round-trip", "[input]")
{
    ActionMap map;
    map.bindMouseButton(MouseButton::Left, "fire");
    REQUIRE(map.mouseButtonAction(MouseButton::Left) == "fire");
    REQUIRE(map.mouseButtonAction(MouseButton::Right) == "");
    REQUIRE(map.mouseButtonAction(MouseButton::Middle) == "");
}

TEST_CASE("ActionMap: bindAxis / axisBinding round-trip", "[input]")
{
    ActionMap map;
    map.bindAxis("horizontal", Key::A, Key::D);

    const AxisBinding* b = map.axisBinding("horizontal");
    REQUIRE(b != nullptr);
    REQUIRE(b->negative == Key::A);
    REQUIRE(b->positive == Key::D);
    REQUIRE(b->name == "horizontal");
}

TEST_CASE("ActionMap: axisBinding returns nullptr for unknown axis", "[input]")
{
    ActionMap map;
    REQUIRE(map.axisBinding("nonexistent") == nullptr);
}

TEST_CASE("ActionMap: rebinding an axis updates the existing entry", "[input]")
{
    ActionMap map;
    map.bindAxis("vertical", Key::S, Key::W);
    map.bindAxis("vertical", Key::Down, Key::Up);

    const AxisBinding* b = map.axisBinding("vertical");
    REQUIRE(b != nullptr);
    REQUIRE(b->negative == Key::Down);
    REQUIRE(b->positive == Key::Up);
}
