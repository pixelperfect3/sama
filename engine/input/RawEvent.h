#pragma once

#include "engine/input/Key.h"

namespace engine::input
{

enum class RawEventType : uint8_t
{
    KeyDown,
    KeyUp,
    MouseButtonDown,
    MouseButtonUp,
    MouseMove,
    TouchBegin,
    TouchMove,
    TouchEnd,
};

struct KeyEvent
{
    Key key;
};

struct MouseButtonEvent
{
    MouseButton button;
};

struct MouseMoveEvent
{
    double x;
    double y;
};

// Touch/pointer event.  id is stable for the lifetime of a single touch gesture.
// iOS: [UITouch hash]; Android: AMotionEvent_getPointerId(); tests: any uint64_t.
struct TouchEvent
{
    uint64_t id;
    float x;
    float y;
};

// Tagged union covering all input events produced by a backend.
struct RawEvent
{
    RawEventType type;
    union
    {
        KeyEvent key;
        MouseButtonEvent mouseButton;
        MouseMoveEvent cursor;
        TouchEvent touch;
    };

    static RawEvent keyDown(Key k)
    {
        RawEvent e;
        e.type = RawEventType::KeyDown;
        e.key = {k};
        return e;
    }
    static RawEvent keyUp(Key k)
    {
        RawEvent e;
        e.type = RawEventType::KeyUp;
        e.key = {k};
        return e;
    }
    static RawEvent mouseButtonDown(MouseButton b)
    {
        RawEvent e;
        e.type = RawEventType::MouseButtonDown;
        e.mouseButton = {b};
        return e;
    }
    static RawEvent mouseButtonUp(MouseButton b)
    {
        RawEvent e;
        e.type = RawEventType::MouseButtonUp;
        e.mouseButton = {b};
        return e;
    }
    static RawEvent mouseMove(double x, double y)
    {
        RawEvent e;
        e.type = RawEventType::MouseMove;
        e.cursor = {x, y};
        return e;
    }
    static RawEvent touchBegin(uint64_t id, float x, float y)
    {
        RawEvent e;
        e.type = RawEventType::TouchBegin;
        e.touch = {id, x, y};
        return e;
    }
    static RawEvent touchMove(uint64_t id, float x, float y)
    {
        RawEvent e;
        e.type = RawEventType::TouchMove;
        e.touch = {id, x, y};
        return e;
    }
    static RawEvent touchEnd(uint64_t id, float x, float y)
    {
        RawEvent e;
        e.type = RawEventType::TouchEnd;
        e.touch = {id, x, y};
        return e;
    }
};

}  // namespace engine::input
