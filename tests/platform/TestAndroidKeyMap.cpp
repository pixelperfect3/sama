#include <catch2/catch_test_macros.hpp>

#include "engine/platform/android/AndroidKeyMap.h"

using engine::input::Key;
using engine::platform::mapAndroidKeyCode;

// AKEYCODE constants (matching Android NDK values) for desktop testing.
namespace
{
constexpr int32_t AKEYCODE_A = 29;
constexpr int32_t AKEYCODE_Z = 54;
constexpr int32_t AKEYCODE_M = 29 + 12;  // 'M' is the 13th letter
constexpr int32_t AKEYCODE_0 = 7;
constexpr int32_t AKEYCODE_9 = 16;
constexpr int32_t AKEYCODE_5 = 12;
constexpr int32_t AKEYCODE_SPACE = 62;
constexpr int32_t AKEYCODE_ENTER = 66;
constexpr int32_t AKEYCODE_ESCAPE = 111;
constexpr int32_t AKEYCODE_BACK = 4;
constexpr int32_t AKEYCODE_DPAD_UP = 19;
constexpr int32_t AKEYCODE_DPAD_DOWN = 20;
constexpr int32_t AKEYCODE_DPAD_LEFT = 21;
constexpr int32_t AKEYCODE_DPAD_RIGHT = 22;
constexpr int32_t AKEYCODE_TAB = 61;
constexpr int32_t AKEYCODE_DEL = 67;
constexpr int32_t AKEYCODE_FORWARD_DEL = 112;
constexpr int32_t AKEYCODE_SHIFT_LEFT = 59;
constexpr int32_t AKEYCODE_SHIFT_RIGHT = 60;
constexpr int32_t AKEYCODE_CTRL_LEFT = 113;
constexpr int32_t AKEYCODE_CTRL_RIGHT = 114;
constexpr int32_t AKEYCODE_ALT_LEFT = 57;
constexpr int32_t AKEYCODE_ALT_RIGHT = 58;
constexpr int32_t AKEYCODE_F1 = 131;
constexpr int32_t AKEYCODE_F12 = 142;
constexpr int32_t AKEYCODE_NUMPAD_0 = 144;
constexpr int32_t AKEYCODE_NUMPAD_9 = 153;
constexpr int32_t AKEYCODE_UNKNOWN = 999;
}  // namespace

TEST_CASE("AndroidKeyMap: A-Z mapping", "[platform][input]")
{
    CHECK(mapAndroidKeyCode(AKEYCODE_A) == Key::A);
    CHECK(mapAndroidKeyCode(AKEYCODE_Z) == Key::Z);
    CHECK(mapAndroidKeyCode(AKEYCODE_M) == Key::M);
}

TEST_CASE("AndroidKeyMap: 0-9 mapping", "[platform][input]")
{
    CHECK(mapAndroidKeyCode(AKEYCODE_0) == Key::Num0);
    CHECK(mapAndroidKeyCode(AKEYCODE_9) == Key::Num9);
    CHECK(mapAndroidKeyCode(AKEYCODE_5) == Key::Num5);
}

TEST_CASE("AndroidKeyMap: arrow keys", "[platform][input]")
{
    CHECK(mapAndroidKeyCode(AKEYCODE_DPAD_UP) == Key::Up);
    CHECK(mapAndroidKeyCode(AKEYCODE_DPAD_DOWN) == Key::Down);
    CHECK(mapAndroidKeyCode(AKEYCODE_DPAD_LEFT) == Key::Left);
    CHECK(mapAndroidKeyCode(AKEYCODE_DPAD_RIGHT) == Key::Right);
}

TEST_CASE("AndroidKeyMap: Space, Enter, Escape", "[platform][input]")
{
    CHECK(mapAndroidKeyCode(AKEYCODE_SPACE) == Key::Space);
    CHECK(mapAndroidKeyCode(AKEYCODE_ENTER) == Key::Enter);
    CHECK(mapAndroidKeyCode(AKEYCODE_ESCAPE) == Key::Escape);
    CHECK(mapAndroidKeyCode(AKEYCODE_BACK) == Key::Escape);
}

TEST_CASE("AndroidKeyMap: modifier keys", "[platform][input]")
{
    CHECK(mapAndroidKeyCode(AKEYCODE_SHIFT_LEFT) == Key::LeftShift);
    CHECK(mapAndroidKeyCode(AKEYCODE_SHIFT_RIGHT) == Key::RightShift);
    CHECK(mapAndroidKeyCode(AKEYCODE_CTRL_LEFT) == Key::LeftCtrl);
    CHECK(mapAndroidKeyCode(AKEYCODE_CTRL_RIGHT) == Key::RightCtrl);
    CHECK(mapAndroidKeyCode(AKEYCODE_ALT_LEFT) == Key::LeftAlt);
    CHECK(mapAndroidKeyCode(AKEYCODE_ALT_RIGHT) == Key::RightAlt);
}

TEST_CASE("AndroidKeyMap: Tab, Backspace, Delete", "[platform][input]")
{
    CHECK(mapAndroidKeyCode(AKEYCODE_TAB) == Key::Tab);
    CHECK(mapAndroidKeyCode(AKEYCODE_DEL) == Key::Backspace);
    CHECK(mapAndroidKeyCode(AKEYCODE_FORWARD_DEL) == Key::Delete);
}

TEST_CASE("AndroidKeyMap: function keys", "[platform][input]")
{
    CHECK(mapAndroidKeyCode(AKEYCODE_F1) == Key::F1);
    CHECK(mapAndroidKeyCode(AKEYCODE_F12) == Key::F12);
}

TEST_CASE("AndroidKeyMap: numpad", "[platform][input]")
{
    CHECK(mapAndroidKeyCode(AKEYCODE_NUMPAD_0) == Key::Numpad0);
    CHECK(mapAndroidKeyCode(AKEYCODE_NUMPAD_9) == Key::Numpad9);
}

TEST_CASE("AndroidKeyMap: unmapped key returns COUNT", "[platform][input]")
{
    CHECK(mapAndroidKeyCode(AKEYCODE_UNKNOWN) == Key::COUNT);
}
