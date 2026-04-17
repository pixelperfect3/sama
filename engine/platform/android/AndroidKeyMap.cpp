#include "engine/platform/android/AndroidKeyMap.h"

// AKEYCODE constants — defined locally so this file compiles on all platforms
// without requiring <android/keycodes.h>. Values match the Android NDK.
namespace
{
constexpr int32_t kAkeycodeA = 29;
constexpr int32_t kAkeycodeZ = 54;
constexpr int32_t kAkeycode0 = 7;
constexpr int32_t kAkeycode9 = 16;
constexpr int32_t kAkeycodeSpace = 62;
constexpr int32_t kAkeycodeEnter = 66;
constexpr int32_t kAkeycodeEscape = 111;
constexpr int32_t kAkeycodeBack = 4;
constexpr int32_t kAkeycodeDpadUp = 19;
constexpr int32_t kAkeycodeDpadDown = 20;
constexpr int32_t kAkeycodeDpadLeft = 21;
constexpr int32_t kAkeycodeDpadRight = 22;
constexpr int32_t kAkeycodeTab = 61;
constexpr int32_t kAkeycodeDel = 67;          // Backspace
constexpr int32_t kAkeycodeForwardDel = 112;  // Delete
constexpr int32_t kAkeycodeShiftLeft = 59;
constexpr int32_t kAkeycodeShiftRight = 60;
constexpr int32_t kAkeycodeCtrlLeft = 113;
constexpr int32_t kAkeycodeCtrlRight = 114;
constexpr int32_t kAkeycodeAltLeft = 57;
constexpr int32_t kAkeycodeAltRight = 58;
constexpr int32_t kAkeycodeMoveHome = 122;
constexpr int32_t kAkeycodeMoveEnd = 123;
constexpr int32_t kAkeycodePageUp = 92;
constexpr int32_t kAkeycodePageDown = 93;
constexpr int32_t kAkeycodeInsert = 124;
constexpr int32_t kAkeycodeComma = 55;
constexpr int32_t kAkeycodePeriod = 56;
constexpr int32_t kAkeycodeMinus = 69;
constexpr int32_t kAkeycodeEquals = 70;
constexpr int32_t kAkeycodeLeftBracket = 71;
constexpr int32_t kAkeycodeRightBracket = 72;
constexpr int32_t kAkeycodeBackslash = 73;
constexpr int32_t kAkeycodeSemicolon = 74;
constexpr int32_t kAkeycodeApostrophe = 75;
constexpr int32_t kAkeycodeSlash = 76;
constexpr int32_t kAkeycodeGrave = 68;
constexpr int32_t kAkeycodeF1 = 131;
constexpr int32_t kAkeycodeF12 = 142;
constexpr int32_t kAkeycodeNumpad0 = 144;
constexpr int32_t kAkeycodeNumpad9 = 153;
constexpr int32_t kAkeycodeNumpadDot = 158;
constexpr int32_t kAkeycodeNumpadDivide = 154;
constexpr int32_t kAkeycodeNumpadMultiply = 155;
constexpr int32_t kAkeycodeNumpadSubtract = 156;
constexpr int32_t kAkeycodeNumpadAdd = 157;
constexpr int32_t kAkeycodeNumpadEnter = 160;
}  // namespace

namespace engine::platform
{

engine::input::Key mapAndroidKeyCode(int32_t akeycode)
{
    using Key = engine::input::Key;

    // Letters A-Z
    if (akeycode >= kAkeycodeA && akeycode <= kAkeycodeZ)
    {
        return static_cast<Key>(static_cast<int>(Key::A) + (akeycode - kAkeycodeA));
    }

    // Digits 0-9
    if (akeycode >= kAkeycode0 && akeycode <= kAkeycode9)
    {
        return static_cast<Key>(static_cast<int>(Key::Num0) + (akeycode - kAkeycode0));
    }

    // Function keys F1-F12
    if (akeycode >= kAkeycodeF1 && akeycode <= kAkeycodeF12)
    {
        return static_cast<Key>(static_cast<int>(Key::F1) + (akeycode - kAkeycodeF1));
    }

    // Numpad 0-9
    if (akeycode >= kAkeycodeNumpad0 && akeycode <= kAkeycodeNumpad9)
    {
        return static_cast<Key>(static_cast<int>(Key::Numpad0) + (akeycode - kAkeycodeNumpad0));
    }

    // Individual key mappings
    switch (akeycode)
    {
        case kAkeycodeSpace:
            return Key::Space;
        case kAkeycodeEnter:
            return Key::Enter;
        case kAkeycodeEscape:
            return Key::Escape;
        case kAkeycodeBack:
            return Key::Escape;
        case kAkeycodeTab:
            return Key::Tab;
        case kAkeycodeDel:
            return Key::Backspace;
        case kAkeycodeForwardDel:
            return Key::Delete;
        case kAkeycodeInsert:
            return Key::Insert;
        case kAkeycodeDpadUp:
            return Key::Up;
        case kAkeycodeDpadDown:
            return Key::Down;
        case kAkeycodeDpadLeft:
            return Key::Left;
        case kAkeycodeDpadRight:
            return Key::Right;
        case kAkeycodeMoveHome:
            return Key::Home;
        case kAkeycodeMoveEnd:
            return Key::End;
        case kAkeycodePageUp:
            return Key::PageUp;
        case kAkeycodePageDown:
            return Key::PageDown;
        case kAkeycodeShiftLeft:
            return Key::LeftShift;
        case kAkeycodeShiftRight:
            return Key::RightShift;
        case kAkeycodeCtrlLeft:
            return Key::LeftCtrl;
        case kAkeycodeCtrlRight:
            return Key::RightCtrl;
        case kAkeycodeAltLeft:
            return Key::LeftAlt;
        case kAkeycodeAltRight:
            return Key::RightAlt;
        case kAkeycodeApostrophe:
            return Key::Apostrophe;
        case kAkeycodeComma:
            return Key::Comma;
        case kAkeycodeMinus:
            return Key::Minus;
        case kAkeycodePeriod:
            return Key::Period;
        case kAkeycodeSlash:
            return Key::Slash;
        case kAkeycodeSemicolon:
            return Key::Semicolon;
        case kAkeycodeEquals:
            return Key::Equal;
        case kAkeycodeLeftBracket:
            return Key::LeftBracket;
        case kAkeycodeBackslash:
            return Key::Backslash;
        case kAkeycodeRightBracket:
            return Key::RightBracket;
        case kAkeycodeGrave:
            return Key::GraveAccent;
        case kAkeycodeNumpadDot:
            return Key::NumpadDecimal;
        case kAkeycodeNumpadDivide:
            return Key::NumpadDivide;
        case kAkeycodeNumpadMultiply:
            return Key::NumpadMultiply;
        case kAkeycodeNumpadSubtract:
            return Key::NumpadSubtract;
        case kAkeycodeNumpadAdd:
            return Key::NumpadAdd;
        case kAkeycodeNumpadEnter:
            return Key::NumpadEnter;
        default:
            return Key::COUNT;
    }
}

}  // namespace engine::platform
