#include <catch2/catch_test_macros.hpp>

#include "engine/input/InputState.h"

// ---------------------------------------------------------------------------
// AndroidInput design tests.
//
// The AndroidInput class depends on AInputEvent and other Android NDK types
// that are not available on desktop.  These tests document the expected
// behavior contract without calling actual Android APIs.
// ---------------------------------------------------------------------------

using engine::input::InputState;
using engine::input::MouseButton;
using engine::input::TouchPoint;

TEST_CASE("AndroidInput design: single touch maps to mouse + left button", "[android][design]")
{
    // When a single finger touches the screen:
    //   - mouseX_ / mouseY_ are set to the touch position
    //   - left mouse button transitions to Pressed + Held
    //   - a TouchPoint with Phase::Began is added to touches_

    InputState state;
    // Simulate what AndroidInput::handleMotionEvent does on ACTION_DOWN:
    state.mouseX_ = 500.0;
    state.mouseY_ = 300.0;
    auto idx = static_cast<size_t>(MouseButton::Left);
    state.mouseFlags_[idx] |= InputState::kFlagPressed | InputState::kFlagHeld;

    TouchPoint tp;
    tp.id = 0;
    tp.x = 500.0f;
    tp.y = 300.0f;
    tp.phase = TouchPoint::Phase::Began;
    state.touches_.push_back(tp);

    CHECK(state.isMouseButtonPressed(MouseButton::Left));
    CHECK(state.isMouseButtonHeld(MouseButton::Left));
    CHECK(state.mouseX() == 500.0);
    CHECK(state.mouseY() == 300.0);
    REQUIRE(state.touches().size() == 1);
    CHECK(state.touches()[0].phase == TouchPoint::Phase::Began);
}

TEST_CASE("AndroidInput design: multi-touch uses stable IDs", "[android][design]")
{
    // When multiple fingers are down:
    //   - each touch gets a unique id (from Android's pointerId)
    //   - only the first touch drives mouseX_/mouseY_
    //   - all touches appear in the touches_ vector

    InputState state;
    state.touches_.push_back({0, 100.0f, 200.0f, TouchPoint::Phase::Moved});
    state.touches_.push_back({1, 400.0f, 500.0f, TouchPoint::Phase::Began});

    REQUIRE(state.touches().size() == 2);
    CHECK(state.touches()[0].id == 0);
    CHECK(state.touches()[1].id == 1);

    // touchById returns the correct touch.
    auto* t = state.touchById(1);
    REQUIRE(t != nullptr);
    CHECK(t->x == 400.0f);
}

TEST_CASE("AndroidInput design: touch up sets Phase::Ended and releases mouse", "[android][design]")
{
    // When the last finger lifts:
    //   - the TouchPoint phase is set to Ended
    //   - left mouse button transitions to Released (Held cleared)

    InputState state;
    state.touches_.push_back({0, 100.0f, 200.0f, TouchPoint::Phase::Ended});

    auto idx = static_cast<size_t>(MouseButton::Left);
    state.mouseFlags_[idx] = InputState::kFlagReleased;

    CHECK(state.isMouseButtonReleased(MouseButton::Left));
    CHECK_FALSE(state.isMouseButtonHeld(MouseButton::Left));
    CHECK(state.touches()[0].phase == TouchPoint::Phase::Ended);
}

TEST_CASE("AndroidInput design: endFrame clears per-frame state", "[android][design]")
{
    // After endFrame:
    //   - Pressed/Released key flags are cleared, Held remains
    //   - Mouse pressed/released flags are cleared, Held remains
    //   - Ended touches are removed from the vector
    //   - Began touches are promoted to Moved
    //   - Mouse delta is zeroed

    InputState state;

    // Set up a key that was pressed this frame and is held.
    auto keyIdx = static_cast<size_t>(engine::input::Key::A);
    state.keyFlags_[keyIdx] = InputState::kFlagPressed | InputState::kFlagHeld;

    // Set up mouse delta.
    state.mouseDeltaX_ = 10.0;
    state.mouseDeltaY_ = -5.0;

    // Set up touches: one Began, one Ended.
    state.touches_.push_back({0, 1.0f, 2.0f, TouchPoint::Phase::Began});
    state.touches_.push_back({1, 3.0f, 4.0f, TouchPoint::Phase::Ended});

    // Simulate endFrame behavior:
    // Clear per-frame flags.
    state.keyFlags_[keyIdx] &= ~(InputState::kFlagPressed | InputState::kFlagReleased);
    state.mouseDeltaX_ = 0.0;
    state.mouseDeltaY_ = 0.0;

    // Remove ended touches.
    state.touches_.erase(
        std::remove_if(state.touches_.begin(), state.touches_.end(),
                       [](const TouchPoint& t) { return t.phase == TouchPoint::Phase::Ended; }),
        state.touches_.end());
    // Promote Began to Moved.
    for (auto& t : state.touches_)
    {
        if (t.phase == TouchPoint::Phase::Began)
            t.phase = TouchPoint::Phase::Moved;
    }

    // Verify.
    CHECK(state.isKeyHeld(engine::input::Key::A));
    CHECK_FALSE(state.isKeyPressed(engine::input::Key::A));
    CHECK(state.mouseDeltaX() == 0.0);
    CHECK(state.mouseDeltaY() == 0.0);
    REQUIRE(state.touches().size() == 1);
    CHECK(state.touches()[0].id == 0);
    CHECK(state.touches()[0].phase == TouchPoint::Phase::Moved);
}
