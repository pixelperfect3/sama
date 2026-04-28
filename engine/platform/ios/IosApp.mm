#include "engine/platform/ios/IosApp.h"

#include <cstdio>
#include <memory>

#include "engine/game/IGame.h"
#include "engine/platform/ios/IosFileSystem.h"
#include "engine/platform/ios/IosGlobals.h"
#include "engine/platform/ios/IosGyro.h"
#include "engine/platform/ios/IosTouchInput.h"
#include "engine/platform/ios/IosWindow.h"

#if defined(__APPLE__) && TARGET_OS_IPHONE
#import <QuartzCore/CADisplayLink.h>
#import <UIKit/UIKit.h>
#endif

// ---------------------------------------------------------------------------
// Engine integration — TODO follow-up.
//
// This file deliberately does NOT call into engine::core::Engine yet.  When
// `Engine::initIos(IosWindow*, IosTouchInput*, IosGyro*, IosFileSystem*,
// EngineDesc)` lands (mirroring `initAndroid`), the frame callback in
// _SamaAppDelegate should construct the Engine + GameRunner the same way
// AndroidApp / GameRunner::runAndroid does today.  Until then, this file
// stands up the platform layer (window, input, gyro, file system) and a
// CADisplayLink-driven tick so the wiring can be exercised in isolation by
// the apps/ios_test stub.
// ---------------------------------------------------------------------------

#if defined(__APPLE__) && TARGET_OS_IPHONE

// ---------------------------------------------------------------------------
// _SamaAppDelegate — owns the per-process platform objects.
//
// The application delegate is the shortest path to a running game on iOS
// 15+.  We do not adopt UISceneDelegate because the engine is single-window;
// the additional ceremony is not worth the savings until multi-window /
// stage-manager support is needed.  Document this choice here so the next
// agent doesn't accidentally split the wiring across both lifecycles.
// ---------------------------------------------------------------------------

@interface _SamaAppDelegate : UIResponder <UIApplicationDelegate>
{
@public
    std::unique_ptr<engine::platform::ios::IosWindow> _window;
    std::unique_ptr<engine::platform::ios::IosTouchInput> _touch;
    std::unique_ptr<engine::platform::ios::IosGyro> _gyro;
    std::unique_ptr<engine::platform::ios::IosFileSystem> _fileSystem;

    engine::game::IGame* _game;  // owned by this delegate, deleted on terminate
}

@property(nonatomic, strong) UIWindow* uiWindow;
@property(nonatomic, strong) CADisplayLink* displayLink;
@end

@implementation _SamaAppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    // -- File system ------------------------------------------------------
    // Construct first so the bundle root is published to IosGlobals before
    // any other subsystem looks for assets.
    _fileSystem = std::make_unique<engine::platform::ios::IosFileSystem>();

    // -- UIWindow + Metal-backed view ------------------------------------
    self.uiWindow = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];

    _window = std::make_unique<engine::platform::ios::IosWindow>();
    _window->setNativeWindow((__bridge void*)self.uiWindow);

    // -- Touch input overlay ---------------------------------------------
    _touch = std::make_unique<engine::platform::ios::IosTouchInput>();
    _touch->attach(_window->nativeView());

    // -- Gyro -------------------------------------------------------------
    _gyro = std::make_unique<engine::platform::ios::IosGyro>();
    if (_gyro->init())
    {
        _gyro->setEnabled(true);
    }

    // -- Game instance ----------------------------------------------------
    // samaCreateGame() lives in the application's TU; called once at launch.
    _game = ::samaCreateGame();
    if (_game == nullptr)
    {
        // Print to stderr so it shows up in Xcode's console.  Returning NO
        // would suppress UI but iOS still expects the launch to finish, so
        // we let the empty app run and rely on the visible black screen +
        // log message to flag the misconfiguration.
        std::fprintf(stderr, "Sama: samaCreateGame() returned null\n");
    }

    // -- CADisplayLink (drives the per-frame tick) ------------------------
    // 60Hz default; high-tier devices that opted in via Info.plist's
    // CADisableMinimumFrameDurationOnPhone can negotiate a higher rate.
    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(onFrame:)];
    [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

    return YES;
}

- (void)applicationWillResignActive:(UIApplication*)application
{
    // Pause the display link so we stop pushing frames while in the
    // background — bgfx's swap chain is not valid when the surface is
    // suspended.  Mirrors the focus-loss handling in AndroidApp.
    self.displayLink.paused = YES;
    if (_gyro)
        _gyro->setEnabled(false);
}

- (void)applicationDidBecomeActive:(UIApplication*)application
{
    self.displayLink.paused = NO;
    if (_gyro)
        _gyro->setEnabled(true);
}

- (void)applicationWillTerminate:(UIApplication*)application
{
    [self.displayLink invalidate];
    self.displayLink = nil;

    // Tear down in reverse construction order so the input overlay leaves
    // before the window, the gyro stops before its manager is freed, etc.
    if (_touch)
        _touch->detach();

    if (_gyro)
        _gyro->shutdown();

    if (_window)
        _window->clearNativeWindow();

    delete _game;
    _game = nullptr;

    _touch.reset();
    _gyro.reset();
    _window.reset();
    _fileSystem.reset();
}

- (void)onFrame:(CADisplayLink*)link
{
    // -- Per-frame engine tick goes here ---------------------------------
    //
    // Once Engine::initIos lands, this becomes the equivalent of the
    // GameRunner::runLoop body: poll the window for resizes, drain gyro
    // samples into the InputState, call game.onFixedUpdate / onUpdate /
    // onRender, then submit the bgfx frame.
    //
    // For now we just keep the platform alive and let the apps/ios_test
    // stub verify the wiring by inspecting the IosWindow / IosTouchInput
    // state through IosGlobals.

    // Eagerly re-publish the view pointer in case it changed (e.g. after
    // a scene reconnect).  Cheap and idempotent.
    if (_window)
    {
        engine::platform::ios::setGameView(_window->nativeView());
    }
}

@end

#endif  // __APPLE__ && TARGET_OS_IPHONE

namespace engine::platform::ios
{

int runIosApp(int argc, char** argv)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([_SamaAppDelegate class]));
    }
#else
    (void)argc;
    (void)argv;
    std::fprintf(stderr, "Sama: runIosApp called on a non-iOS build\n");
    return 1;
#endif
}

}  // namespace engine::platform::ios
