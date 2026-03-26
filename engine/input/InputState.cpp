#include "engine/input/InputState.h"

namespace engine::input
{

bool InputState::isKeyPressed(Key k) const
{
    return (keyFlags_[static_cast<size_t>(k)] & kFlagPressed) != 0;
}

bool InputState::isKeyHeld(Key k) const
{
    return (keyFlags_[static_cast<size_t>(k)] & kFlagHeld) != 0;
}

bool InputState::isKeyReleased(Key k) const
{
    return (keyFlags_[static_cast<size_t>(k)] & kFlagReleased) != 0;
}

bool InputState::isMouseButtonPressed(MouseButton b) const
{
    return (mouseFlags_[static_cast<size_t>(b)] & kFlagPressed) != 0;
}

bool InputState::isMouseButtonHeld(MouseButton b) const
{
    return (mouseFlags_[static_cast<size_t>(b)] & kFlagHeld) != 0;
}

bool InputState::isMouseButtonReleased(MouseButton b) const
{
    return (mouseFlags_[static_cast<size_t>(b)] & kFlagReleased) != 0;
}

// -----------------------------------------------------------------------
// Action helpers — iterate all keys/buttons for the given action name
// -----------------------------------------------------------------------

bool InputState::isActionPressed(std::string_view action, const ActionMap& map) const
{
    for (size_t i = 0; i < kKeyCount; ++i)
    {
        if (map.keyAction(static_cast<Key>(i)) == action)
            if (keyFlags_[i] & kFlagPressed)
                return true;
    }
    for (size_t i = 0; i < kMouseButtonCount; ++i)
    {
        if (map.mouseButtonAction(static_cast<MouseButton>(i)) == action)
            if (mouseFlags_[i] & kFlagPressed)
                return true;
    }
    return false;
}

bool InputState::isActionHeld(std::string_view action, const ActionMap& map) const
{
    for (size_t i = 0; i < kKeyCount; ++i)
    {
        if (map.keyAction(static_cast<Key>(i)) == action)
            if (keyFlags_[i] & kFlagHeld)
                return true;
    }
    for (size_t i = 0; i < kMouseButtonCount; ++i)
    {
        if (map.mouseButtonAction(static_cast<MouseButton>(i)) == action)
            if (mouseFlags_[i] & kFlagHeld)
                return true;
    }
    return false;
}

bool InputState::isActionReleased(std::string_view action, const ActionMap& map) const
{
    for (size_t i = 0; i < kKeyCount; ++i)
    {
        if (map.keyAction(static_cast<Key>(i)) == action)
            if (keyFlags_[i] & kFlagReleased)
                return true;
    }
    for (size_t i = 0; i < kMouseButtonCount; ++i)
    {
        if (map.mouseButtonAction(static_cast<MouseButton>(i)) == action)
            if (mouseFlags_[i] & kFlagReleased)
                return true;
    }
    return false;
}

const TouchPoint* InputState::touchById(uint64_t id) const
{
    for (const auto& t : touches_)
        if (t.id == id)
            return &t;
    return nullptr;
}

float InputState::axisValue(std::string_view axisName, const ActionMap& map) const
{
    const AxisBinding* binding = map.axisBinding(axisName);
    if (!binding)
        return 0.0f;

    bool neg = (binding->negative != Key::COUNT) &&
               (keyFlags_[static_cast<size_t>(binding->negative)] & kFlagHeld);
    bool pos = (binding->positive != Key::COUNT) &&
               (keyFlags_[static_cast<size_t>(binding->positive)] & kFlagHeld);

    if (pos == neg)
        return 0.0f;
    return pos ? 1.0f : -1.0f;
}

}  // namespace engine::input
