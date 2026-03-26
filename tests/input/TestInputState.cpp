#include <catch2/catch_test_macros.hpp>

#include "engine/input/ActionMap.h"
#include "engine/input/InputState.h"
#include "engine/input/Key.h"

using namespace engine::input;

// Helper: set a key as held (simulates what InputSystem writes).
static void setHeld(InputState& s, Key k)
{
    s.keyFlags_[static_cast<size_t>(k)] = InputState::kFlagHeld;
}
static void setPressed(InputState& s, Key k)
{
    s.keyFlags_[static_cast<size_t>(k)] = InputState::kFlagPressed | InputState::kFlagHeld;
}
static void setReleased(InputState& s, Key k)
{
    s.keyFlags_[static_cast<size_t>(k)] = InputState::kFlagReleased;
}

TEST_CASE("InputState: fresh state has no keys held", "[input]")
{
    InputState s;
    REQUIRE_FALSE(s.isKeyHeld(Key::Space));
    REQUIRE_FALSE(s.isKeyPressed(Key::Space));
    REQUIRE_FALSE(s.isKeyReleased(Key::Space));
}

TEST_CASE("InputState: held flag — isHeld true, isPressed false", "[input]")
{
    InputState s;
    setHeld(s, Key::W);
    REQUIRE(s.isKeyHeld(Key::W));
    REQUIRE_FALSE(s.isKeyPressed(Key::W));
    REQUIRE_FALSE(s.isKeyReleased(Key::W));
}

TEST_CASE("InputState: pressed frame — isPressed and isHeld both true", "[input]")
{
    InputState s;
    setPressed(s, Key::Space);
    REQUIRE(s.isKeyPressed(Key::Space));
    REQUIRE(s.isKeyHeld(Key::Space));
    REQUIRE_FALSE(s.isKeyReleased(Key::Space));
}

TEST_CASE("InputState: release frame — isReleased true, isHeld false", "[input]")
{
    InputState s;
    setReleased(s, Key::Escape);
    REQUIRE(s.isKeyReleased(Key::Escape));
    REQUIRE_FALSE(s.isKeyHeld(Key::Escape));
    REQUIRE_FALSE(s.isKeyPressed(Key::Escape));
}

TEST_CASE("InputState: isActionHeld respects key binding", "[input]")
{
    ActionMap map;
    map.bindKey(Key::Space, "jump");

    InputState s;
    setHeld(s, Key::Space);
    REQUIRE(s.isActionHeld("jump", map));
    REQUIRE_FALSE(s.isActionHeld("fire", map));
}

TEST_CASE("InputState: isActionPressed triggers when bound key is pressed", "[input]")
{
    ActionMap map;
    map.bindKey(Key::Space, "jump");

    InputState s;
    setPressed(s, Key::Space);
    REQUIRE(s.isActionPressed("jump", map));
    REQUIRE_FALSE(s.isActionPressed("fire", map));
}

TEST_CASE("InputState: isActionReleased triggers when bound key is released", "[input]")
{
    ActionMap map;
    map.bindKey(Key::Space, "jump");

    InputState s;
    setReleased(s, Key::Space);
    REQUIRE(s.isActionReleased("jump", map));
}

TEST_CASE("InputState: mouse button flags", "[input]")
{
    InputState s;
    s.mouseFlags_[static_cast<size_t>(MouseButton::Left)] =
        InputState::kFlagPressed | InputState::kFlagHeld;

    REQUIRE(s.isMouseButtonPressed(MouseButton::Left));
    REQUIRE(s.isMouseButtonHeld(MouseButton::Left));
    REQUIRE_FALSE(s.isMouseButtonReleased(MouseButton::Left));
}
