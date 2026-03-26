#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include "engine/input/IInputBackend.h"
#include "engine/input/Key.h"

#if defined(__ANDROID__)
struct AInputEvent;
#endif

namespace engine::input
{

#if defined(__ANDROID__)
// Translate an Android AKEYCODE_* value to an engine Key.
// Returns nullopt for unmapped keys.
std::optional<Key> androidKeyCodeToKey(int32_t akeycode);
#endif

// Android NDK input backend.
//
// The game's main event loop (android_main / GameActivity) must call
// processEvent() for every AInputEvent* it receives from AInputQueue.
//
// Usage:
//   AndroidInputBackend backend;
//
//   // in the Android event loop:
//   AInputEvent* event = nullptr;
//   while (AInputQueue_getEvent(queue, &event) >= 0) {
//       backend.processEvent(event);
//       AInputQueue_finishEvent(queue, event, 1);
//   }
//
//   // per-frame:
//   InputState state;
//   sys.update(state);
class AndroidInputBackend final : public IInputBackend
{
public:
    AndroidInputBackend() = default;

    // IInputBackend
    void collectEvents(std::vector<RawEvent>& out) override;
    void mousePosition(double& x, double& y) const override;

#if defined(__ANDROID__)
    // Process a single NDK input event. Call this from the Android event loop.
    // Returns true if the event was consumed.
    bool processEvent(AInputEvent* event);
#endif

private:
    mutable std::mutex mutex_;
    std::vector<RawEvent> writeBuffer_;

    float lastTouchX_ = 0.0f;
    float lastTouchY_ = 0.0f;
};

}  // namespace engine::input
