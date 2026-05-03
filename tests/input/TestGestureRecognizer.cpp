#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/input/GestureRecognizer.h"
#include "engine/input/InputState.h"

using Catch::Matchers::WithinAbs;
using engine::input::GestureRecognizer;
using engine::input::GestureState;
using engine::input::InputState;
using engine::input::TouchPoint;

namespace
{

constexpr float kEps = 1e-4f;

// Helper that fakes the InputState the way InputSystem would have populated
// it.  GestureRecognizer reads only `touches()`, so this is enough.
TouchPoint touch(uint64_t id, float x, float y, TouchPoint::Phase phase = TouchPoint::Phase::Moved)
{
    TouchPoint p;
    p.id = id;
    p.x = x;
    p.y = y;
    p.phase = phase;
    return p;
}

}  // namespace

TEST_CASE("GestureRecognizer: no touches yields zero deltas", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    rec.update(state, gesture);

    CHECK(gesture.active == false);
    CHECK(gesture.pinchDelta == 0.f);
    CHECK(gesture.panDeltaX == 0.f);
    CHECK(gesture.panDeltaY == 0.f);
}

TEST_CASE("GestureRecognizer: single touch is not a gesture", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    state.touches_ = {touch(1, 100.f, 100.f)};
    rec.update(state, gesture);

    CHECK_FALSE(gesture.active);
    CHECK(gesture.pinchDelta == 0.f);
    CHECK(gesture.panDeltaX == 0.f);
    CHECK(gesture.panDeltaY == 0.f);
}

TEST_CASE("GestureRecognizer: 0 -> 1 -> 2 touches: deltas are zero on the first two-touch frame",
          "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    // Frame 0 — no touches
    rec.update(state, gesture);
    CHECK_FALSE(gesture.active);

    // Frame 1 — one touch
    state.touches_ = {touch(1, 100.f, 100.f)};
    rec.update(state, gesture);
    CHECK_FALSE(gesture.active);

    // Frame 2 — two touches arrive.  Tracking starts, no delta yet.
    state.touches_ = {touch(1, 100.f, 100.f), touch(2, 200.f, 100.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    CHECK_THAT(gesture.pinchDelta, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaX, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaY, WithinAbs(0.f, kEps));
}

TEST_CASE("GestureRecognizer: pinch out (zoom in)", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    // Anchor frame
    state.touches_ = {touch(1, 100.f, 100.f), touch(2, 200.f, 100.f)};
    rec.update(state, gesture);
    REQUIRE(gesture.active);
    REQUIRE_THAT(gesture.pinchDelta, WithinAbs(0.f, kEps));

    // Fingers spread apart by 50px total (25 each side)
    state.touches_ = {touch(1, 75.f, 100.f), touch(2, 225.f, 100.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    CHECK_THAT(gesture.pinchDelta, WithinAbs(50.f, kEps));
    // Midpoint stayed at x=150,y=100 → no pan
    CHECK_THAT(gesture.panDeltaX, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaY, WithinAbs(0.f, kEps));
}

TEST_CASE("GestureRecognizer: pinch in (zoom out)", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    state.touches_ = {touch(1, 0.f, 0.f), touch(2, 100.f, 0.f)};
    rec.update(state, gesture);
    REQUIRE(gesture.active);

    state.touches_ = {touch(1, 30.f, 0.f), touch(2, 70.f, 0.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    CHECK_THAT(gesture.pinchDelta, WithinAbs(-60.f, kEps));
}

TEST_CASE("GestureRecognizer: pure two-finger pan", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    state.touches_ = {touch(1, 100.f, 100.f), touch(2, 200.f, 100.f)};
    rec.update(state, gesture);

    // Both fingers translate +30 X, +20 Y — distance unchanged → no pinch.
    state.touches_ = {touch(1, 130.f, 120.f), touch(2, 230.f, 120.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    CHECK_THAT(gesture.pinchDelta, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaX, WithinAbs(30.f, kEps));
    CHECK_THAT(gesture.panDeltaY, WithinAbs(20.f, kEps));
}

TEST_CASE("GestureRecognizer: simultaneous pinch and pan", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    // Anchor — fingers at (0,0) and (100,0); midpoint (50,0); distance 100.
    state.touches_ = {touch(1, 0.f, 0.f), touch(2, 100.f, 0.f)};
    rec.update(state, gesture);

    // Now fingers at (40,10) and (200,10):
    //   distance = 160 → pinch +60
    //   midpoint = (120, 10) → pan +(70, 10)
    state.touches_ = {touch(1, 40.f, 10.f), touch(2, 200.f, 10.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    CHECK_THAT(gesture.pinchDelta, WithinAbs(60.f, kEps));
    CHECK_THAT(gesture.panDeltaX, WithinAbs(70.f, kEps));
    CHECK_THAT(gesture.panDeltaY, WithinAbs(10.f, kEps));
}

TEST_CASE("GestureRecognizer: 2 -> 1 transition resets tracking", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    state.touches_ = {touch(1, 100.f, 100.f), touch(2, 200.f, 100.f)};
    rec.update(state, gesture);
    state.touches_ = {touch(1, 90.f, 100.f), touch(2, 210.f, 100.f)};
    rec.update(state, gesture);
    REQUIRE(gesture.active);

    // One finger lifts.
    state.touches_ = {touch(1, 90.f, 100.f)};
    rec.update(state, gesture);
    CHECK_FALSE(gesture.active);
    CHECK(gesture.pinchDelta == 0.f);
    CHECK(gesture.panDeltaX == 0.f);
    CHECK(gesture.panDeltaY == 0.f);

    // Two fingers come back at completely different positions — should NOT
    // emit the giant delta from the previous gesture; treat as a fresh
    // anchor.
    state.touches_ = {touch(3, 500.f, 500.f), touch(4, 700.f, 500.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    CHECK_THAT(gesture.pinchDelta, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaX, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaY, WithinAbs(0.f, kEps));
}

TEST_CASE("GestureRecognizer: tracked-id swap re-anchors without spike", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    // Anchor on (1,2)
    state.touches_ = {touch(1, 0.f, 0.f), touch(2, 100.f, 0.f)};
    rec.update(state, gesture);
    state.touches_ = {touch(1, 10.f, 0.f), touch(2, 110.f, 0.f)};
    rec.update(state, gesture);
    REQUIRE(gesture.active);
    REQUIRE_THAT(gesture.panDeltaX, WithinAbs(10.f, kEps));

    // Touch 2 ends, touch 3 takes over — far away, would otherwise spike.
    state.touches_ = {touch(1, 10.f, 0.f), touch(3, 900.f, 900.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    // Re-anchored — no delta this frame.
    CHECK_THAT(gesture.pinchDelta, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaX, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaY, WithinAbs(0.f, kEps));

    // Subsequent frame should now produce a delta against the new anchor.
    state.touches_ = {touch(1, 20.f, 0.f), touch(3, 910.f, 900.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    CHECK_THAT(gesture.panDeltaX, WithinAbs(10.f, kEps));
    CHECK_THAT(gesture.panDeltaY, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.pinchDelta, WithinAbs(0.f, kEps));
}

TEST_CASE("GestureRecognizer: third finger does not perturb tracked pair", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    state.touches_ = {touch(1, 0.f, 0.f), touch(2, 100.f, 0.f)};
    rec.update(state, gesture);
    REQUIRE(gesture.active);

    // Third finger appears — but recognizer should still track the two
    // lowest-id touches (1 and 2) so the gesture is stable.
    state.touches_ = {touch(1, 10.f, 0.f), touch(2, 110.f, 0.f), touch(3, 500.f, 500.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    CHECK_THAT(gesture.panDeltaX, WithinAbs(10.f, kEps));
    CHECK_THAT(gesture.panDeltaY, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.pinchDelta, WithinAbs(0.f, kEps));
}

TEST_CASE("GestureRecognizer: ended-phase touches are excluded from pair selection",
          "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    state.touches_ = {touch(1, 0.f, 0.f), touch(2, 100.f, 0.f)};
    rec.update(state, gesture);

    // Touch 2 is in Ended phase this frame — recognizer should treat it as
    // gone, drop tracking (only 1 active), and emit zeros + active=false.
    state.touches_ = {touch(1, 10.f, 0.f), touch(2, 110.f, 0.f, TouchPoint::Phase::Ended)};
    rec.update(state, gesture);
    CHECK_FALSE(gesture.active);
    CHECK(gesture.pinchDelta == 0.f);
}

TEST_CASE("GestureRecognizer: reset() clears tracking", "[input][gesture]")
{
    GestureRecognizer rec;
    InputState state;
    GestureState gesture;

    state.touches_ = {touch(1, 0.f, 0.f), touch(2, 100.f, 0.f)};
    rec.update(state, gesture);
    state.touches_ = {touch(1, 10.f, 0.f), touch(2, 110.f, 0.f)};
    rec.update(state, gesture);
    REQUIRE_THAT(gesture.panDeltaX, WithinAbs(10.f, kEps));

    rec.reset();

    // Same input state — should look like first-time tracking again
    // (re-anchor, no delta).
    state.touches_ = {touch(1, 50.f, 0.f), touch(2, 150.f, 0.f)};
    rec.update(state, gesture);
    CHECK(gesture.active);
    CHECK_THAT(gesture.pinchDelta, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaX, WithinAbs(0.f, kEps));
    CHECK_THAT(gesture.panDeltaY, WithinAbs(0.f, kEps));
}
