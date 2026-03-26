#pragma once

#include <vector>

#include "engine/input/IInputBackend.h"
#include "engine/input/RawEvent.h"

namespace engine::input::test
{

// Injectable backend for unit tests. Push events before calling
// InputSystem::update(); collectEvents drains them exactly once.
class FakeInputBackend final : public IInputBackend
{
public:
    void push(RawEvent e)
    {
        pending_.push_back(e);
    }

    // Convenience helpers
    void pressKey(Key k)
    {
        push(RawEvent::keyDown(k));
    }
    void releaseKey(Key k)
    {
        push(RawEvent::keyUp(k));
    }

    void pressMouseButton(MouseButton b)
    {
        push(RawEvent::mouseButtonDown(b));
    }
    void releaseMouseButton(MouseButton b)
    {
        push(RawEvent::mouseButtonUp(b));
    }

    void moveMouse(double x, double y)
    {
        mouseX_ = x;
        mouseY_ = y;
        push(RawEvent::mouseMove(x, y));
    }

    void touchBegin(uint64_t id, float x, float y)
    {
        push(RawEvent::touchBegin(id, x, y));
    }
    void touchMove(uint64_t id, float x, float y)
    {
        push(RawEvent::touchMove(id, x, y));
    }
    void touchEnd(uint64_t id, float x, float y)
    {
        push(RawEvent::touchEnd(id, x, y));
    }

    // IInputBackend
    void collectEvents(std::vector<RawEvent>& out) override
    {
        out.insert(out.end(), pending_.begin(), pending_.end());
        pending_.clear();
    }

    void mousePosition(double& x, double& y) const override
    {
        x = mouseX_;
        y = mouseY_;
    }

private:
    std::vector<RawEvent> pending_;
    double mouseX_ = 0.0;
    double mouseY_ = 0.0;
};

}  // namespace engine::input::test
