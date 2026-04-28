#include "engine/platform/ios/IosTouchInput.h"

#include <algorithm>

#if defined(__APPLE__) && TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif

namespace engine::platform::ios
{

#if defined(__APPLE__) && TARGET_OS_IPHONE

// ---------------------------------------------------------------------------
// _SamaTouchOverlay — transparent UIView that rides on top of the game view
// and forwards UITouch events into the C++ IosTouchInput.
//
// We use a dedicated overlay (rather than subclassing the metal view itself)
// so the input layer stays decoupled from the rendering layer — the metal
// view can be replaced without losing input wiring, and this class can be
// unit-tested against a stub UIView.
// ---------------------------------------------------------------------------

}  // namespace engine::platform::ios

@interface _SamaTouchOverlay : UIView
@property(nonatomic, assign) engine::platform::ios::IosTouchInput* owner;
@end

@implementation _SamaTouchOverlay

- (instancetype)initWithFrame:(CGRect)frame owner:(engine::platform::ios::IosTouchInput*)owner
{
    self = [super initWithFrame:frame];
    if (self)
    {
        _owner = owner;
        self.multipleTouchEnabled = YES;
        self.exclusiveTouch = NO;
        self.userInteractionEnabled = YES;
        self.backgroundColor = [UIColor clearColor];
        self.opaque = NO;
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    }
    return self;
}

// Convert a UITouch's view-local point into physical pixel coordinates so
// the engine sees the same units as bgfx framebuffer (matching Android,
// which always reports raw motion-event pixels).
static inline void _resolvePixel(UITouch* t, UIView* host, float* outX, float* outY)
{
    CGPoint p = [t locationInView:host];
    CGFloat scale = host.contentScaleFactor;
    *outX = static_cast<float>(p.x * scale);
    *outY = static_cast<float>(p.y * scale);
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (!_owner)
        return;
    for (UITouch* t in touches)
    {
        float x, y;
        _resolvePixel(t, self, &x, &y);
        _owner->onTouchBegin(static_cast<uint64_t>([t hash]), x, y);
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (!_owner)
        return;
    for (UITouch* t in touches)
    {
        float x, y;
        _resolvePixel(t, self, &x, &y);
        _owner->onTouchMove(static_cast<uint64_t>([t hash]), x, y);
    }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (!_owner)
        return;
    for (UITouch* t in touches)
    {
        float x, y;
        _resolvePixel(t, self, &x, &y);
        _owner->onTouchEnd(static_cast<uint64_t>([t hash]), x, y);
    }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (!_owner)
        return;
    for (UITouch* t in touches)
    {
        float x, y;
        _resolvePixel(t, self, &x, &y);
        _owner->onTouchCancel(static_cast<uint64_t>([t hash]), x, y);
    }
}

@end

namespace engine::platform::ios
{

#endif  // __APPLE__ && TARGET_OS_IPHONE

IosTouchInput::IosTouchInput() = default;

IosTouchInput::~IosTouchInput()
{
    detach();
}

void IosTouchInput::attach(void* parentUiViewPtr)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    detach();
    if (parentUiViewPtr == nullptr)
        return;

    UIView* parent = (__bridge UIView*)parentUiViewPtr;
    _SamaTouchOverlay* overlay = [[_SamaTouchOverlay alloc] initWithFrame:parent.bounds owner:this];
    [parent addSubview:overlay];

    // Keep an unsafe pointer back to the overlay for detach().  ARC keeps
    // the overlay alive via the parent view's subviews array.
    overlayView_ = (__bridge void*)overlay;
#else
    (void)parentUiViewPtr;
#endif
}

void IosTouchInput::detach()
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    if (!overlayView_)
        return;

    _SamaTouchOverlay* overlay = (__bridge _SamaTouchOverlay*)overlayView_;
    overlay.owner = nullptr;
    [overlay removeFromSuperview];
    overlayView_ = nullptr;
#endif
    activeTouches_.clear();
    mouseTouchActive_ = false;
    hasPrevMouse_ = false;
}

void IosTouchInput::bindState(engine::input::InputState* state)
{
    state_ = state;
}

void IosTouchInput::endFrame(engine::input::InputState& state)
{
    // Clear per-frame pressed/released flags for keys.  iOS phones have no
    // hardware keyboard support today, but the InputState contract still
    // requires the same edge-clearing each frame in case a future hardware
    // keyboard backend writes into state.keyFlags_.
    for (auto& f : state.keyFlags_)
    {
        f &= ~(engine::input::InputState::kFlagPressed | engine::input::InputState::kFlagReleased);
    }

    for (auto& f : state.mouseFlags_)
    {
        f &= ~(engine::input::InputState::kFlagPressed | engine::input::InputState::kFlagReleased);
    }

    state.mouseDeltaX_ = 0.0;
    state.mouseDeltaY_ = 0.0;

    // Drop ended touches; promote Began -> Moved for the next frame.
    state.touches_.erase(
        std::remove_if(state.touches_.begin(), state.touches_.end(),
                       [](const engine::input::TouchPoint& tp)
                       { return tp.phase == engine::input::TouchPoint::Phase::Ended; }),
        state.touches_.end());

    for (auto& tp : state.touches_)
    {
        if (tp.phase == engine::input::TouchPoint::Phase::Began)
        {
            tp.phase = engine::input::TouchPoint::Phase::Moved;
        }
    }
}

void IosTouchInput::onTouchBegin(uint64_t touchId, float x, float y)
{
    if (!state_)
        return;

    // Track the active touch so we can recognise its move/end pair.
    ActiveTouch at;
    at.touchId = touchId;
    at.x = x;
    at.y = y;
    activeTouches_.push_back(at);

    engine::input::TouchPoint tp;
    tp.id = touchId;
    tp.x = x;
    tp.y = y;
    tp.phase = engine::input::TouchPoint::Phase::Began;
    state_->touches_.push_back(tp);

    // First active touch on screen drives the mouse-cursor compatibility
    // path.  Subsequent fingers do not steal the mouse.
    if (!mouseTouchActive_)
    {
        mouseTouchActive_ = true;
        mouseTouchId_ = touchId;
        state_->mouseX_ = static_cast<double>(x);
        state_->mouseY_ = static_cast<double>(y);
        state_->mouseDeltaX_ = 0.0;
        state_->mouseDeltaY_ = 0.0;
        prevMouseX_ = static_cast<double>(x);
        prevMouseY_ = static_cast<double>(y);
        hasPrevMouse_ = true;

        auto idx = static_cast<size_t>(engine::input::MouseButton::Left);
        state_->mouseFlags_[idx] |=
            engine::input::InputState::kFlagPressed | engine::input::InputState::kFlagHeld;
    }
}

void IosTouchInput::onTouchMove(uint64_t touchId, float x, float y)
{
    if (!state_)
        return;

    for (auto& at : activeTouches_)
    {
        if (at.touchId == touchId)
        {
            at.x = x;
            at.y = y;
            break;
        }
    }

    bool found = false;
    for (auto& tp : state_->touches_)
    {
        if (tp.id == touchId)
        {
            tp.x = x;
            tp.y = y;
            // Don't downgrade Began -> Moved within the same frame; the
            // first frame should keep the Began phase so apps can detect
            // the press edge.  endFrame() will promote it next frame.
            if (tp.phase != engine::input::TouchPoint::Phase::Began)
            {
                tp.phase = engine::input::TouchPoint::Phase::Moved;
            }
            found = true;
            break;
        }
    }
    if (!found)
    {
        engine::input::TouchPoint tp;
        tp.id = touchId;
        tp.x = x;
        tp.y = y;
        tp.phase = engine::input::TouchPoint::Phase::Moved;
        state_->touches_.push_back(tp);
    }

    // Drive mouse position from the touch that "owns" the cursor.
    if (mouseTouchActive_ && touchId == mouseTouchId_)
    {
        state_->mouseX_ = static_cast<double>(x);
        state_->mouseY_ = static_cast<double>(y);
        if (hasPrevMouse_)
        {
            state_->mouseDeltaX_ = static_cast<double>(x) - prevMouseX_;
            state_->mouseDeltaY_ = static_cast<double>(y) - prevMouseY_;
        }
        prevMouseX_ = static_cast<double>(x);
        prevMouseY_ = static_cast<double>(y);
        hasPrevMouse_ = true;
    }
}

void IosTouchInput::onTouchEnd(uint64_t touchId, float x, float y)
{
    if (!state_)
        return;

    activeTouches_.erase(
        std::remove_if(activeTouches_.begin(), activeTouches_.end(),
                       [touchId](const ActiveTouch& at) { return at.touchId == touchId; }),
        activeTouches_.end());

    for (auto& tp : state_->touches_)
    {
        if (tp.id == touchId)
        {
            tp.x = x;
            tp.y = y;
            tp.phase = engine::input::TouchPoint::Phase::Ended;
            break;
        }
    }

    if (mouseTouchActive_ && touchId == mouseTouchId_)
    {
        mouseTouchActive_ = false;
        hasPrevMouse_ = false;
        auto idx = static_cast<size_t>(engine::input::MouseButton::Left);
        state_->mouseFlags_[idx] &= ~engine::input::InputState::kFlagHeld;
        state_->mouseFlags_[idx] |= engine::input::InputState::kFlagReleased;
    }
}

void IosTouchInput::onTouchCancel(uint64_t touchId, float x, float y)
{
    // Cancellation behaves like an end for the cancelled finger.  iOS
    // delivers touchesCancelled: when the system intercepts the gesture
    // (e.g. a phone call alert).  Treat it as an Ended touch so the game
    // doesn't see a perpetually held finger.
    onTouchEnd(touchId, x, y);
}

}  // namespace engine::platform::ios
