#include "engine/platform/ios/IosWindow.h"

#if defined(__APPLE__) && TARGET_OS_IPHONE

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include "engine/platform/ios/IosGlobals.h"

// ---------------------------------------------------------------------------
// _SamaMetalView — a UIView whose backing CALayer is a CAMetalLayer.
//
// This is the canonical way to host a Metal drawable on iOS: override
// +layerClass to return [CAMetalLayer class] and UIKit will create the layer
// of the right type when the view is instantiated.
//
// We forward layoutSubviews to a C++ callback so the IosWindow can re-cache
// the drawable size whenever the OS changes the view's bounds (rotation,
// split-screen on iPad, status bar size changes, etc.).
// ---------------------------------------------------------------------------

@interface _SamaMetalView : UIView
@property(nonatomic, assign) engine::platform::ios::IosWindow* owner;
@end

@implementation _SamaMetalView

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (void)layoutSubviews
{
    [super layoutSubviews];

    // Push the new pixel-space size into the CAMetalLayer so its next drawable
    // matches the view bounds.  iOS does NOT do this for us automatically.
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    CGFloat scale = self.contentScaleFactor;
    layer.drawableSize =
        CGSizeMake(self.bounds.size.width * scale, self.bounds.size.height * scale);

    if (_owner)
    {
        _owner->updateSize();
    }
}

@end

// ---------------------------------------------------------------------------
// _SamaViewController — host for the Metal view.
//
// Uses the device's native scale on the layer so drawable pixels match what
// CAMetalLayer.drawableSize reports.  Hides the home indicator and keeps the
// status bar out of the way for full-screen games.  Locks orientation to
// landscape to start (apps that want portrait can override later via the
// supportedInterfaceOrientations override on a custom subclass).
// ---------------------------------------------------------------------------

@interface _SamaViewController : UIViewController
@property(nonatomic, strong) _SamaMetalView* metalView;
@end

@implementation _SamaViewController

- (void)loadView
{
    _SamaMetalView* mv = [[_SamaMetalView alloc] initWithFrame:[UIScreen mainScreen].bounds];
    mv.contentScaleFactor = [UIScreen mainScreen].nativeScale;
    mv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    mv.multipleTouchEnabled = YES;
    mv.opaque = YES;
    mv.backgroundColor = [UIColor blackColor];

    CAMetalLayer* layer = (CAMetalLayer*)mv.layer;
    layer.contentsScale = mv.contentScaleFactor;
    layer.opaque = YES;
    layer.framebufferOnly = YES;  // standard Metal opt-in; bgfx is fine with it

    self.metalView = mv;
    self.view = mv;
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden
{
    return YES;
}

@end

namespace engine::platform::ios
{

IosWindow::IosWindow() = default;

IosWindow::~IosWindow()
{
    clearNativeWindow();
}

void IosWindow::setNativeWindow(void* uiWindowPtr)
{
    if (uiWindowPtr == nullptr)
        return;

    // The UIWindow is owned by the UIScene that created it.  We hold an
    // __unsafe_unretained reference so we never extend its lifetime.
    UIWindow* window = (__bridge UIWindow*)uiWindowPtr;

    _SamaViewController* vc = [[_SamaViewController alloc] init];
    [vc loadViewIfNeeded];
    window.rootViewController = vc;
    [window makeKeyAndVisible];

    _SamaMetalView* mv = vc.metalView;
    mv.owner = this;
    CAMetalLayer* layer = (CAMetalLayer*)mv.layer;

    // Stash raw pointers as void*.  ARC keeps the view controller alive via
    // the UIWindow's rootViewController strong reference, and the metal view
    // / layer live as long as the view controller does.  These pointers are
    // therefore valid until clearNativeWindow() runs (or the application
    // tears down the UIWindow first).
    uiWindow_ = (__bridge void*)window;
    viewController_ = (__bridge void*)vc;
    view_ = (__bridge void*)mv;
    metalLayer_ = (__bridge void*)layer;

    contentScale_ = static_cast<float>([UIScreen mainScreen].nativeScale);
    updateSize();
    ready_ = (width_ > 0 && height_ > 0);

    // Publish to the global so other subsystems (touch input, etc.) can find
    // the game view without holding a IosWindow reference.
    setGameView(view_);
}

void IosWindow::clearNativeWindow()
{
    if (view_)
    {
        _SamaMetalView* mv = (__bridge _SamaMetalView*)view_;
        mv.owner = nullptr;
    }

    if (uiWindow_)
    {
        // Drop the root view controller so ARC can release the view
        // controller, the metal view and the metal layer in that order.
        UIWindow* window = (__bridge UIWindow*)uiWindow_;
        window.rootViewController = nil;
    }

    setGameView(nullptr);

    uiWindow_ = nullptr;
    viewController_ = nullptr;
    view_ = nullptr;
    metalLayer_ = nullptr;
    width_ = 0;
    height_ = 0;
    ready_ = false;
}

void IosWindow::updateSize()
{
    if (!view_)
        return;

    _SamaMetalView* mv = (__bridge _SamaMetalView*)view_;
    CGFloat scale = mv.contentScaleFactor;
    CGFloat w = mv.bounds.size.width * scale;
    CGFloat h = mv.bounds.size.height * scale;
    if (w > 0 && h > 0)
    {
        width_ = static_cast<uint32_t>(w);
        height_ = static_cast<uint32_t>(h);
        ready_ = true;
    }
}

}  // namespace engine::platform::ios

#else  // not iOS — provide trivial stubs so this TU still compiles.

namespace engine::platform::ios
{

IosWindow::IosWindow() = default;
IosWindow::~IosWindow() = default;
void IosWindow::setNativeWindow(void*) {}
void IosWindow::clearNativeWindow() {}
void IosWindow::updateSize() {}

}  // namespace engine::platform::ios

#endif  // __APPLE__ && TARGET_OS_IPHONE
