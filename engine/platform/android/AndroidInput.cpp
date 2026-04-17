#include "engine/platform/android/AndroidInput.h"

#include <android/input.h>

#include <algorithm>

#include "engine/platform/android/AndroidKeyMap.h"

namespace engine::platform
{

int32_t AndroidInput::handleInputEvent(AInputEvent* event, engine::input::InputState& state)
{
    int32_t type = AInputEvent_getType(event);
    switch (type)
    {
        case AINPUT_EVENT_TYPE_MOTION:
            return handleMotionEvent(event, state);
        case AINPUT_EVENT_TYPE_KEY:
            return handleKeyEvent(event, state);
        default:
            return 0;
    }
}

void AndroidInput::endFrame(engine::input::InputState& state)
{
    // Clear per-frame pressed/released flags for keys.
    for (auto& f : state.keyFlags_)
    {
        f &= ~(engine::input::InputState::kFlagPressed | engine::input::InputState::kFlagReleased);
    }

    // Clear per-frame pressed/released flags for mouse buttons.
    for (auto& f : state.mouseFlags_)
    {
        f &= ~(engine::input::InputState::kFlagPressed | engine::input::InputState::kFlagReleased);
    }

    // Clear mouse delta.
    state.mouseDeltaX_ = 0.0;
    state.mouseDeltaY_ = 0.0;

    // Remove ended touches, promote Began to Moved.
    state.touches_.erase(
        std::remove_if(state.touches_.begin(), state.touches_.end(),
                       [](const engine::input::TouchPoint& tp)
                       { return tp.phase == engine::input::TouchPoint::Phase::Ended; }),
        state.touches_.end());

    for (auto& tp : state.touches_)
    {
        if (tp.phase == engine::input::TouchPoint::Phase::Began)
        {
            tp.phase = engine::input::TouchPoint::Phase::Moved;
        }
    }
}

int32_t AndroidInput::handleMotionEvent(AInputEvent* event, engine::input::InputState& state)
{
    int32_t action = AMotionEvent_getAction(event);
    int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
    int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                           AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    size_t pointerCount = static_cast<size_t>(AMotionEvent_getPointerCount(event));

    switch (actionMasked)
    {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
        {
            int32_t id = AMotionEvent_getPointerId(event, pointerIndex);
            float x = AMotionEvent_getX(event, pointerIndex);
            float y = AMotionEvent_getY(event, pointerIndex);

            // Track active touch.
            ActiveTouch at;
            at.pointerId = id;
            at.x = x;
            at.y = y;
            at.active = true;
            activeTouches_.push_back(at);

            // Add to InputState touches.
            engine::input::TouchPoint tp;
            tp.id = static_cast<uint64_t>(id);
            tp.x = x;
            tp.y = y;
            tp.phase = engine::input::TouchPoint::Phase::Began;
            state.touches_.push_back(tp);

            // First touch maps to mouse.
            if (actionMasked == AMOTION_EVENT_ACTION_DOWN)
            {
                state.mouseX_ = static_cast<double>(x);
                state.mouseY_ = static_cast<double>(y);
                state.mouseDeltaX_ = 0.0;
                state.mouseDeltaY_ = 0.0;
                prevMouseX_ = static_cast<double>(x);
                prevMouseY_ = static_cast<double>(y);
                hasPrevMouse_ = true;

                auto idx = static_cast<size_t>(engine::input::MouseButton::Left);
                state.mouseFlags_[idx] |=
                    engine::input::InputState::kFlagPressed | engine::input::InputState::kFlagHeld;
            }
            return 1;
        }

        case AMOTION_EVENT_ACTION_MOVE:
        {
            // Update all pointers.
            for (size_t i = 0; i < pointerCount; ++i)
            {
                int32_t id = AMotionEvent_getPointerId(event, i);
                float x = AMotionEvent_getX(event, i);
                float y = AMotionEvent_getY(event, i);

                // Update active touch record.
                for (auto& at : activeTouches_)
                {
                    if (at.pointerId == id)
                    {
                        at.x = x;
                        at.y = y;
                        break;
                    }
                }

                // Update or add TouchPoint in InputState.
                bool found = false;
                for (auto& tp : state.touches_)
                {
                    if (tp.id == static_cast<uint64_t>(id))
                    {
                        tp.x = x;
                        tp.y = y;
                        tp.phase = engine::input::TouchPoint::Phase::Moved;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    engine::input::TouchPoint tp;
                    tp.id = static_cast<uint64_t>(id);
                    tp.x = x;
                    tp.y = y;
                    tp.phase = engine::input::TouchPoint::Phase::Moved;
                    state.touches_.push_back(tp);
                }
            }

            // First pointer drives mouse position.
            if (pointerCount > 0)
            {
                float x = AMotionEvent_getX(event, 0);
                float y = AMotionEvent_getY(event, 0);
                state.mouseX_ = static_cast<double>(x);
                state.mouseY_ = static_cast<double>(y);
                if (hasPrevMouse_)
                {
                    state.mouseDeltaX_ = static_cast<double>(x) - prevMouseX_;
                    state.mouseDeltaY_ = static_cast<double>(y) - prevMouseY_;
                }
                prevMouseX_ = static_cast<double>(x);
                prevMouseY_ = static_cast<double>(y);
                hasPrevMouse_ = true;
            }
            return 1;
        }

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        {
            int32_t id = AMotionEvent_getPointerId(event, pointerIndex);
            float x = AMotionEvent_getX(event, pointerIndex);
            float y = AMotionEvent_getY(event, pointerIndex);

            // Remove from active touches.
            activeTouches_.erase(
                std::remove_if(activeTouches_.begin(), activeTouches_.end(),
                               [id](const ActiveTouch& at) { return at.pointerId == id; }),
                activeTouches_.end());

            // Update TouchPoint phase to Ended.
            for (auto& tp : state.touches_)
            {
                if (tp.id == static_cast<uint64_t>(id))
                {
                    tp.x = x;
                    tp.y = y;
                    tp.phase = engine::input::TouchPoint::Phase::Ended;
                    break;
                }
            }

            // Last touch up releases mouse button.
            if (actionMasked == AMOTION_EVENT_ACTION_UP)
            {
                auto idx = static_cast<size_t>(engine::input::MouseButton::Left);
                state.mouseFlags_[idx] &= ~engine::input::InputState::kFlagHeld;
                state.mouseFlags_[idx] |= engine::input::InputState::kFlagReleased;
                hasPrevMouse_ = false;
            }
            return 1;
        }

        case AMOTION_EVENT_ACTION_CANCEL:
        {
            // Cancel all active touches.
            for (auto& tp : state.touches_)
            {
                tp.phase = engine::input::TouchPoint::Phase::Ended;
            }
            activeTouches_.clear();

            auto idx = static_cast<size_t>(engine::input::MouseButton::Left);
            state.mouseFlags_[idx] &= ~engine::input::InputState::kFlagHeld;
            state.mouseFlags_[idx] |= engine::input::InputState::kFlagReleased;
            hasPrevMouse_ = false;
            return 1;
        }

        default:
            return 0;
    }
}

int32_t AndroidInput::handleKeyEvent(AInputEvent* event, engine::input::InputState& state)
{
    int32_t keyCode = AKeyEvent_getKeyCode(event);
    int32_t action = AKeyEvent_getAction(event);

    auto key = engine::platform::mapAndroidKeyCode(keyCode);
    if (key == engine::input::Key::COUNT)
        return 0;  // unmapped key

    auto idx = static_cast<size_t>(key);

    if (action == AKEY_EVENT_ACTION_DOWN)
    {
        if (!(state.keyFlags_[idx] & engine::input::InputState::kFlagHeld))
        {
            state.keyFlags_[idx] |= engine::input::InputState::kFlagPressed;
        }
        state.keyFlags_[idx] |= engine::input::InputState::kFlagHeld;
    }
    else if (action == AKEY_EVENT_ACTION_UP)
    {
        state.keyFlags_[idx] &= ~engine::input::InputState::kFlagHeld;
        state.keyFlags_[idx] |= engine::input::InputState::kFlagReleased;
    }

    return 1;
}

}  // namespace engine::platform
