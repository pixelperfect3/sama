// IosInputBackend.mm — must be compiled as Objective-C++ (.mm).
//
// _IosInputView is a transparent UIView subclass that sits on top of the
// game view and routes UIKit touch events into the C++ backend.

#import <UIKit/UIKit.h>
#include "engine/input/ios/IosInputBackend.h"

// ---------------------------------------------------------------------------
// ObjC bridge view
// ---------------------------------------------------------------------------

@interface _IosInputView : UIView
@property (nonatomic, assign) engine::input::IosInputBackend* backend;
@end

@implementation _IosInputView

- (instancetype)initWithFrame:(CGRect)frame
                      backend:(engine::input::IosInputBackend*)backend
{
    self = [super initWithFrame:frame];
    if (self)
    {
        _backend                   = backend;
        self.multipleTouchEnabled  = YES;
        self.exclusiveTouch        = NO;
        self.userInteractionEnabled = YES;
        // Transparent: pass visual rendering to the game view below.
        self.backgroundColor       = [UIColor clearColor];
        self.opaque                = NO;
    }
    return self;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    for (UITouch* t in touches)
    {
        CGPoint p = [t locationInView:self];
        _backend->onTouchBegin(static_cast<uint64_t>([t hash]),
                               static_cast<float>(p.x),
                               static_cast<float>(p.y));
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    for (UITouch* t in touches)
    {
        CGPoint p = [t locationInView:self];
        _backend->onTouchMove(static_cast<uint64_t>([t hash]),
                              static_cast<float>(p.x),
                              static_cast<float>(p.y));
    }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    for (UITouch* t in touches)
    {
        CGPoint p = [t locationInView:self];
        _backend->onTouchEnd(static_cast<uint64_t>([t hash]),
                             static_cast<float>(p.x),
                             static_cast<float>(p.y));
    }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    // Treat cancelled touches (e.g., incoming call) the same as ended.
    [self touchesEnded:touches withEvent:event];
}

@end

// ---------------------------------------------------------------------------
// IosInputBackend
// ---------------------------------------------------------------------------

namespace engine::input
{

IosInputBackend::IosInputBackend(void* viewPtr)
{
    UIView* parentView = (__bridge UIView*)viewPtr;
    overlayView_ = [[_IosInputView alloc] initWithFrame:parentView.bounds
                                                backend:this];
    overlayView_.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [parentView addSubview:overlayView_];
}

IosInputBackend::~IosInputBackend()
{
    // Clear the back-pointer before removing the view so any in-flight
    // callbacks on the main thread see a null backend.
    overlayView_.backend = nullptr;
    [overlayView_ removeFromSuperview];
    overlayView_ = nullptr;
}

void IosInputBackend::collectEvents(std::vector<RawEvent>& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    out.insert(out.end(), writeBuffer_.begin(), writeBuffer_.end());
    writeBuffer_.clear();
}

void IosInputBackend::mousePosition(double& x, double& y) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    x = static_cast<double>(lastX_);
    y = static_cast<double>(lastY_);
}

void IosInputBackend::onTouchBegin(uint64_t touchId, float x, float y)
{
    std::lock_guard<std::mutex> lock(mutex_);
    lastX_ = x;
    lastY_ = y;
    writeBuffer_.push_back(RawEvent::touchBegin(touchId, x, y));
}

void IosInputBackend::onTouchMove(uint64_t touchId, float x, float y)
{
    std::lock_guard<std::mutex> lock(mutex_);
    lastX_ = x;
    lastY_ = y;
    writeBuffer_.push_back(RawEvent::touchMove(touchId, x, y));
}

void IosInputBackend::onTouchEnd(uint64_t touchId, float x, float y)
{
    std::lock_guard<std::mutex> lock(mutex_);
    writeBuffer_.push_back(RawEvent::touchEnd(touchId, x, y));
}

}  // namespace engine::input
