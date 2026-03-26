#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "engine/input/IInputBackend.h"
#include "engine/input/InputState.h"
#include "engine/input/Key.h"
#include "engine/input/RawEvent.h"

namespace engine::input
{

// Drives an IInputBackend once per frame and produces an InputState snapshot.
//
// Usage:
//   InputSystem sys(backend);
//   while (running) {
//       InputState state;
//       sys.update(state);
//       // query state this frame
//   }
class InputSystem
{
public:
    explicit InputSystem(IInputBackend& backend);

    // Collect events from the backend, compute transitions, fill `state`.
    // Call exactly once per frame.
    void update(InputState& state);

private:
    IInputBackend& backend_;

    // Per-key down state carried from the previous frame.
    std::array<bool, InputState::kKeyCount> prevKeyDown_{};
    std::array<bool, InputState::kMouseButtonCount> prevMouseDown_{};

    // Last known mouse position (for delta calculation).
    double prevMouseX_ = 0.0;
    double prevMouseY_ = 0.0;
    bool firstFrame_ = true;

    // Active touches carried across frames: used to emit Moved phase for
    // stationary contacts each frame.
    struct ActiveTouch
    {
        uint64_t id;
        float x, y;
    };
    std::vector<ActiveTouch> activeTouches_;

    std::vector<RawEvent> eventBuf_;
};

}  // namespace engine::input
