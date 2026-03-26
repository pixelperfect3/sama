#pragma once

#include <vector>

#include "engine/input/RawEvent.h"

namespace engine::input
{

// Platform seam: implementations deliver raw input events to the engine.
// A backend is called once per frame by InputSystem::update().
class IInputBackend
{
public:
    virtual ~IInputBackend() = default;

    // Append all events buffered since the last call into `out`.
    // Clears the backend's internal buffer.
    virtual void collectEvents(std::vector<RawEvent>& out) = 0;

    // Current absolute cursor / touch position (platform screen coordinates).
    virtual void mousePosition(double& x, double& y) const = 0;
};

}  // namespace engine::input
