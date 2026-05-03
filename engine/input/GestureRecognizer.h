#pragma once

#include <cstdint>

namespace engine::input
{

class InputState;

// Per-frame snapshot of multi-touch gesture deltas.  All deltas are *frame*
// deltas (not cumulative) measured in pixels, so divide by `dt` if you want a
// rate.  Reset to zero when fewer than two touches are active.
//
// Sign conventions:
//   pinchDelta  > 0 → fingers moved apart this frame  (zoom in)
//   pinchDelta  < 0 → fingers moved together          (zoom out)
//   panDeltaX/Y     → midpoint translation in pixels  (same axes as touches)
struct GestureState
{
    float pinchDelta = 0.f;
    float panDeltaX = 0.f;
    float panDeltaY = 0.f;
    bool active = false;
};

// Cross-platform two-finger gesture recognizer.  Reads `InputState::touches()`
// and converts the first two stable touch IDs into per-frame pinch + pan
// deltas.
//
// Lifecycle:
//   - touches < 2          → not tracking; output zero, active=false
//   - touches >= 2 and not yet tracking → capture the two IDs, record the
//     initial distance and midpoint, output zero this frame (no delta yet),
//     active=true
//   - touches >= 2 and tracking with the same IDs → emit current-vs-last
//     delta, then update last
//   - touches >= 2 but the tracked IDs changed (one finger lifted, another
//     down) → re-anchor on the new pair this frame; skip emitting a delta to
//     avoid the spike that the discontinuity would produce
class GestureRecognizer
{
public:
    // Call once per frame after `InputSystem::update()` has populated
    // `input.touches()`.
    void update(const InputState& input, GestureState& outState);

    // Resets internal tracking state (e.g. on activity pause/resume).  The
    // next `update()` will treat the next two-touch frame as the start of a
    // fresh gesture.
    void reset();

private:
    bool tracking_ = false;
    uint64_t trackedId0_ = 0;
    uint64_t trackedId1_ = 0;
    float lastDistance_ = 0.f;
    float lastMidX_ = 0.f;
    float lastMidY_ = 0.f;
};

}  // namespace engine::input
