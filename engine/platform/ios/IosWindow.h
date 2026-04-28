#pragma once

#include <TargetConditionals.h>

#include <cstdint>

namespace engine::platform::ios
{

// ---------------------------------------------------------------------------
// IosWindow — owns the UIWindow + UIView (CAMetalLayer-backed) for the game.
//
// On iOS the OS hands us a UIScene / UIWindow that we must populate with a
// view controller.  This class wraps that ceremony and exposes the bits the
// engine needs:
//
//   - the CAMetalLayer*, which goes into bgfx::PlatformData::nwh
//   - drawable size in physical pixels (UIView.bounds * contentScaleFactor)
//   - content scale, for DPI-aware HUD / cursor logic
//
// All UI work happens on the main thread; drawableSize / nativeLayer can be
// queried from any thread because they are just cached uint32_t / pointer
// values updated on the main thread.
//
// Lifetime: instantiated by IosApp during application launch and torn down
// when the scene disconnects.  Mirrors the AndroidWindow contract: the
// caller calls setNativeWindow() / clearNativeWindow() at the right scene
// lifecycle points; queries (width/height/isReady) are safe at any time.
// ---------------------------------------------------------------------------

class IosWindow
{
public:
    IosWindow();
    ~IosWindow();

    IosWindow(const IosWindow&) = delete;
    IosWindow& operator=(const IosWindow&) = delete;
    IosWindow(IosWindow&&) = delete;
    IosWindow& operator=(IosWindow&&) = delete;

    // Attach to a UIWindow* (cast to void* to keep this header plain C++).
    // Creates the root view controller, the Metal-backed UIView and the
    // CAMetalLayer.  Caches the initial drawable size and content scale.
    //
    // The UIWindow remains owned by the application / UIScene — IosWindow
    // does not take ownership.  __unsafe_unretained semantics: callers must
    // call clearNativeWindow() before releasing the UIWindow.
    void setNativeWindow(void* uiWindowPtr);

    // Detach from the UIWindow.  Removes the view controller and drops all
    // cached pointers.  After this isReady() returns false.
    void clearNativeWindow();

    // True once a UIWindow has been attached and the Metal layer is live.
    [[nodiscard]] bool isReady() const
    {
        return ready_;
    }

    // Pixel-space drawable size (CAMetalLayer.drawableSize).  Updated by
    // updateSize() in response to layoutSubviews / orientation changes.
    [[nodiscard]] uint32_t width() const
    {
        return width_;
    }
    [[nodiscard]] uint32_t height() const
    {
        return height_;
    }

    // [UIScreen mainScreen].nativeScale (typically 2.0 or 3.0).  Used by the
    // engine for HUD / DPI scaling, mirroring AndroidWindow::contentScale().
    [[nodiscard]] float contentScale() const
    {
        return contentScale_;
    }

    // CAMetalLayer* cast to void*.  This is the value bgfx wants in
    // PlatformData::nwh.  Returns nullptr until setNativeWindow() succeeds.
    [[nodiscard]] void* nativeLayer() const
    {
        return metalLayer_;
    }

    // UIView* cast to void*.  Useful for attaching touch overlays (the input
    // backend installs a transparent subview here).  Returns nullptr until
    // setNativeWindow() succeeds.
    [[nodiscard]] void* nativeView() const
    {
        return view_;
    }

    // Re-query the layer's drawableSize.  Call from layoutSubviews or after
    // an orientation change.  Safe to call before setNativeWindow() (no-op).
    void updateSize();

private:
    // Non-owning pointers held as void* so the header stays plain C++.
    // Bridge casts in the .mm guarantee these are __unsafe_unretained: their
    // lifetime is tied to the owning UIWindow / UIScene.
    void* uiWindow_ = nullptr;
    void* viewController_ = nullptr;
    void* view_ = nullptr;
    void* metalLayer_ = nullptr;

    bool ready_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    float contentScale_ = 1.0f;
};

}  // namespace engine::platform::ios
