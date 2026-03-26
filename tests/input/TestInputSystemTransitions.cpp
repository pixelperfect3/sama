#include <catch2/catch_test_macros.hpp>

#include "engine/input/InputState.h"
#include "engine/input/InputSystem.h"
#include "tests/input/FakeInputBackend.h"

using namespace engine::input;
using namespace engine::input::test;

TEST_CASE("InputSystem: key press produces Pressed+Held on first frame", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.pressKey(Key::Space);
    sys.update(state);

    REQUIRE(state.isKeyPressed(Key::Space));
    REQUIRE(state.isKeyHeld(Key::Space));
    REQUIRE_FALSE(state.isKeyReleased(Key::Space));
}

TEST_CASE("InputSystem: key held over two frames — Pressed only on first", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    // Frame 1: press
    backend.pressKey(Key::W);
    sys.update(state);
    REQUIRE(state.isKeyPressed(Key::W));
    REQUIRE(state.isKeyHeld(Key::W));

    // Frame 2: still down, no new event
    sys.update(state);
    REQUIRE_FALSE(state.isKeyPressed(Key::W));
    REQUIRE(state.isKeyHeld(Key::W));
    REQUIRE_FALSE(state.isKeyReleased(Key::W));
}

TEST_CASE("InputSystem: key release produces Released, not Held", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    // Frame 1: press
    backend.pressKey(Key::Space);
    sys.update(state);

    // Frame 2: release
    backend.releaseKey(Key::Space);
    sys.update(state);

    REQUIRE(state.isKeyReleased(Key::Space));
    REQUIRE_FALSE(state.isKeyHeld(Key::Space));
    REQUIRE_FALSE(state.isKeyPressed(Key::Space));
}

TEST_CASE("InputSystem: key released and gone on frame after release", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.pressKey(Key::Enter);
    sys.update(state);

    backend.releaseKey(Key::Enter);
    sys.update(state);  // release frame

    sys.update(state);  // frame after release — all flags clear
    REQUIRE_FALSE(state.isKeyHeld(Key::Enter));
    REQUIRE_FALSE(state.isKeyPressed(Key::Enter));
    REQUIRE_FALSE(state.isKeyReleased(Key::Enter));
}

TEST_CASE("InputSystem: multiple keys independent", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.pressKey(Key::A);
    backend.pressKey(Key::D);
    sys.update(state);

    REQUIRE(state.isKeyHeld(Key::A));
    REQUIRE(state.isKeyHeld(Key::D));
    REQUIRE_FALSE(state.isKeyHeld(Key::W));

    backend.releaseKey(Key::A);
    sys.update(state);

    REQUIRE(state.isKeyReleased(Key::A));
    REQUIRE(state.isKeyHeld(Key::D));
}

TEST_CASE("InputSystem: mouse button transitions", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.pressMouseButton(MouseButton::Left);
    sys.update(state);
    REQUIRE(state.isMouseButtonPressed(MouseButton::Left));
    REQUIRE(state.isMouseButtonHeld(MouseButton::Left));

    sys.update(state);
    REQUIRE_FALSE(state.isMouseButtonPressed(MouseButton::Left));
    REQUIRE(state.isMouseButtonHeld(MouseButton::Left));

    backend.releaseMouseButton(MouseButton::Left);
    sys.update(state);
    REQUIRE(state.isMouseButtonReleased(MouseButton::Left));
    REQUIRE_FALSE(state.isMouseButtonHeld(MouseButton::Left));
}
