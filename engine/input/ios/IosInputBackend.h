#pragma once

#include <mutex>
#include <vector>

#include "engine/input/IInputBackend.h"

// Forward-declare the ObjC bridge helper so this header stays plain C++.
#ifdef __OBJC__
@class _IosInputView;
#else
struct _IosInputView;
#endif

namespace engine::input
{

// iOS input backend. Attach to the game's UIView; touch events are routed
// through a transparent overlay subview that fills the parent.
//
// Usage (in application code, necessarily ObjC++):
//
//   UIView* gameView = ...;
//   IosInputBackend backend((__bridge void*)gameView);
//
//   // per-frame:
//   InputState state;
//   sys.update(state);
//
// Lifetime: backend must not outlive the UIView it was attached to.
class IosInputBackend final : public IInputBackend
{
public:
    // viewPtr must be a UIView* cast to void*.
    explicit IosInputBackend(void* viewPtr);
    ~IosInputBackend() override;

    // IInputBackend
    void collectEvents(std::vector<RawEvent>& out) override;
    void mousePosition(double& x, double& y) const override;

    // Called by the ObjC bridge on the main thread.
    void onTouchBegin(uint64_t touchId, float x, float y);
    void onTouchMove(uint64_t touchId, float x, float y);
    void onTouchEnd(uint64_t touchId, float x, float y);

private:
    _IosInputView* overlayView_ = nullptr;

    mutable std::mutex mutex_;
    std::vector<RawEvent> writeBuffer_;

    // Last known touch position (for mousePosition() compat).
    float lastX_ = 0.0f;
    float lastY_ = 0.0f;
};

}  // namespace engine::input
