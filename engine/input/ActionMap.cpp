#include "engine/input/ActionMap.h"

namespace engine::input
{

void ActionMap::bindKey(Key k, std::string_view action)
{
    keyBindings_[static_cast<size_t>(k)] = action;
}

std::string_view ActionMap::keyAction(Key k) const
{
    return keyBindings_[static_cast<size_t>(k)];
}

void ActionMap::bindMouseButton(MouseButton b, std::string_view action)
{
    mouseBindings_[static_cast<size_t>(b)] = action;
}

std::string_view ActionMap::mouseButtonAction(MouseButton b) const
{
    return mouseBindings_[static_cast<size_t>(b)];
}

void ActionMap::bindAxis(std::string_view axisName, Key negative, Key positive)
{
    for (auto& binding : axisBindings_)
    {
        if (binding.name == axisName)
        {
            binding.negative = negative;
            binding.positive = positive;
            return;
        }
    }
    axisBindings_.push_back({negative, positive, axisName});
}

const AxisBinding* ActionMap::axisBinding(std::string_view axisName) const
{
    for (const auto& binding : axisBindings_)
    {
        if (binding.name == axisName)
            return &binding;
    }
    return nullptr;
}

}  // namespace engine::input
