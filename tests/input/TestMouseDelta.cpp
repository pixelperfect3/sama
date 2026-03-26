#include <catch2/catch_test_macros.hpp>

#include "engine/input/InputState.h"
#include "engine/input/InputSystem.h"
#include "tests/input/FakeInputBackend.h"

using namespace engine::input;
using namespace engine::input::test;

TEST_CASE("Mouse: position is updated from MouseMove events", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.moveMouse(100.0, 200.0);
    sys.update(state);

    REQUIRE(state.mouseX() == 100.0);
    REQUIRE(state.mouseY() == 200.0);
}

TEST_CASE("Mouse: delta is zero on first frame", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.moveMouse(50.0, 75.0);
    sys.update(state);

    // First frame: delta must be zero regardless of position
    REQUIRE(state.mouseDeltaX() == 0.0);
    REQUIRE(state.mouseDeltaY() == 0.0);
}

TEST_CASE("Mouse: delta computed correctly on subsequent frames", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.moveMouse(10.0, 20.0);
    sys.update(state);  // first frame — delta = 0

    backend.moveMouse(15.0, 18.0);
    sys.update(state);

    REQUIRE(state.mouseDeltaX() == 5.0);
    REQUIRE(state.mouseDeltaY() == -2.0);
}

TEST_CASE("Mouse: no MouseMove event → delta is 0 on subsequent frame", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.moveMouse(30.0, 40.0);
    sys.update(state);  // first frame

    sys.update(state);  // no movement

    REQUIRE(state.mouseDeltaX() == 0.0);
    REQUIRE(state.mouseDeltaY() == 0.0);
    REQUIRE(state.mouseX() == 30.0);
    REQUIRE(state.mouseY() == 40.0);
}

TEST_CASE("Mouse: position accumulates across multiple move events in one frame", "[input]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    // First frame: set baseline
    backend.moveMouse(0.0, 0.0);
    sys.update(state);

    // Second frame: multiple move events — last one wins
    backend.push(RawEvent::mouseMove(10.0, 10.0));
    backend.push(RawEvent::mouseMove(20.0, 30.0));
    sys.update(state);

    REQUIRE(state.mouseX() == 20.0);
    REQUIRE(state.mouseY() == 30.0);
}
