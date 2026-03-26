#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "engine/input/Key.h"

namespace engine::input
{

// A binding that maps two keys to a signed axis value.
//   negative key held → -1.0f
//   positive key held → +1.0f
//   both or neither   →  0.0f
struct AxisBinding
{
    Key negative = Key::COUNT;  // COUNT = slot unbound
    Key positive = Key::COUNT;
    std::string_view name;
};

// Maps Keys and MouseButtons to action names, and pairs of keys to named axes.
//
// All string_view action names must point to storage that outlives the ActionMap
// (use string literals or constants with static/global lifetime).
class ActionMap
{
public:
    // Bind a key to an action name.  Overwrites any previous binding for that key.
    void bindKey(Key k, std::string_view action);

    // Return the action bound to key k, or "" if unbound.
    [[nodiscard]] std::string_view keyAction(Key k) const;

    // Bind a mouse button to an action name.
    void bindMouseButton(MouseButton b, std::string_view action);

    // Return the action bound to button b, or "" if unbound.
    [[nodiscard]] std::string_view mouseButtonAction(MouseButton b) const;

    // Bind a named axis to a (negative, positive) key pair.
    void bindAxis(std::string_view axisName, Key negative, Key positive);

    // Return pointer to the AxisBinding with this name, or nullptr.
    [[nodiscard]] const AxisBinding* axisBinding(std::string_view axisName) const;

private:
    static constexpr size_t kKeyCount = static_cast<size_t>(Key::COUNT);
    static constexpr size_t kMouseButtonCount = static_cast<size_t>(MouseButton::COUNT);

    std::array<std::string_view, kKeyCount> keyBindings_{};
    std::array<std::string_view, kMouseButtonCount> mouseBindings_{};

    // Few axes expected — linear scan over a small vector is fine.
    std::vector<AxisBinding> axisBindings_;
};

}  // namespace engine::input
