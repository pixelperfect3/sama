// ---------------------------------------------------------------------------
// Sama Engine — iOS Simulator smoke test (Phase A).
//
// Self-contained Objective-C++ entry point that proves bgfx Metal works on
// the iOS Simulator end-to-end:
//
//   * UIApplicationDelegate creates a UIWindow + UIViewController.
//   * The view controller installs a UIView whose backing layer is a
//     CAMetalLayer (via +layerClass).
//   * bgfx is initialized with init.platformData.nwh = (__bridge void*)layer.
//   * A CADisplayLink-driven render loop clears view 0 to the engine's
//     signature dark purple and prints "iOS Test" via bgfx::dbgTextPrintf.
//
// No engine_core, no IGame, no GLFW — Phase A's deliverable is "the build
// pipeline works and bgfx draws a frame."  Phase B (Agent B) introduces the
// real platform layer; Phase C/D add asset loading and packaging.
//
// Background colour: 0x443355FF (the engine's standard clear).  If you can
// see purple in the simulator, every link in the chain — CMake toolchain →
// bgfx Metal compile → CAMetalLayer wiring → render loop — is green.
// ---------------------------------------------------------------------------

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <cstdint>

namespace
{
constexpr uint32_t kSamaPurple = 0x443355FF;

// View ID for the clear pass.  bgfx allows up to 256 views; view 0 is fine
// for a single-pass demo.
constexpr bgfx::ViewId kClearView = 0;
}  // namespace

// ---------------------------------------------------------------------------
// SamaIosMetalView — UIView subclass whose backing layer is a CAMetalLayer.
// Returning [CAMetalLayer class] from +layerClass tells UIKit to allocate the
// layer once at view creation, so view.layer is already the right type by the
// time the view controller wires it up.
// ---------------------------------------------------------------------------
@interface SamaIosMetalView : UIView
@end

@implementation SamaIosMetalView
+ (Class)layerClass
{
    return [CAMetalLayer class];
}
@end

// ---------------------------------------------------------------------------
// SamaIosViewController — owns the Metal view, drives the render loop via
// CADisplayLink, and routes layout changes back to bgfx::reset().
// ---------------------------------------------------------------------------
@interface SamaIosViewController : UIViewController
@property(nonatomic, strong) CADisplayLink* displayLink;
@property(nonatomic, assign) BOOL bgfxInitialized;
@property(nonatomic, assign) uint32_t frameCount;
@property(nonatomic, assign) uint32_t bgfxWidth;
@property(nonatomic, assign) uint32_t bgfxHeight;
@end

@implementation SamaIosViewController

- (void)loadView
{
    self.view = [[SamaIosMetalView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.view.backgroundColor = [UIColor blackColor];
    self.view.contentScaleFactor = [[UIScreen mainScreen] nativeScale];
}

- (void)viewDidLoad
{
    [super viewDidLoad];

    CAMetalLayer* metalLayer = (CAMetalLayer*)self.view.layer;
    metalLayer.contentsScale = [[UIScreen mainScreen] nativeScale];
    metalLayer.framebufferOnly = YES;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    // Compute initial drawable size in physical pixels.
    CGSize boundsSize = self.view.bounds.size;
    CGFloat scale = self.view.contentScaleFactor;
    self.bgfxWidth = (uint32_t)(boundsSize.width * scale);
    self.bgfxHeight = (uint32_t)(boundsSize.height * scale);
    metalLayer.drawableSize = CGSizeMake(self.bgfxWidth, self.bgfxHeight);

    // Single-threaded mode: bgfx::renderFrame() must be called exactly once
    // before bgfx::init() so bgfx does NOT spawn its own render thread.
    // (Multi-threaded mode is incompatible with iOS's main-thread-only UIKit
    // contract — every CAMetalLayer touch has to come from the main thread.)
    bgfx::renderFrame();

    bgfx::Init init;
    init.type = bgfx::RendererType::Metal;
    init.platformData.nwh = (__bridge void*)metalLayer;
    init.resolution.width = self.bgfxWidth;
    init.resolution.height = self.bgfxHeight;
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init))
    {
        NSLog(@"SamaIosTest: bgfx::init() FAILED");
        return;
    }
    self.bgfxInitialized = YES;
    NSLog(@"SamaIosTest: bgfx::init OK — renderer=%s drawable=%ux%u scale=%.2f",
          bgfx::getRendererName(bgfx::getRendererType()), self.bgfxWidth, self.bgfxHeight,
          (double)scale);

    // Clear to Sama purple + depth, with a debug text overlay enabled.
    bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kSamaPurple, 1.0f, 0);
    bgfx::setViewRect(kClearView, 0, 0, (uint16_t)self.bgfxWidth, (uint16_t)self.bgfxHeight);
    bgfx::setDebug(BGFX_DEBUG_TEXT);

    // Drive bgfx::frame() from CADisplayLink (one tick per refresh).
    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(renderFrame)];
    [self.displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];

    if (!self.bgfxInitialized)
        return;

    CAMetalLayer* metalLayer = (CAMetalLayer*)self.view.layer;
    CGSize boundsSize = self.view.bounds.size;
    CGFloat scale = self.view.contentScaleFactor;
    uint32_t newW = (uint32_t)(boundsSize.width * scale);
    uint32_t newH = (uint32_t)(boundsSize.height * scale);
    if (newW == self.bgfxWidth && newH == self.bgfxHeight)
        return;

    self.bgfxWidth = newW;
    self.bgfxHeight = newH;
    metalLayer.drawableSize = CGSizeMake(newW, newH);
    bgfx::reset(newW, newH, BGFX_RESET_VSYNC);
    bgfx::setViewRect(kClearView, 0, 0, (uint16_t)newW, (uint16_t)newH);
}

- (void)renderFrame
{
    if (!self.bgfxInitialized)
        return;

    self.frameCount++;

    // Touch the view so it actually clears (bgfx skips views with no submits).
    bgfx::touch(kClearView);

    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(0, 1, 0x4f, "iOS Test");
    bgfx::dbgTextPrintf(0, 2, 0x0f, "frame %u  drawable %ux%u  renderer %s",
                        (unsigned)self.frameCount, (unsigned)self.bgfxWidth,
                        (unsigned)self.bgfxHeight, bgfx::getRendererName(bgfx::getRendererType()));
    bgfx::dbgTextPrintf(0, 3, 0x0e, "Sama Engine - Phase A smoke test");

    bgfx::frame();
}

- (void)dealloc
{
    [self.displayLink invalidate];
    if (self.bgfxInitialized)
    {
        bgfx::shutdown();
    }
}

@end

// ---------------------------------------------------------------------------
// SamaIosAppDelegate — bare-minimum UIApplicationDelegate.  Creates a
// UIWindow rooted at our view controller; no storyboards, no scenes (Info.plist
// has UIApplicationSupportsMultipleScenes=NO so the legacy delegate path is
// used).
// ---------------------------------------------------------------------------
@interface SamaIosAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation SamaIosAppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [[SamaIosViewController alloc] init];
    [self.window makeKeyAndVisible];
    NSLog(@"SamaIosTest: launched");
    return YES;
}

@end

// ---------------------------------------------------------------------------
// main — vanilla UIApplicationMain pointing at SamaIosAppDelegate.  No
// argument parsing; the simulator launcher passes the standard set.
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([SamaIosAppDelegate class]));
    }
}
