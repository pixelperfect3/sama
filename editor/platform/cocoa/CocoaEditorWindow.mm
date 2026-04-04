#include "editor/platform/cocoa/CocoaEditorWindow.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

// ---------------------------------------------------------------------------
// EditorMetalView -- NSView subclass backed by a CAMetalLayer.
// ---------------------------------------------------------------------------

@interface EditorMetalView : NSView
@property(nonatomic, assign) double scrollDeltaY;
@end

@implementation EditorMetalView

- (BOOL)wantsUpdateLayer
{
    return YES;
}

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (CALayer*)makeBackingLayer
{
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.device = MTLCreateSystemDefaultDevice();
    return layer;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)scrollWheel:(NSEvent*)event
{
    _scrollDeltaY += [event scrollingDeltaY];
}

@end

// ---------------------------------------------------------------------------
// EditorWindowDelegate -- handles window close.
// ---------------------------------------------------------------------------

@interface EditorWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) BOOL windowShouldCloseFlag;
@end

@implementation EditorWindowDelegate

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        _windowShouldCloseFlag = NO;
    }
    return self;
}

- (BOOL)windowShouldClose:(id)sender
{
    _windowShouldCloseFlag = YES;
    return NO;  // We handle the close ourselves.
}

@end

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

namespace engine::editor
{

struct CocoaEditorWindow::Impl
{
    NSWindow* window = nil;
    EditorMetalView* metalView = nil;
    EditorWindowDelegate* windowDelegate = nil;
    CAMetalLayer* metalLayer = nil;

    uint32_t width = 0;
    uint32_t height = 0;

    // Mouse state
    double mouseX = 0.0;
    double mouseY = 0.0;
    double prevMouseX = 0.0;
    double prevMouseY = 0.0;
    double deltaX = 0.0;
    double deltaY = 0.0;
    double scrollY = 0.0;
    bool leftDown = false;
    bool rightDown = false;
    bool firstFrame = true;
};

CocoaEditorWindow::CocoaEditorWindow() : impl_(std::make_unique<Impl>()) {}

CocoaEditorWindow::~CocoaEditorWindow()
{
    shutdown();
}

bool CocoaEditorWindow::init(uint32_t w, uint32_t h, const char* title)
{
    @autoreleasepool
    {
        // Ensure NSApplication is initialized (poll-based, no xib).
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        // Create the window.
        NSRect frame = NSMakeRect(100, 100, w, h);
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

        impl_->window = [[NSWindow alloc] initWithContentRect:frame
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];

        if (!impl_->window)
            return false;

        impl_->windowDelegate = [[EditorWindowDelegate alloc] init];
        [impl_->window setDelegate:impl_->windowDelegate];

        NSString* nsTitle = [NSString stringWithUTF8String:(title ? title : "Sama Editor")];
        [impl_->window setTitle:nsTitle];

        // Create the Metal-backed view.
        impl_->metalView = [[EditorMetalView alloc] initWithFrame:frame];
        [impl_->metalView setWantsLayer:YES];
        [impl_->window setContentView:impl_->metalView];

        impl_->metalLayer = (CAMetalLayer*)[impl_->metalView layer];

        // Set content scale for HiDPI.
        CGFloat scale = [impl_->window backingScaleFactor];
        impl_->metalLayer.contentsScale = scale;

        impl_->width = w;
        impl_->height = h;

        // Show the window.
        [impl_->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        return true;
    }
}

void CocoaEditorWindow::shutdown()
{
    @autoreleasepool
    {
        if (impl_->window)
        {
            [impl_->window setDelegate:nil];
            [impl_->window close];
            impl_->window = nil;
        }
        impl_->metalView = nil;
        impl_->windowDelegate = nil;
        impl_->metalLayer = nil;
    }
}

bool CocoaEditorWindow::shouldClose() const
{
    if (!impl_->windowDelegate)
        return true;
    return impl_->windowDelegate.windowShouldCloseFlag;
}

void CocoaEditorWindow::pollEvents()
{
    @autoreleasepool
    {
        // Drain all pending events (poll-based, not blocking).
        while (true)
        {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:nil
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (!event)
                break;
            [NSApp sendEvent:event];
            [NSApp updateWindows];
        }

        // Update window dimensions (may have changed due to resize).
        NSRect contentRect = [impl_->metalView bounds];
        impl_->width = static_cast<uint32_t>(contentRect.size.width);
        impl_->height = static_cast<uint32_t>(contentRect.size.height);

        // Update Metal layer drawable size.
        CGFloat scale = [impl_->window backingScaleFactor];
        impl_->metalLayer.contentsScale = scale;
        impl_->metalLayer.drawableSize =
            CGSizeMake(contentRect.size.width * scale, contentRect.size.height * scale);

        // Update mouse state.
        NSPoint mouseLoc = [impl_->window mouseLocationOutsideOfEventStream];
        // Convert from bottom-left origin to top-left origin.
        double mx = mouseLoc.x;
        double my = contentRect.size.height - mouseLoc.y;

        impl_->prevMouseX = impl_->mouseX;
        impl_->prevMouseY = impl_->mouseY;
        impl_->mouseX = mx;
        impl_->mouseY = my;

        if (impl_->firstFrame)
        {
            impl_->deltaX = 0.0;
            impl_->deltaY = 0.0;
            impl_->firstFrame = false;
        }
        else
        {
            impl_->deltaX = impl_->mouseX - impl_->prevMouseX;
            impl_->deltaY = impl_->mouseY - impl_->prevMouseY;
        }

        // Mouse buttons.
        NSUInteger buttons = [NSEvent pressedMouseButtons];
        impl_->leftDown = (buttons & (1 << 0)) != 0;
        impl_->rightDown = (buttons & (1 << 1)) != 0;

        // Scroll delta (accumulated by the view, consumed here).
        impl_->scrollY = impl_->metalView.scrollDeltaY;
        impl_->metalView.scrollDeltaY = 0.0;
    }
}

void* CocoaEditorWindow::nativeHandle() const
{
    return (__bridge void*)impl_->window;
}

void* CocoaEditorWindow::nativeLayer() const
{
    return (__bridge void*)impl_->metalLayer;
}

uint32_t CocoaEditorWindow::width() const
{
    return impl_->width;
}

uint32_t CocoaEditorWindow::height() const
{
    return impl_->height;
}

uint32_t CocoaEditorWindow::framebufferWidth() const
{
    CGFloat scale = impl_->window ? [impl_->window backingScaleFactor] : 1.0;
    return static_cast<uint32_t>(impl_->width * scale);
}

uint32_t CocoaEditorWindow::framebufferHeight() const
{
    CGFloat scale = impl_->window ? [impl_->window backingScaleFactor] : 1.0;
    return static_cast<uint32_t>(impl_->height * scale);
}

float CocoaEditorWindow::contentScale() const
{
    if (impl_->window)
        return static_cast<float>([impl_->window backingScaleFactor]);
    return 1.0f;
}

double CocoaEditorWindow::mouseX() const
{
    return impl_->mouseX;
}

double CocoaEditorWindow::mouseY() const
{
    return impl_->mouseY;
}

double CocoaEditorWindow::mouseDeltaX() const
{
    return impl_->deltaX;
}

double CocoaEditorWindow::mouseDeltaY() const
{
    return impl_->deltaY;
}

double CocoaEditorWindow::scrollDeltaY() const
{
    return impl_->scrollY;
}

bool CocoaEditorWindow::isLeftMouseDown() const
{
    return impl_->leftDown;
}

bool CocoaEditorWindow::isRightMouseDown() const
{
    return impl_->rightDown;
}

}  // namespace engine::editor
