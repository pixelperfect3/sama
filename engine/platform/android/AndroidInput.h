#pragma once

#include <android/input.h>

#include <cstdint>
#include <vector>

#include "engine/input/InputState.h"
#include "engine/input/Key.h"

namespace engine::platform
{

// ---------------------------------------------------------------------------
// AndroidInput — maps AInputEvent touch and key events to engine InputState.
//
// Touch mapping:
//   - First touch also drives mouseX_/mouseY_ and left mouse button for
//     compatibility with desktop-oriented camera and UI code.
//   - All touches are recorded in InputState::touches_ with stable IDs.
//
// Key mapping:
//   - Common AKEYCODE_* values are mapped to engine::input::Key.
// ---------------------------------------------------------------------------

class AndroidInput
{
public:
    /// Process an AInputEvent and update InputState.
    /// Returns 1 if handled, 0 if not.
    int32_t handleInputEvent(AInputEvent* event, engine::input::InputState& state);

    /// Call at end of frame to clear per-frame state (pressed/released flags,
    /// ended touches).
    void endFrame(engine::input::InputState& state);

private:
    struct ActiveTouch
    {
        int32_t pointerId = -1;
        float x = 0.0f;
        float y = 0.0f;
        bool active = false;
    };
    std::vector<ActiveTouch> activeTouches_;

    // Previous mouse position for delta computation.
    double prevMouseX_ = 0.0;
    double prevMouseY_ = 0.0;
    bool hasPrevMouse_ = false;

    int32_t handleMotionEvent(AInputEvent* event, engine::input::InputState& state);
    int32_t handleKeyEvent(AInputEvent* event, engine::input::InputState& state);

    /// Map AKEYCODE_* to engine::input::Key.  Returns Key::COUNT if unmapped.
    static engine::input::Key mapKeyCode(int32_t keyCode);
};

}  // namespace engine::platform
