#include <catch2/catch_test_macros.hpp>

#include "engine/input/ActionMap.h"
#include "engine/input/InputState.h"
#include "engine/input/InputSystem.h"
#include "tests/input/FakeInputBackend.h"

using namespace engine::input;
using namespace engine::input::test;

TEST_CASE("Axis: neither key held → 0", "[input]")
{
    ActionMap map;
    map.bindAxis("horizontal", Key::A, Key::D);

    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;
    sys.update(state);

    REQUIRE(state.axisValue("horizontal", map) == 0.0f);
}

TEST_CASE("Axis: positive key held → +1", "[input]")
{
    ActionMap map;
    map.bindAxis("horizontal", Key::A, Key::D);

    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.pressKey(Key::D);
    sys.update(state);

    REQUIRE(state.axisValue("horizontal", map) == 1.0f);
}

TEST_CASE("Axis: negative key held → -1", "[input]")
{
    ActionMap map;
    map.bindAxis("horizontal", Key::A, Key::D);

    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.pressKey(Key::A);
    sys.update(state);

    REQUIRE(state.axisValue("horizontal", map) == -1.0f);
}

TEST_CASE("Axis: both keys held → 0 (cancel out)", "[input]")
{
    ActionMap map;
    map.bindAxis("horizontal", Key::A, Key::D);

    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.pressKey(Key::A);
    backend.pressKey(Key::D);
    sys.update(state);

    REQUIRE(state.axisValue("horizontal", map) == 0.0f);
}

TEST_CASE("Axis: unknown axis name → 0", "[input]")
{
    ActionMap map;
    InputState state;
    REQUIRE(state.axisValue("nonexistent", map) == 0.0f);
}

TEST_CASE("Axis: vertical axis with arrow keys", "[input]")
{
    ActionMap map;
    map.bindAxis("vertical", Key::Down, Key::Up);

    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.pressKey(Key::Up);
    sys.update(state);

    REQUIRE(state.axisValue("vertical", map) == 1.0f);

    backend.releaseKey(Key::Up);
    backend.pressKey(Key::Down);
    sys.update(state);

    REQUIRE(state.axisValue("vertical", map) == -1.0f);
}
