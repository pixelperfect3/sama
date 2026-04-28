#pragma once

#include <TargetConditionals.h>

#include <cstdint>
#include <vector>

#include "engine/input/InputState.h"
#include "engine/input/Key.h"

namespace engine::platform::ios
{

// ---------------------------------------------------------------------------
// IosTouchInput — maps UITouch events to engine InputState.
//
// Mirrors AndroidInput on the Android side: a stateful helper that the
// platform layer feeds raw events into and that mutates an InputState in
// place.  Use it together with IosWindow:
//
//   IosTouchInput touch;
//   touch.attach(window.nativeView());
//
//   // each frame, after the OS has dispatched touch events:
//   touch.endFrame(state);
//
// Touch mapping rules — identical to AndroidInput so games written for
// Android work unchanged on iOS:
//
//   - The first touch also drives mouseX_ / mouseY_ and the left mouse
//     button, so desktop-oriented camera / UI code keeps working.
//   - All touches go into InputState::touches_ with stable IDs derived from
//     [UITouch hash].  Coordinates are in physical pixels (UITouch's
//     locationInView is in points, multiplied by view.contentScaleFactor).
// ---------------------------------------------------------------------------

class IosTouchInput
{
public:
    IosTouchInput();
    ~IosTouchInput();

    IosTouchInput(const IosTouchInput&) = delete;
    IosTouchInput& operator=(const IosTouchInput&) = delete;
    IosTouchInput(IosTouchInput&&) = delete;
    IosTouchInput& operator=(IosTouchInput&&) = delete;

    // Install a transparent UIView overlay on the supplied parent UIView*
    // (cast to void*).  The overlay receives touch callbacks and forwards
    // them into this object.  The parent view (and its window) must outlive
    // the IosTouchInput.
    //
    // Safe to call multiple times; the previous overlay is removed first.
    void attach(void* parentUiViewPtr);

    // Remove the overlay subview and stop receiving events.
    void detach();

    // Per-frame housekeeping: clears mouse delta, removes Ended touches,
    // promotes Began touches to Moved.  Mirrors AndroidInput::endFrame.
    void endFrame(engine::input::InputState& state);

    // Bind / unbind the InputState that incoming touch callbacks should
    // mutate.  Typically the engine's single InputState_.  Touches received
    // while no state is bound are dropped on the floor.
    void bindState(engine::input::InputState* state);

    // Internal entry points called by the ObjC overlay view.  Coordinates
    // are pre-converted to physical pixels (locationInView * contentScale).
    void onTouchBegin(uint64_t touchId, float x, float y);
    void onTouchMove(uint64_t touchId, float x, float y);
    void onTouchEnd(uint64_t touchId, float x, float y);
    void onTouchCancel(uint64_t touchId, float x, float y);

private:
    // Per-active-finger record.  Mirrors AndroidInput::ActiveTouch and lets
    // us reuse the "first finger == mouse" idiom across move events.
    struct ActiveTouch
    {
        uint64_t touchId = 0;
        float x = 0.0f;
        float y = 0.0f;
    };

    // Bookkeeping for the first-touch-as-mouse mapping.  prevMouse* drives
    // mouseDelta on subsequent move events, hasPrevMouse_ guards the very
    // first move.
    double prevMouseX_ = 0.0;
    double prevMouseY_ = 0.0;
    bool hasPrevMouse_ = false;

    // The id of the touch currently mapped to the mouse cursor.  Tracking it
    // explicitly (instead of "touch index 0") matches iOS semantics: touches
    // can begin / end in any order independent of slot index.
    uint64_t mouseTouchId_ = 0;
    bool mouseTouchActive_ = false;

    std::vector<ActiveTouch> activeTouches_;

    // Bound by the platform layer — never owned.  May be nullptr if the
    // overlay receives events before the engine has finished bootstrapping.
    engine::input::InputState* state_ = nullptr;

    // Held as void* for plain-C++ header compatibility; cast back to the ObjC
    // bridge view inside the .mm file.  Lifetime: ARC-owned via the parent
    // UIView's subviews array; we drop the back-pointer in detach().
    void* overlayView_ = nullptr;
};

}  // namespace engine::platform::ios
