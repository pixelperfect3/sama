#include "editor/platform/cocoa/CocoaEditorWindow.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "editor/platform/cocoa/CocoaConsoleView.h"
#include "editor/platform/cocoa/CocoaHierarchyView.h"
#include "editor/platform/cocoa/CocoaPropertiesView.h"
#include "editor/platform/cocoa/CocoaResourceView.h"

// ---------------------------------------------------------------------------
// EditorMetalView -- NSView subclass backed by a CAMetalLayer (viewport only).
// ---------------------------------------------------------------------------

@interface EditorMetalView : NSView
{
    BOOL keyPressed_[256];
    BOOL modCommand_;
    BOOL modShift_;
    BOOL modControl_;
    BOOL modOption_;
}
@property(nonatomic, assign) double scrollDeltaY;
- (BOOL)wasKeyPressed:(uint8_t)keyCode;
- (BOOL)isCommandDown;
- (BOOL)isShiftDown;
- (BOOL)isControlDown;
- (BOOL)isOptionDown;
- (void)clearKeyState;
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

// Map macOS virtual key codes to ASCII-ish codes.
static uint8_t mapKeyCode(unsigned short vk)
{
    switch (vk)
    {
        case 0x00:
            return 'A';
        case 0x01:
            return 'S';
        case 0x02:
            return 'D';
        case 0x03:
            return 'F';
        case 0x05:
            return 'G';
        case 0x04:
            return 'H';
        case 0x06:
            return 'Z';
        case 0x07:
            return 'X';
        case 0x08:
            return 'C';
        case 0x09:
            return 'V';
        case 0x0B:
            return 'B';
        case 0x0D:
            return 'W';
        case 0x0E:
            return 'E';
        case 0x0F:
            return 'R';
        case 0x10:
            return 'Y';
        case 0x11:
            return 'T';
        case 0x12:
            return '1';
        case 0x13:
            return '2';
        case 0x14:
            return '3';
        case 0x15:
            return '4';
        case 0x17:
            return '5';
        case 0x16:
            return '6';
        case 0x1A:
            return '7';
        case 0x1C:
            return '8';
        case 0x19:
            return '9';
        case 0x1D:
            return '0';
        case 0x1E:
            return ']';
        case 0x1F:
            return 'O';
        case 0x20:
            return 'U';
        case 0x22:
            return 'I';
        case 0x23:
            return 'P';
        case 0x25:
            return 'L';
        case 0x26:
            return 'J';
        case 0x28:
            return 'K';
        case 0x2C:
            return '/';
        case 0x2D:
            return 'N';
        case 0x2E:
            return 'M';
        case 0x2F:
            return '.';
        case 0x31:
            return ' ';  // Space
        case 0x7B:
            return 0x80;  // Left arrow
        case 0x7C:
            return 0x81;  // Right arrow
        case 0x7D:
            return 0x82;  // Down arrow
        case 0x7E:
            return 0x83;  // Up arrow
        case 0x24:
            return 0x0D;  // Return/Enter
        case 0x33:
            return 0x08;  // Delete/Backspace
        case 0x35:
            return 0x1B;  // Escape
        case 0x30:
            return 0x09;  // Tab
        case 0x18:
            return '=';
        case 0x1B:
            return '-';
        case 0x32:
            return '`';  // Tilde/backtick
        case 0x75:
            return 0x7F;  // Forward delete
        default:
            return 0;
    }
}

- (void)keyDown:(NSEvent*)event
{
    uint8_t code = mapKeyCode([event keyCode]);
    if (code > 0)
    {
        keyPressed_[code] = YES;
    }
}

- (void)flagsChanged:(NSEvent*)event
{
    NSEventModifierFlags flags = [event modifierFlags];
    modCommand_ = (flags & NSEventModifierFlagCommand) != 0;
    modShift_ = (flags & NSEventModifierFlagShift) != 0;
    modControl_ = (flags & NSEventModifierFlagControl) != 0;
    modOption_ = (flags & NSEventModifierFlagOption) != 0;
}

- (BOOL)wasKeyPressed:(uint8_t)keyCode
{
    return keyPressed_[keyCode];
}

- (BOOL)isCommandDown
{
    return modCommand_;
}

- (BOOL)isShiftDown
{
    return modShift_;
}

- (BOOL)isControlDown
{
    return modControl_;
}

- (BOOL)isOptionDown
{
    return modOption_;
}

- (void)clearKeyState
{
    memset(keyPressed_, 0, sizeof(keyPressed_));
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
// SplitViewDelegate -- constrains panel sizes.
// ---------------------------------------------------------------------------

@interface EditorSplitDelegate : NSObject <NSSplitViewDelegate>
@property(nonatomic, assign) CGFloat leftMinWidth;
@property(nonatomic, assign) CGFloat rightMinWidth;
@property(nonatomic, assign) CGFloat bottomMinHeight;
@end

@implementation EditorSplitDelegate

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        _leftMinWidth = 150.0;
        _rightMinWidth = 180.0;
        _bottomMinHeight = 80.0;
    }
    return self;
}

- (CGFloat)splitView:(NSSplitView*)splitView
    constrainMinCoordinate:(CGFloat)proposedMinimumPosition
               ofSubviewAt:(NSInteger)dividerIndex
{
    if (splitView.isVertical)
    {
        // Horizontal split (left | center | right).
        if (dividerIndex == 0)
            return _leftMinWidth;
        return proposedMinimumPosition;
    }
    else
    {
        // Vertical split (top | bottom).
        return proposedMinimumPosition;
    }
}

- (CGFloat)splitView:(NSSplitView*)splitView
    constrainMaxCoordinate:(CGFloat)proposedMaximumPosition
               ofSubviewAt:(NSInteger)dividerIndex
{
    if (splitView.isVertical)
    {
        // Horizontal: constrain last divider to leave room for right panel.
        if (dividerIndex == 1)
            return proposedMaximumPosition - _rightMinWidth;
        return proposedMaximumPosition;
    }
    else
    {
        // Vertical: constrain divider to leave room for bottom panel.
        return proposedMaximumPosition - _bottomMinHeight;
    }
}

- (BOOL)splitView:(NSSplitView*)splitView canCollapseSubview:(NSView*)subview
{
    return NO;
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
    EditorSplitDelegate* hSplitDelegate = nil;
    EditorSplitDelegate* vSplitDelegate = nil;
    CAMetalLayer* metalLayer = nil;

    // Split views.
    NSSplitView* verticalSplit = nil;    // top area + bottom console
    NSSplitView* horizontalSplit = nil;  // left + center + right

    // Native panel views.
    std::unique_ptr<CocoaHierarchyView> hierarchyView;
    std::unique_ptr<CocoaPropertiesView> propertiesView;
    std::unique_ptr<CocoaConsoleView> consoleView;
    std::unique_ptr<CocoaResourceView> resourceView;

    // Bottom tab view for Console + Resources.
    NSTabView* bottomTabView = nil;

    uint32_t windowWidth = 0;
    uint32_t windowHeight = 0;

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
    bool mouseOverViewport = false;
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

        // -- Native menu bar -----------------------------------------------------
        {
            NSMenu* menuBar = [[NSMenu alloc] init];

            // App menu (required for Quit to work)
            NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
            NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Sama Editor"];
            [appMenu addItemWithTitle:@"About Sama Editor"
                               action:@selector(orderFrontStandardAboutPanel:)
                        keyEquivalent:@""];
            [appMenu addItem:[NSMenuItem separatorItem]];
            [appMenu addItemWithTitle:@"Quit Sama Editor"
                               action:@selector(terminate:)
                        keyEquivalent:@"q"];
            appMenuItem.submenu = appMenu;
            [menuBar addItem:appMenuItem];

            // File menu
            NSMenuItem* fileMenuItem = [[NSMenuItem alloc] init];
            NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
            [fileMenu addItemWithTitle:@"New Scene" action:nil keyEquivalent:@"n"];
            [fileMenu addItemWithTitle:@"Open Scene..." action:nil keyEquivalent:@"o"];
            [fileMenu addItem:[NSMenuItem separatorItem]];
            [fileMenu addItemWithTitle:@"Save Scene" action:nil keyEquivalent:@"s"];
            NSMenuItem* saveAs = [fileMenu addItemWithTitle:@"Save Scene As..."
                                                     action:nil
                                              keyEquivalent:@"S"];
            saveAs.keyEquivalentModifierMask =
                NSEventModifierFlagCommand | NSEventModifierFlagShift;
            fileMenuItem.submenu = fileMenu;
            [menuBar addItem:fileMenuItem];

            // Edit menu
            NSMenuItem* editMenuItem = [[NSMenuItem alloc] init];
            NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
            [editMenu addItemWithTitle:@"Undo" action:nil keyEquivalent:@"z"];
            NSMenuItem* redo = [editMenu addItemWithTitle:@"Redo"
                                                   action:nil
                                            keyEquivalent:@"Z"];
            redo.keyEquivalentModifierMask =
                NSEventModifierFlagCommand | NSEventModifierFlagShift;
            [editMenu addItem:[NSMenuItem separatorItem]];
            [editMenu addItemWithTitle:@"Delete" action:nil keyEquivalent:@""];
            editMenuItem.submenu = editMenu;
            [menuBar addItem:editMenuItem];

            // Entity menu
            NSMenuItem* entityMenuItem = [[NSMenuItem alloc] init];
            NSMenu* entityMenu = [[NSMenu alloc] initWithTitle:@"Entity"];
            [entityMenu addItemWithTitle:@"Create Empty" action:nil keyEquivalent:@""];
            [entityMenu addItemWithTitle:@"Create Cube" action:nil keyEquivalent:@""];
            [entityMenu addItemWithTitle:@"Create Light" action:nil keyEquivalent:@""];
            entityMenuItem.submenu = entityMenu;
            [menuBar addItem:entityMenuItem];

            // View menu
            NSMenuItem* viewMenuItem = [[NSMenuItem alloc] init];
            NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
            [viewMenu addItemWithTitle:@"Toggle Console" action:nil keyEquivalent:@""];
            [viewMenu addItemWithTitle:@"Toggle Resources" action:nil keyEquivalent:@""];
            viewMenuItem.submenu = viewMenu;
            [menuBar addItem:viewMenuItem];

            [NSApp setMainMenu:menuBar];
        }

        impl_->windowWidth = w;
        impl_->windowHeight = h;

        // -- Create native panel views ----------------------------------------
        impl_->hierarchyView = std::make_unique<CocoaHierarchyView>();
        impl_->propertiesView = std::make_unique<CocoaPropertiesView>();
        impl_->consoleView = std::make_unique<CocoaConsoleView>();
        impl_->resourceView = std::make_unique<CocoaResourceView>();

        // -- Create the Metal viewport view -----------------------------------
        impl_->metalView = [[EditorMetalView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
        [impl_->metalView setWantsLayer:YES];
        impl_->metalLayer = (CAMetalLayer*)[impl_->metalView layer];

        CGFloat scale = [impl_->window backingScaleFactor];
        impl_->metalLayer.contentsScale = scale;

        // -- Build NSSplitView layout -----------------------------------------
        // Vertical split: [top area | bottom console]
        impl_->vSplitDelegate = [[EditorSplitDelegate alloc] init];
        impl_->verticalSplit = [[NSSplitView alloc] initWithFrame:frame];
        impl_->verticalSplit.vertical = NO;  // horizontal divider = vertical split
        impl_->verticalSplit.dividerStyle = NSSplitViewDividerStyleThin;
        impl_->verticalSplit.delegate = impl_->vSplitDelegate;
        impl_->verticalSplit.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

        // Horizontal split for top area: [left hierarchy | center viewport | right properties]
        impl_->hSplitDelegate = [[EditorSplitDelegate alloc] init];
        impl_->horizontalSplit = [[NSSplitView alloc] initWithFrame:NSMakeRect(0, 0, w, h - 150)];
        impl_->horizontalSplit.vertical = YES;  // vertical dividers = horizontal split
        impl_->horizontalSplit.dividerStyle = NSSplitViewDividerStyleThin;
        impl_->horizontalSplit.delegate = impl_->hSplitDelegate;

        // Helper: wrap a view in a container with a title bar.
        auto wrapWithTitle = [](NSView* contentView, NSString* title) -> NSView*
        {
            NSView* container = [[NSView alloc] initWithFrame:contentView.frame];
            container.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

            // Title label.
            NSTextField* label = [NSTextField labelWithString:title];
            label.font = [NSFont boldSystemFontOfSize:11.0];
            label.textColor = [NSColor secondaryLabelColor];
            label.translatesAutoresizingMaskIntoConstraints = NO;

            // Separator line.
            NSBox* separator = [[NSBox alloc] init];
            separator.boxType = NSBoxSeparator;
            separator.translatesAutoresizingMaskIntoConstraints = NO;

            contentView.translatesAutoresizingMaskIntoConstraints = NO;

            [container addSubview:label];
            [container addSubview:separator];
            [container addSubview:contentView];

            [NSLayoutConstraint activateConstraints:@[
                [label.topAnchor constraintEqualToAnchor:container.topAnchor constant:4],
                [label.leadingAnchor constraintEqualToAnchor:container.leadingAnchor constant:8],
                [label.trailingAnchor constraintEqualToAnchor:container.trailingAnchor constant:-8],

                [separator.topAnchor constraintEqualToAnchor:label.bottomAnchor constant:4],
                [separator.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
                [separator.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],

                [contentView.topAnchor constraintEqualToAnchor:separator.bottomAnchor],
                [contentView.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
                [contentView.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
                [contentView.bottomAnchor constraintEqualToAnchor:container.bottomAnchor],
            ]];

            return container;
        };

        // Get the native NSView* for each panel, wrapped with titles.
        NSView* leftView =
            wrapWithTitle((__bridge NSView*)impl_->hierarchyView->nativeView(), @"Scene Hierarchy");
        NSView* rightView =
            wrapWithTitle((__bridge NSView*)impl_->propertiesView->nativeView(), @"Properties");

        // Build a tabbed bottom panel (Console + Resources).
        impl_->bottomTabView = [[NSTabView alloc] initWithFrame:NSMakeRect(0, 0, w, 150)];
        impl_->bottomTabView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        impl_->bottomTabView.tabViewType = NSTopTabsBezelBorder;
        impl_->bottomTabView.controlSize = NSControlSizeSmall;
        impl_->bottomTabView.font = [NSFont systemFontOfSize:10.0];

        {
            NSTabViewItem* consoleTab = [[NSTabViewItem alloc] initWithIdentifier:@"console"];
            consoleTab.label = @"Console";
            consoleTab.view = (__bridge NSView*)impl_->consoleView->nativeView();
            [impl_->bottomTabView addTabViewItem:consoleTab];
        }
        {
            NSTabViewItem* resourceTab = [[NSTabViewItem alloc] initWithIdentifier:@"resources"];
            resourceTab.label = @"Resources";
            resourceTab.view = (__bridge NSView*)impl_->resourceView->nativeView();
            [impl_->bottomTabView addTabViewItem:resourceTab];
        }

        NSView* bottomView = impl_->bottomTabView;

        // Add panels to horizontal split: left, center (viewport), right.
        [impl_->horizontalSplit addSubview:leftView];
        [impl_->horizontalSplit addSubview:impl_->metalView];
        [impl_->horizontalSplit addSubview:rightView];

        // Set initial panel widths.
        CGFloat leftWidth = 200.0;
        CGFloat rightWidth = 250.0;
        CGFloat centerWidth = w - leftWidth - rightWidth - 2.0;  // 2px for dividers

        [leftView setFrameSize:NSMakeSize(leftWidth, h - 150)];
        [impl_->metalView setFrameSize:NSMakeSize(centerWidth, h - 150)];
        [rightView setFrameSize:NSMakeSize(rightWidth, h - 150)];

        // Add top and bottom to vertical split.
        [impl_->verticalSplit addSubview:impl_->horizontalSplit];
        [impl_->verticalSplit addSubview:bottomView];

        // Set initial heights.
        CGFloat bottomHeight = 150.0;
        CGFloat topHeight = h - bottomHeight - 1.0;
        [impl_->horizontalSplit setFrameSize:NSMakeSize(w, topHeight)];
        [bottomView setFrameSize:NSMakeSize(w, bottomHeight)];

        // Set divider positions.
        [impl_->verticalSplit adjustSubviews];
        [impl_->horizontalSplit adjustSubviews];

        // Set as content view.
        [impl_->window setContentView:impl_->verticalSplit];

        // Show the window.
        [impl_->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        // Make the metal view first responder for keyboard events.
        [impl_->window makeFirstResponder:impl_->metalView];

        return true;
    }
}

void CocoaEditorWindow::shutdown()
{
    @autoreleasepool
    {
        impl_->hierarchyView.reset();
        impl_->propertiesView.reset();
        impl_->consoleView.reset();
        impl_->resourceView.reset();
        impl_->bottomTabView = nil;

        if (impl_->window)
        {
            [impl_->window setDelegate:nil];
            [impl_->window close];
            impl_->window = nil;
        }
        impl_->metalView = nil;
        impl_->windowDelegate = nil;
        impl_->hSplitDelegate = nil;
        impl_->vSplitDelegate = nil;
        impl_->metalLayer = nil;
        impl_->verticalSplit = nil;
        impl_->horizontalSplit = nil;
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
        // Clear per-frame key state before processing new events.
        [impl_->metalView clearKeyState];

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

        // Update window dimensions.
        NSRect contentRect = [impl_->verticalSplit bounds];
        impl_->windowWidth = static_cast<uint32_t>(contentRect.size.width);
        impl_->windowHeight = static_cast<uint32_t>(contentRect.size.height);

        // Update Metal layer drawable size to match viewport panel.
        CGFloat scale = [impl_->window backingScaleFactor];
        impl_->metalLayer.contentsScale = scale;
        NSRect vpBounds = [impl_->metalView bounds];
        impl_->metalLayer.drawableSize =
            CGSizeMake(vpBounds.size.width * scale, vpBounds.size.height * scale);

        // Update mouse state relative to viewport.
        NSPoint mouseLoc = [impl_->window mouseLocationOutsideOfEventStream];
        // Convert to viewport-local coordinates.
        NSPoint vpLocal = [impl_->metalView convertPoint:mouseLoc fromView:nil];
        NSRect vpFrame = [impl_->metalView bounds];

        impl_->mouseOverViewport = NSPointInRect(vpLocal, vpFrame);

        // Mouse coords relative to viewport (top-left origin).
        double mx = vpLocal.x;
        double my = vpFrame.size.height - vpLocal.y;

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

        // Scroll delta (accumulated by the metal view, consumed here).
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
    return impl_->windowWidth;
}

uint32_t CocoaEditorWindow::height() const
{
    return impl_->windowHeight;
}

uint32_t CocoaEditorWindow::framebufferWidth() const
{
    return viewportFramebufferWidth();
}

uint32_t CocoaEditorWindow::framebufferHeight() const
{
    return viewportFramebufferHeight();
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

bool CocoaEditorWindow::isKeyPressed(uint8_t keyCode) const
{
    if (!impl_->metalView)
        return false;
    return [impl_->metalView wasKeyPressed:keyCode];
}

bool CocoaEditorWindow::isCommandDown() const
{
    if (!impl_->metalView)
        return false;
    return [impl_->metalView isCommandDown];
}

bool CocoaEditorWindow::isShiftDown() const
{
    if (!impl_->metalView)
        return false;
    return [impl_->metalView isShiftDown];
}

bool CocoaEditorWindow::isControlDown() const
{
    if (!impl_->metalView)
        return false;
    return [impl_->metalView isControlDown];
}

bool CocoaEditorWindow::isOptionDown() const
{
    if (!impl_->metalView)
        return false;
    return [impl_->metalView isOptionDown];
}

// --- Viewport-specific dimensions -------------------------------------------

uint32_t CocoaEditorWindow::viewportWidth() const
{
    if (!impl_->metalView)
        return 0;
    @autoreleasepool
    {
        NSRect bounds = [impl_->metalView bounds];
        return static_cast<uint32_t>(bounds.size.width);
    }
}

uint32_t CocoaEditorWindow::viewportHeight() const
{
    if (!impl_->metalView)
        return 0;
    @autoreleasepool
    {
        NSRect bounds = [impl_->metalView bounds];
        return static_cast<uint32_t>(bounds.size.height);
    }
}

uint32_t CocoaEditorWindow::viewportFramebufferWidth() const
{
    if (!impl_->metalView || !impl_->window)
        return 0;
    @autoreleasepool
    {
        CGFloat scale = [impl_->window backingScaleFactor];
        NSRect bounds = [impl_->metalView bounds];
        return static_cast<uint32_t>(bounds.size.width * scale);
    }
}

uint32_t CocoaEditorWindow::viewportFramebufferHeight() const
{
    if (!impl_->metalView || !impl_->window)
        return 0;
    @autoreleasepool
    {
        CGFloat scale = [impl_->window backingScaleFactor];
        NSRect bounds = [impl_->metalView bounds];
        return static_cast<uint32_t>(bounds.size.height * scale);
    }
}

bool CocoaEditorWindow::isMouseOverViewport() const
{
    return impl_->mouseOverViewport;
}

// --- Native file dialogs ----------------------------------------------------

std::string CocoaEditorWindow::showSaveDialog(const char* defaultName, const char* extension)
{
    @autoreleasepool
    {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.allowedContentTypes = @[ [UTType typeWithFilenameExtension:@(extension)] ];
        panel.nameFieldStringValue = @(defaultName);
        if ([panel runModal] == NSModalResponseOK)
        {
            return std::string([[panel.URL path] UTF8String]);
        }
        return {};
    }
}

std::string CocoaEditorWindow::showOpenDialog(const char* extension)
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.allowedContentTypes = @[ [UTType typeWithFilenameExtension:@(extension)] ];
        panel.allowsMultipleSelection = NO;
        if ([panel runModal] == NSModalResponseOK)
        {
            return std::string([[panel.URLs[0] path] UTF8String]);
        }
        return {};
    }
}

void CocoaEditorWindow::setWindowTitle(const char* title)
{
    @autoreleasepool
    {
        [impl_->window setTitle:@(title)];
    }
}

// --- Native panel views -----------------------------------------------------

CocoaHierarchyView* CocoaEditorWindow::hierarchyView() const
{
    return impl_->hierarchyView.get();
}

CocoaPropertiesView* CocoaEditorWindow::propertiesView() const
{
    return impl_->propertiesView.get();
}

CocoaConsoleView* CocoaEditorWindow::consoleView() const
{
    return impl_->consoleView.get();
}

CocoaResourceView* CocoaEditorWindow::resourceView() const
{
    return impl_->resourceView.get();
}

}  // namespace engine::editor
