#include "engine/platform/ios/IosApp.h"

#include <cstdio>
#include <memory>

#include "engine/core/Engine.h"
#include "engine/game/GameRunner.h"
#include "engine/game/IGame.h"
#include "engine/game/ProjectConfig.h"
#include "engine/platform/ios/IosFileSystem.h"
#include "engine/platform/ios/IosGlobals.h"
#include "engine/platform/ios/IosGyro.h"
#include "engine/platform/ios/IosTierDetect.h"
#include "engine/platform/ios/IosTouchInput.h"
#include "engine/platform/ios/IosWindow.h"

#if defined(__APPLE__) && TARGET_OS_IPHONE
#import <QuartzCore/CADisplayLink.h>
#import <UIKit/UIKit.h>
#endif

// ---------------------------------------------------------------------------
// Engine integration.
//
// `_SamaAppDelegate` owns the platform pieces (window, input, gyro, file
// system), a `GameRunner` driving the iOS lifecycle (runIos at launch,
// tickIos per CADisplayLink, shutdownIos at terminate), and the IGame
// instance returned by `samaCreateGame()`.  Tear-down order on
// applicationWillTerminate: tickIos signal -> shutdownIos -> delete game ->
// release platform pieces.  Mirrors the Android lifecycle in AndroidApp.
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

    engine::game::IGame* _game;         // owned by this delegate, deleted on terminate
    engine::game::GameRunner* _runner;  // owned by this delegate; references _game
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

    // -- Show the window so the Metal-backed view goes on screen ----------
    // GameRunner::runIos() init's bgfx against the CAMetalLayer below, but
    // the layer must be in the view hierarchy first.
    [self.uiWindow makeKeyAndVisible];

    // -- Engine + GameRunner ---------------------------------------------
    // EngineDesc is largely advisory on iOS — initIos reads dimensions
    // from the IosWindow itself (UIScreen.mainScreen.bounds × scale).
    //
    // We build a ProjectConfig (rather than a bare EngineDesc) so the
    // device-detected tier (IosTierDetect.h) flows into shadow resolution
    // / cascade count via TierConfig.  Future work will read the same
    // ProjectConfig from the bundle's project.json; for now there is no
    // such file in the iOS bundle, so we use defaults + the detected tier.
    int targetFps = 60;  // safe default; overridden below if a tier was chosen
    if (_game != nullptr)
    {
        _runner = new engine::game::GameRunner(*_game);

        engine::game::ProjectConfig config;
        const auto detectedTier = engine::platform::ios::detectIosTier();
        config.activeTier = engine::platform::ios::tierToProjectConfigName(detectedTier);
        std::fprintf(stderr, "[Sama][iOS] ProjectConfig::activeTier = \"%s\"\n",
                     config.activeTier.c_str());

        // Pull targetFPS from the active tier so we can hand it to the
        // CADisplayLink below.  Built-in tiers: low=30, mid=30, high=60.
        targetFps = config.getActiveTier().targetFPS;

        const engine::core::EngineDesc desc = config.toEngineDesc();
        const int rc =
            _runner->runIos(_window.get(), _touch.get(), _gyro.get(), _fileSystem.get(), desc);
        if (rc != 0)
        {
            std::fprintf(stderr, "Sama: GameRunner::runIos failed (rc=%d)\n", rc);
            delete _runner;
            _runner = nullptr;
        }
    }

    // -- CADisplayLink (drives the per-frame tick) ------------------------
    // Frame rate cap is driven by the active tier's targetFPS:
    //   low/mid tiers cap at 30fps so the GPU has headroom and the device
    //   stays cool; high tier targets 60fps (or higher on ProMotion
    //   displays — we ask for the system max as the upper bound).
    //
    // CAFrameRateRange (iOS 15+) is the modern API.  preferredFramesPerSecond
    // alone is deprecated for high-rate displays; on a 120Hz iPad Pro the
    // legacy property would clamp to 60fps even with CADisableMinimumFrame
    // DurationOnPhone set.  CAFrameRateRangeMake(min, max, preferred) lets
    // us be explicit about the band the system can negotiate.
    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(onFrame:)];
    if (@available(iOS 15.0, *))
    {
        // CAFrameRateRange wants min<=preferred<=max with all three > 0.
        // High tier (60fps target) gets a 30..120Hz band so ProMotion
        // displays can negotiate up; low/mid (30fps target) get a tight
        // 30..30 band to keep the GPU cool.  CAFrameRateRangeDefault.max
        // is 0 and would throw NSInvalidArgumentException, hence the
        // explicit numeric upper bound.
        const float pref = static_cast<float>(targetFps);
        const float minFps = (targetFps >= 60) ? 30.0f : pref;
        const float maxFps = (targetFps >= 60) ? 120.0f : pref;
        self.displayLink.preferredFrameRateRange = CAFrameRateRangeMake(minFps, maxFps, pref);
    }
    else
    {
        self.displayLink.preferredFramesPerSecond = targetFps;
    }
    std::fprintf(stderr, "[Sama][iOS] CADisplayLink targetFps = %d\n", targetFps);
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

    // GameRunner first: shutdownIos calls game.onShutdown then engine.shutdown
    // (releases bgfx, framebuffers, shadow atlas, etc.).  Must run before the
    // platform pieces it references go away.
    if (_runner)
    {
        _runner->shutdownIos();
        delete _runner;
        _runner = nullptr;
    }

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
    // Eagerly re-publish the view pointer in case it changed (e.g. after
    // a scene reconnect).  Cheap and idempotent.
    if (_window)
    {
        engine::platform::ios::setGameView(_window->nativeView());
    }

    // Drive one logical frame.  tickIos returns false once the engine has
    // signalled exit (bgfx surface lost, etc.); we drop the display link
    // rather than spinning so the OS can suspend us cleanly.
    if (_runner)
    {
        if (!_runner->tickIos())
        {
            self.displayLink.paused = YES;
        }
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
