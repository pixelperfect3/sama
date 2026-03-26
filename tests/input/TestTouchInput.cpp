#include <catch2/catch_test_macros.hpp>

#include "engine/input/InputState.h"
#include "engine/input/InputSystem.h"
#include "tests/input/FakeInputBackend.h"

using namespace engine::input;
using namespace engine::input::test;

TEST_CASE("Touch: no events → empty touch list", "[input][touch]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;
    sys.update(state);
    REQUIRE(state.touches().empty());
}

TEST_CASE("Touch: TouchBegin produces a Began touch", "[input][touch]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.touchBegin(1, 100.f, 200.f);
    sys.update(state);

    REQUIRE(state.touches().size() == 1);
    REQUIRE(state.touches()[0].id == 1);
    REQUIRE(state.touches()[0].x == 100.f);
    REQUIRE(state.touches()[0].y == 200.f);
    REQUIRE(state.touches()[0].phase == TouchPoint::Phase::Began);
}

TEST_CASE("Touch: stationary contact emits Moved phase on subsequent frames", "[input][touch]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.touchBegin(1, 50.f, 60.f);
    sys.update(state);

    // No new event — should appear as Moved (stationary)
    sys.update(state);

    REQUIRE(state.touches().size() == 1);
    REQUIRE(state.touches()[0].phase == TouchPoint::Phase::Moved);
    REQUIRE(state.touches()[0].x == 50.f);
    REQUIRE(state.touches()[0].y == 60.f);
}

TEST_CASE("Touch: TouchMove updates position and phase", "[input][touch]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.touchBegin(1, 10.f, 10.f);
    sys.update(state);

    backend.touchMove(1, 30.f, 40.f);
    sys.update(state);

    REQUIRE(state.touches().size() == 1);
    REQUIRE(state.touches()[0].phase == TouchPoint::Phase::Moved);
    REQUIRE(state.touches()[0].x == 30.f);
    REQUIRE(state.touches()[0].y == 40.f);
}

TEST_CASE("Touch: TouchEnd produces Ended phase and removes from active list", "[input][touch]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.touchBegin(1, 0.f, 0.f);
    sys.update(state);

    backend.touchEnd(1, 0.f, 0.f);
    sys.update(state);

    REQUIRE(state.touches().size() == 1);
    REQUIRE(state.touches()[0].phase == TouchPoint::Phase::Ended);

    // Next frame: ended touch is gone
    sys.update(state);
    REQUIRE(state.touches().empty());
}

TEST_CASE("Touch: multiple simultaneous contacts", "[input][touch]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.touchBegin(1, 10.f, 10.f);
    backend.touchBegin(2, 50.f, 50.f);
    sys.update(state);

    REQUIRE(state.touches().size() == 2);

    const TouchPoint* t1 = state.touchById(1);
    const TouchPoint* t2 = state.touchById(2);
    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);
    REQUIRE(t1->phase == TouchPoint::Phase::Began);
    REQUIRE(t2->phase == TouchPoint::Phase::Began);
}

TEST_CASE("Touch: releasing one finger does not affect others", "[input][touch]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.touchBegin(1, 10.f, 10.f);
    backend.touchBegin(2, 50.f, 50.f);
    sys.update(state);

    backend.touchEnd(1, 10.f, 10.f);
    sys.update(state);

    // Touch 1 is Ended, touch 2 is Moved (stationary)
    const TouchPoint* t1 = state.touchById(1);
    const TouchPoint* t2 = state.touchById(2);
    REQUIRE(t1 != nullptr);
    REQUIRE(t1->phase == TouchPoint::Phase::Ended);
    REQUIRE(t2 != nullptr);
    REQUIRE(t2->phase == TouchPoint::Phase::Moved);

    // Frame after: touch 1 gone, touch 2 still active
    sys.update(state);
    REQUIRE(state.touchById(1) == nullptr);
    REQUIRE(state.touchById(2) != nullptr);
}

TEST_CASE("Touch: touchById returns nullptr for absent id", "[input][touch]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;
    sys.update(state);
    REQUIRE(state.touchById(999) == nullptr);
}

TEST_CASE("Touch: touch and key events coexist in same frame", "[input][touch]")
{
    FakeInputBackend backend;
    InputSystem sys(backend);
    InputState state;

    backend.touchBegin(1, 5.f, 5.f);
    backend.pressKey(Key::Space);
    sys.update(state);

    REQUIRE(state.touches().size() == 1);
    REQUIRE(state.isKeyPressed(Key::Space));
}
