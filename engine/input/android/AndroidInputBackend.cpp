#include "engine/input/android/AndroidInputBackend.h"

#if defined(__ANDROID__)
#include <android/input.h>
#include <android/keycodes.h>
#endif

namespace engine::input
{

// ---------------------------------------------------------------------------
// Key mapping
// ---------------------------------------------------------------------------

#if defined(__ANDROID__)
std::optional<Key> androidKeyCodeToKey(int32_t kc)
{
    // clang-format off
    switch (kc)
    {
        // Letters: AKEYCODE_A = 29 … AKEYCODE_Z = 54
        case AKEYCODE_A: return Key::A;
        case AKEYCODE_B: return Key::B;
        case AKEYCODE_C: return Key::C;
        case AKEYCODE_D: return Key::D;
        case AKEYCODE_E: return Key::E;
        case AKEYCODE_F: return Key::F;
        case AKEYCODE_G: return Key::G;
        case AKEYCODE_H: return Key::H;
        case AKEYCODE_I: return Key::I;
        case AKEYCODE_J: return Key::J;
        case AKEYCODE_K: return Key::K;
        case AKEYCODE_L: return Key::L;
        case AKEYCODE_M: return Key::M;
        case AKEYCODE_N: return Key::N;
        case AKEYCODE_O: return Key::O;
        case AKEYCODE_P: return Key::P;
        case AKEYCODE_Q: return Key::Q;
        case AKEYCODE_R: return Key::R;
        case AKEYCODE_S: return Key::S;
        case AKEYCODE_T: return Key::T;
        case AKEYCODE_U: return Key::U;
        case AKEYCODE_V: return Key::V;
        case AKEYCODE_W: return Key::W;
        case AKEYCODE_X: return Key::X;
        case AKEYCODE_Y: return Key::Y;
        case AKEYCODE_Z: return Key::Z;

        // Digits: AKEYCODE_0=7, AKEYCODE_1=8 … AKEYCODE_9=16
        case AKEYCODE_0: return Key::Num0;
        case AKEYCODE_1: return Key::Num1;
        case AKEYCODE_2: return Key::Num2;
        case AKEYCODE_3: return Key::Num3;
        case AKEYCODE_4: return Key::Num4;
        case AKEYCODE_5: return Key::Num5;
        case AKEYCODE_6: return Key::Num6;
        case AKEYCODE_7: return Key::Num7;
        case AKEYCODE_8: return Key::Num8;
        case AKEYCODE_9: return Key::Num9;

        // Function keys
        case AKEYCODE_F1:  return Key::F1;
        case AKEYCODE_F2:  return Key::F2;
        case AKEYCODE_F3:  return Key::F3;
        case AKEYCODE_F4:  return Key::F4;
        case AKEYCODE_F5:  return Key::F5;
        case AKEYCODE_F6:  return Key::F6;
        case AKEYCODE_F7:  return Key::F7;
        case AKEYCODE_F8:  return Key::F8;
        case AKEYCODE_F9:  return Key::F9;
        case AKEYCODE_F10: return Key::F10;
        case AKEYCODE_F11: return Key::F11;
        case AKEYCODE_F12: return Key::F12;

        // Control / navigation
        case AKEYCODE_SPACE:          return Key::Space;
        case AKEYCODE_ENTER:          return Key::Enter;
        case AKEYCODE_ESCAPE:         return Key::Escape;   // AKEYCODE_ESCAPE = 111
        case AKEYCODE_TAB:            return Key::Tab;
        case AKEYCODE_DEL:            return Key::Backspace;  // DEL = backspace on Android
        case AKEYCODE_FORWARD_DEL:    return Key::Delete;
        case AKEYCODE_INSERT:         return Key::Insert;
        case AKEYCODE_DPAD_LEFT:      return Key::Left;
        case AKEYCODE_DPAD_RIGHT:     return Key::Right;
        case AKEYCODE_DPAD_UP:        return Key::Up;
        case AKEYCODE_DPAD_DOWN:      return Key::Down;
        case AKEYCODE_MOVE_HOME:      return Key::Home;
        case AKEYCODE_MOVE_END:       return Key::End;
        case AKEYCODE_PAGE_UP:        return Key::PageUp;
        case AKEYCODE_PAGE_DOWN:      return Key::PageDown;

        // Modifier keys
        case AKEYCODE_SHIFT_LEFT:     return Key::LeftShift;
        case AKEYCODE_SHIFT_RIGHT:    return Key::RightShift;
        case AKEYCODE_CTRL_LEFT:      return Key::LeftCtrl;
        case AKEYCODE_CTRL_RIGHT:     return Key::RightCtrl;
        case AKEYCODE_ALT_LEFT:       return Key::LeftAlt;
        case AKEYCODE_ALT_RIGHT:      return Key::RightAlt;
        case AKEYCODE_META_LEFT:      return Key::LeftSuper;
        case AKEYCODE_META_RIGHT:     return Key::RightSuper;

        // Punctuation / symbols
        case AKEYCODE_APOSTROPHE:     return Key::Apostrophe;
        case AKEYCODE_COMMA:          return Key::Comma;
        case AKEYCODE_MINUS:          return Key::Minus;
        case AKEYCODE_PERIOD:         return Key::Period;
        case AKEYCODE_SLASH:          return Key::Slash;
        case AKEYCODE_SEMICOLON:      return Key::Semicolon;
        case AKEYCODE_EQUALS:         return Key::Equal;
        case AKEYCODE_LEFT_BRACKET:   return Key::LeftBracket;
        case AKEYCODE_BACKSLASH:      return Key::Backslash;
        case AKEYCODE_RIGHT_BRACKET:  return Key::RightBracket;
        case AKEYCODE_GRAVE:          return Key::GraveAccent;

        // Numpad
        case AKEYCODE_NUMPAD_0:       return Key::Numpad0;
        case AKEYCODE_NUMPAD_1:       return Key::Numpad1;
        case AKEYCODE_NUMPAD_2:       return Key::Numpad2;
        case AKEYCODE_NUMPAD_3:       return Key::Numpad3;
        case AKEYCODE_NUMPAD_4:       return Key::Numpad4;
        case AKEYCODE_NUMPAD_5:       return Key::Numpad5;
        case AKEYCODE_NUMPAD_6:       return Key::Numpad6;
        case AKEYCODE_NUMPAD_7:       return Key::Numpad7;
        case AKEYCODE_NUMPAD_8:       return Key::Numpad8;
        case AKEYCODE_NUMPAD_9:       return Key::Numpad9;
        case AKEYCODE_NUMPAD_DOT:     return Key::NumpadDecimal;
        case AKEYCODE_NUMPAD_DIVIDE:  return Key::NumpadDivide;
        case AKEYCODE_NUMPAD_MULTIPLY:return Key::NumpadMultiply;
        case AKEYCODE_NUMPAD_SUBTRACT:return Key::NumpadSubtract;
        case AKEYCODE_NUMPAD_ADD:     return Key::NumpadAdd;
        case AKEYCODE_NUMPAD_ENTER:   return Key::NumpadEnter;

        default: return std::nullopt;
    }
    // clang-format on
}
#endif  // __ANDROID__

// ---------------------------------------------------------------------------
// AndroidInputBackend
// ---------------------------------------------------------------------------

void AndroidInputBackend::collectEvents(std::vector<RawEvent>& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    out.insert(out.end(), writeBuffer_.begin(), writeBuffer_.end());
    writeBuffer_.clear();
}

void AndroidInputBackend::mousePosition(double& x, double& y) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    x = static_cast<double>(lastTouchX_);
    y = static_cast<double>(lastTouchY_);
}

#if defined(__ANDROID__)

bool AndroidInputBackend::processEvent(AInputEvent* event)
{
    const int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_KEY)
    {
        const int32_t keycode = AKeyEvent_getKeyCode(event);
        const int32_t action = AKeyEvent_getAction(event);

        auto key = androidKeyCodeToKey(keycode);
        if (!key)
            return false;

        std::lock_guard<std::mutex> lock(mutex_);
        if (action == AKEY_EVENT_ACTION_DOWN)
            writeBuffer_.push_back(RawEvent::keyDown(*key));
        else if (action == AKEY_EVENT_ACTION_UP)
            writeBuffer_.push_back(RawEvent::keyUp(*key));

        return true;
    }

    if (type == AINPUT_EVENT_TYPE_MOTION)
    {
        const int32_t action = AMotionEvent_getAction(event);
        const int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
        const int32_t ptrIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                 AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        std::lock_guard<std::mutex> lock(mutex_);

        if (actionMasked == AMOTION_EVENT_ACTION_DOWN ||
            actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN)
        {
            const auto id = static_cast<uint64_t>(AMotionEvent_getPointerId(event, ptrIndex));
            const float x = AMotionEvent_getX(event, ptrIndex);
            const float y = AMotionEvent_getY(event, ptrIndex);
            lastTouchX_ = x;
            lastTouchY_ = y;
            writeBuffer_.push_back(RawEvent::touchBegin(id, x, y));
            return true;
        }

        if (actionMasked == AMOTION_EVENT_ACTION_MOVE)
        {
            const size_t count = static_cast<size_t>(AMotionEvent_getPointerCount(event));
            for (size_t i = 0; i < count; ++i)
            {
                const auto id = static_cast<uint64_t>(AMotionEvent_getPointerId(event, i));
                const float x = AMotionEvent_getX(event, i);
                const float y = AMotionEvent_getY(event, i);
                lastTouchX_ = x;
                lastTouchY_ = y;
                writeBuffer_.push_back(RawEvent::touchMove(id, x, y));
            }
            return true;
        }

        if (actionMasked == AMOTION_EVENT_ACTION_UP ||
            actionMasked == AMOTION_EVENT_ACTION_POINTER_UP ||
            actionMasked == AMOTION_EVENT_ACTION_CANCEL)
        {
            const auto id = static_cast<uint64_t>(AMotionEvent_getPointerId(event, ptrIndex));
            const float x = AMotionEvent_getX(event, ptrIndex);
            const float y = AMotionEvent_getY(event, ptrIndex);
            writeBuffer_.push_back(RawEvent::touchEnd(id, x, y));
            return true;
        }
    }

    return false;
}

#endif  // __ANDROID__

}  // namespace engine::input
