# Input System Architecture

## Overview

The input system decouples raw platform events from game-facing state through
three layers: a **backend** seam, an **InputSystem** processor, and a
per-frame **InputState** snapshot.

```
╔══════════════════════════════════════════════════════════════════╗
║  Platform layer                                                  ║
║                                                                  ║
║  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             ║
║  │    GLFW     │  │   UIKit     │  │  Android    │             ║
║  │  callbacks  │  │  touches    │  │  NDK events │             ║
║  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘             ║
╚═════════╪════════════════╪════════════════╪════════════════════╝
          │                │                │
          ▼                ▼                ▼
  ┌───────────────┐ ┌─────────────┐ ┌──────────────────┐
  │GlfwInputBackend│ │IosInputBackend│ │AndroidInputBackend│
  └───────┬───────┘ └──────┬──────┘ └────────┬─────────┘
          │                │                  │
          └────────────────┼──────────────────┘
                           │  IInputBackend::collectEvents()
                           ▼
                   ┌──────────────┐
                   │  InputSystem │  (called once per frame)
                   │   ::update() │
                   └──────┬───────┘
                          │  fills
                          ▼
                   ┌──────────────┐
                   │  InputState  │  (per-frame snapshot)
                   └──────┬───────┘
                          │  queried by
                          ▼
                     Game / ECS systems
```

---

## Core Types

### `RawEvent` — the currency between backend and system

A tagged union covering all event types:

| Type              | Payload                          | Source               |
|-------------------|----------------------------------|----------------------|
| `KeyDown`/`KeyUp` | `Key` enum                       | Desktop keyboard     |
| `MouseButtonDown/Up` | `MouseButton` enum            | Desktop mouse        |
| `MouseMove`       | `(double x, double y)`           | Desktop mouse cursor |
| `TouchBegin`      | `(uint64_t id, float x, float y)`| Mobile / stylus      |
| `TouchMove`       | `(uint64_t id, float x, float y)`| Mobile / stylus      |
| `TouchEnd`        | `(uint64_t id, float x, float y)`| Mobile / stylus      |

Touch IDs are stable for the lifetime of one gesture:
- **iOS**: `[UITouch hash]` (NSUInteger → uint64_t)
- **Android**: `AMotionEvent_getPointerId()` (int32_t → uint64_t)
- **Tests**: any uint64_t supplied by `FakeInputBackend`

---

### `IInputBackend` — the platform seam

```cpp
class IInputBackend {
    virtual void collectEvents(std::vector<RawEvent>& out) = 0;
    virtual void mousePosition(double& x, double& y) const = 0;
};
```

`collectEvents` is called once per frame by `InputSystem::update()`. It
appends all buffered events and clears the backend's internal buffer.
On mobile, `mousePosition` returns the position of the most recent touch.

---

### `InputSystem` — event processor

Holds all cross-frame state needed to compute transitions:

- `prevKeyDown_[Key::COUNT]` — keyboard state from last frame
- `prevMouseDown_[MouseButton::COUNT]` — mouse button state from last frame
- `prevMouseX_/Y_` — cursor position from last frame
- `activeTouches_[]` — currently-held touches (id + position)

Each `update()` call:
1. Calls `backend_.collectEvents()` into an internal buffer
2. Applies key/mouse events to the current-frame tables
3. Applies touch events, merging with stationary active touches
4. Computes pressed/held/released flags for keys and mouse buttons
5. Writes the `InputState` snapshot

---

### `InputState` — per-frame consumer snapshot

| Query                              | Meaning                                     |
|------------------------------------|---------------------------------------------|
| `isKeyPressed(k)`                  | Transition: up→down this frame              |
| `isKeyHeld(k)`                     | Currently down (including press frame)      |
| `isKeyReleased(k)`                 | Transition: down→up this frame              |
| `isActionHeld(name, map)`          | Any key/button bound to `name` is held      |
| `isMouseButtonPressed(b)`          | Mouse button transition down                |
| `mouseX() / mouseY()`             | Absolute cursor position                    |
| `mouseDeltaX() / mouseDeltaY()`   | Cursor delta since last frame (0 on frame 1)|
| `axisValue(name, map)`             | −1 / 0 / +1 from two keys bound to an axis |
| `touches()`                        | All active + just-ended touches this frame  |
| `touchById(id)`                    | Find a specific touch by ID                 |

---

## Backends

### Desktop — `GlfwInputBackend`

```
GLFWwindow
  ├── glfwSetKeyCallback         →  onKey(glfwKey, action)
  ├── glfwSetMouseButtonCallback →  onMouseButton(button, action)
  └── glfwSetCursorPosCallback   →  onCursorPos(x, y)
                                       │
                               mutex_-protected
                                 writeBuffer_
                                       │
                           collectEvents() swaps out
```

All three callbacks are registered in the constructor. They run on the main
thread inside `glfwPollEvents()`, so the mutex protects the write buffer in
case `collectEvents` is called from a different thread.

`onKey`/`onMouseButton`/`onCursorPos` are public so white-box integration
tests can call them directly without needing real keyboard hardware.

**Key repeat** (`GLFW_REPEAT`) is intentionally ignored — the held-key model
already provides held state without repeat events.

### iOS — `IosInputBackend`

```
UIView (parent, passed in by caller)
  └── _IosInputView (transparent overlay, full-size subview)
        ├── touchesBegan:withEvent:   → onTouchBegin(id, x, y)
        ├── touchesMoved:withEvent:   → onTouchMove(id, x, y)
        └── touchesEnded/Cancelled   → onTouchEnd(id, x, y)
                                            │
                                    mutex_-protected
                                      writeBuffer_
```

`_IosInputView` is a UIView subclass defined in the `.mm` file. It holds a
raw pointer back to the `IosInputBackend` (the backend's lifetime must
encompass the view's).

The overlay view sets `multipleTouchEnabled = YES` and
`exclusiveTouch = NO` to capture all simultaneous finger contacts.

Touch ID is derived from `[UITouch hash]` (uint64_t). On iOS, `UITouch`
objects are reused across the gesture so the hash is stable.

### Android — `AndroidInputBackend`

```
AInputQueue (from android_app or GameActivity)
  └── AInputQueue_getEvent()
        └── backend.processEvent(event)
              ├── AINPUT_EVENT_TYPE_KEY   → key events via androidKeyCodeToKey()
              └── AINPUT_EVENT_TYPE_MOTION
                    ├── ACTION_DOWN / POINTER_DOWN  → TouchBegin per pointer
                    ├── ACTION_MOVE                 → TouchMove per pointer
                    └── ACTION_UP / POINTER_UP      → TouchEnd for pointer
                                                          │
                                                  mutex_-protected
                                                    writeBuffer_
```

`processEvent()` returns `true` if the event was consumed (so the caller can
pass that to `AInputQueue_finishEvent`).

Key mapping covers the standard `AKEYCODE_*` constants for letters, digits,
function keys, navigation, and modifiers. Unknown keycodes are silently
ignored.

### Test helpers

| Class                | Use                                             |
|----------------------|-------------------------------------------------|
| `NullInputBackend`   | Headless contexts; produces no events           |
| `FakeInputBackend`   | Unit tests; `pressKey()`, `touchBegin()`, etc.  |

---

## ActionMap — high-level bindings

`ActionMap` is a separate data object (not part of `InputState`) that maps:

- `Key` → action name (`string_view` into caller-owned literals)
- `MouseButton` → action name
- `(Key negative, Key positive)` → axis name

It is O(1) for key/button lookups (fixed-size array indexed by enum value)
and O(n\_axes) for axis lookups (tiny vector, typically < 10 axes).

`InputState::isActionHeld(name, map)` iterates all keys/buttons with that
action name — this is O(KEY::COUNT) but called at most a few times per
frame by game systems.

---

## Touch lifecycle across frames

```
Frame N:    TouchBegin(id=1)  → touches_ = [{id=1, Began}]
            activeTouches_    = [{id=1, x, y}]

Frame N+1:  (no event)        → touches_ = [{id=1, Moved}]  ← stationary
            activeTouches_    = [{id=1, x, y}]

Frame N+2:  TouchMove(id=1)   → touches_ = [{id=1, Moved}]
            activeTouches_    = [{id=1, x', y'}]

Frame N+3:  TouchEnd(id=1)    → touches_ = [{id=1, Ended}]
            activeTouches_    = []                            ← cleared

Frame N+4:  (no event)        → touches_ = []
```

---

## Cross-platform testing strategy

| Layer             | How to test                                                  |
|-------------------|--------------------------------------------------------------|
| `ActionMap`       | Pure C++ unit tests, no platform APIs                        |
| `InputState`      | Pure C++ unit tests, manipulate flags/touches directly       |
| `InputSystem`     | Unit tests via `FakeInputBackend`                            |
| `GlfwInputBackend`| `onKey`/`onCursorPos` called directly; hidden GLFW window   |
| `IosInputBackend` | `onTouchBegin` etc. called directly in XCTest               |
| `AndroidInputBackend` | `processEvent` called with synthetic `AInputEvent`       |
| Key mapping fns   | Standalone unit tests, no window/device needed               |

Unit tests (`FakeInputBackend` + `InputSystem`) run on every platform and CI
target including headless Android emulator / iOS simulator jobs. Platform-
specific integration tests use a hidden/off-screen window where applicable.
