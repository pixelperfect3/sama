#include "engine/input/InputSystem.h"

#include <algorithm>

namespace engine::input
{

InputSystem::InputSystem(IInputBackend& backend) : backend_(backend) {}

void InputSystem::update(InputState& state)
{
    // Clear per-frame state.
    state.keyFlags_.fill(0);
    state.mouseFlags_.fill(0);
    state.touches_.clear();

    // Collect raw events from the backend.
    eventBuf_.clear();
    backend_.collectEvents(eventBuf_);

    // Build current key-down table from events (on top of prev state).
    std::array<bool, InputState::kKeyCount> curKeyDown = prevKeyDown_;
    std::array<bool, InputState::kMouseButtonCount> curMouseDown = prevMouseDown_;

    double curMouseX = prevMouseX_;
    double curMouseY = prevMouseY_;

    // Touch: start with all active touches as stationary (Moved).
    // Events will override individual touches below.
    struct FrameTouch
    {
        uint64_t id;
        float x, y;
        TouchPoint::Phase phase;
        bool seenEvent;
    };
    std::vector<FrameTouch> frameTouches;
    frameTouches.reserve(activeTouches_.size() + 4);
    for (const auto& at : activeTouches_)
        frameTouches.push_back({at.id, at.x, at.y, TouchPoint::Phase::Moved, false});

    auto findFrameTouch = [&](uint64_t id) -> FrameTouch*
    {
        for (auto& ft : frameTouches)
            if (ft.id == id)
                return &ft;
        return nullptr;
    };

    for (const RawEvent& e : eventBuf_)
    {
        switch (e.type)
        {
            case RawEventType::KeyDown:
                if (e.key.key < Key::COUNT)
                    curKeyDown[static_cast<size_t>(e.key.key)] = true;
                break;
            case RawEventType::KeyUp:
                if (e.key.key < Key::COUNT)
                    curKeyDown[static_cast<size_t>(e.key.key)] = false;
                break;
            case RawEventType::MouseButtonDown:
                if (e.mouseButton.button < MouseButton::COUNT)
                    curMouseDown[static_cast<size_t>(e.mouseButton.button)] = true;
                break;
            case RawEventType::MouseButtonUp:
                if (e.mouseButton.button < MouseButton::COUNT)
                    curMouseDown[static_cast<size_t>(e.mouseButton.button)] = false;
                break;
            case RawEventType::MouseMove:
                curMouseX = e.cursor.x;
                curMouseY = e.cursor.y;
                break;

            case RawEventType::TouchBegin:
            {
                FrameTouch* ft = findFrameTouch(e.touch.id);
                if (!ft)
                {
                    frameTouches.push_back(
                        {e.touch.id, e.touch.x, e.touch.y, TouchPoint::Phase::Began, true});
                }
                else
                {
                    ft->x = e.touch.x;
                    ft->y = e.touch.y;
                    ft->phase = TouchPoint::Phase::Began;
                    ft->seenEvent = true;
                }
                break;
            }
            case RawEventType::TouchMove:
            {
                FrameTouch* ft = findFrameTouch(e.touch.id);
                if (!ft)
                {
                    frameTouches.push_back(
                        {e.touch.id, e.touch.x, e.touch.y, TouchPoint::Phase::Moved, true});
                }
                else
                {
                    ft->x = e.touch.x;
                    ft->y = e.touch.y;
                    ft->phase = TouchPoint::Phase::Moved;
                    ft->seenEvent = true;
                }
                break;
            }
            case RawEventType::TouchEnd:
            {
                FrameTouch* ft = findFrameTouch(e.touch.id);
                if (!ft)
                {
                    frameTouches.push_back(
                        {e.touch.id, e.touch.x, e.touch.y, TouchPoint::Phase::Ended, true});
                }
                else
                {
                    ft->x = e.touch.x;
                    ft->y = e.touch.y;
                    ft->phase = TouchPoint::Phase::Ended;
                    ft->seenEvent = true;
                }
                break;
            }
        }
    }

    // Compute key transitions and write flags.
    for (size_t i = 0; i < InputState::kKeyCount; ++i)
    {
        bool prev = prevKeyDown_[i];
        bool cur = curKeyDown[i];
        uint8_t flags = 0;
        if (cur)
            flags |= InputState::kFlagHeld;
        if (cur && !prev)
            flags |= InputState::kFlagPressed;
        if (!cur && prev)
            flags |= InputState::kFlagReleased;
        state.keyFlags_[i] = flags;
    }

    for (size_t i = 0; i < InputState::kMouseButtonCount; ++i)
    {
        bool prev = prevMouseDown_[i];
        bool cur = curMouseDown[i];
        uint8_t flags = 0;
        if (cur)
            flags |= InputState::kFlagHeld;
        if (cur && !prev)
            flags |= InputState::kFlagPressed;
        if (!cur && prev)
            flags |= InputState::kFlagReleased;
        state.mouseFlags_[i] = flags;
    }

    // Mouse position and delta.
    if (firstFrame_)
    {
        backend_.mousePosition(curMouseX, curMouseY);
        state.mouseDeltaX_ = 0.0;
        state.mouseDeltaY_ = 0.0;
        firstFrame_ = false;
    }
    else
    {
        state.mouseDeltaX_ = curMouseX - prevMouseX_;
        state.mouseDeltaY_ = curMouseY - prevMouseY_;
    }
    state.mouseX_ = curMouseX;
    state.mouseY_ = curMouseY;

    // Write touch snapshot and update activeTouches_.
    activeTouches_.clear();
    for (const auto& ft : frameTouches)
    {
        state.touches_.push_back({ft.id, ft.x, ft.y, ft.phase});
        if (ft.phase != TouchPoint::Phase::Ended)
            activeTouches_.push_back({ft.id, ft.x, ft.y});
    }

    // Carry keyboard/mouse state forward.
    prevKeyDown_ = curKeyDown;
    prevMouseDown_ = curMouseDown;
    prevMouseX_ = curMouseX;
    prevMouseY_ = curMouseY;
}

}  // namespace engine::input
