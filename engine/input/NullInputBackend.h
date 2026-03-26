#pragma once

#include "engine/input/IInputBackend.h"

namespace engine::input
{

// No-op backend — produces no events. Useful for headless contexts and as a
// safe default when no real backend is available.
class NullInputBackend final : public IInputBackend
{
public:
    void collectEvents(std::vector<RawEvent>& /*out*/) override {}
    void mousePosition(double& x, double& y) const override
    {
        x = 0.0;
        y = 0.0;
    }
};

}  // namespace engine::input
