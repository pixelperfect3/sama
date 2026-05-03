#include "editor/platform/cocoa/CocoaEditorWindow.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "editor/platform/cocoa/CocoaAnimationView.h"
#include "editor/platform/cocoa/CocoaConsoleView.h"
#include "editor/platform/cocoa/CocoaHierarchyView.h"
#include "editor/platform/cocoa/CocoaPropertiesView.h"
#include "editor/platform/cocoa/CocoaResourceView.h"

// ---------------------------------------------------------------------------
// EditorMetalView -- NSView subclass backed by a CAMetalLayer (viewport only).
// ---------------------------------------------------------------------------

@interface EditorMetalView : NSView
{
@public
    BOOL keyPressed_[256];
    BOOL modCommand_;
    BOOL modShift_;
    BOOL modControl_;
    BOOL modOption_;
}
@property(nonatomic, assign) double scrollDeltaY;
@property(nonatomic, assign) BOOL leftMouseHeld;
@property(nonatomic, assign) BOOL rightMouseHeld;
@property(nonatomic, assign) double mouseMoveAccumX;
@property(nonatomic, assign) double mouseMoveAccumY;
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

// Track mouse button state via events — more reliable than pressedMouseButtons
// on trackpads and with system right-click settings.
- (void)mouseDown:(NSEvent*)event
{
    _leftMouseHeld = YES;
}

- (void)mouseUp:(NSEvent*)event
{
    _leftMouseHeld = NO;
}

- (void)mouseDragged:(NSEvent*)event
{
    _mouseMoveAccumX += [event deltaX];
    _mouseMoveAccumY += [event deltaY];
}

- (void)rightMouseDown:(NSEvent*)event
{
    _rightMouseHeld = YES;
    // Don't call super — suppresses context menu.
}

- (void)rightMouseDragged:(NSEvent*)event
{
    _mouseMoveAccumX += [event deltaX];
    _mouseMoveAccumY += [event deltaY];
}

- (void)rightMouseUp:(NSEvent*)event
{
    _rightMouseHeld = NO;
}

// Suppress context menu from menu key.
- (NSMenu*)menuForEvent:(NSEvent*)event
{
    return nil;
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
// MenuHandler -- routes menu item clicks to a C++ callback via tag IDs.
// ---------------------------------------------------------------------------

typedef void (*MenuActionFn)(const char* action);
static MenuActionFn s_menuActionCallback = nullptr;

@interface EditorMenuHandler : NSObject
- (void)menuAction:(NSMenuItem*)sender;
@end

@implementation EditorMenuHandler
- (void)menuAction:(NSMenuItem*)sender
{
    if (!s_menuActionCallback)
        return;
    // Use the menu item's tag to identify the action.
    // Tags: 1=new, 2=open, 3=save, 4=saveAs, 5=undo, 6=redo, 7=delete,
    //        8=createEmpty, 9=createCube, 10=createLight
    switch (sender.tag)
    {
        case 1:
            s_menuActionCallback("new_scene");
            break;
        case 2:
            s_menuActionCallback("open_scene");
            break;
        case 3:
            s_menuActionCallback("save_scene");
            break;
        case 4:
            s_menuActionCallback("save_scene_as");
            break;
        case 5:
            s_menuActionCallback("undo");
            break;
        case 6:
            s_menuActionCallback("redo");
            break;
        case 7:
            s_menuActionCallback("delete");
            break;
        case 8:
            s_menuActionCallback("create_empty");
            break;
        case 9:
            s_menuActionCallback("create_cube");
            break;
        case 10:
            s_menuActionCallback("create_light");
            break;
        case 11:
            s_menuActionCallback("import_asset");
            break;
        case 20:
            s_menuActionCallback("add_component:directional_light");
            break;
        case 21:
            s_menuActionCallback("add_component:point_light");
            break;
        case 22:
            s_menuActionCallback("add_component:mesh");
            break;
        case 23:
            s_menuActionCallback("add_component:rigid_body");
            break;
        case 24:
            s_menuActionCallback("add_component:box_collider");
            break;
        case 30:
            s_menuActionCallback("load_environment");
            break;
        case 31:
            s_menuActionCallback("load_environment_hdr");
            break;
        case 32:
            s_menuActionCallback("load_environment_cubemap");
            break;
        case 33:
            s_menuActionCallback("reset_environment");
            break;
        case 40:
            s_menuActionCallback("build_android_low");
            break;
        case 41:
            s_menuActionCallback("build_android_mid");
            break;
        case 42:
            s_menuActionCallback("build_android_high");
            break;
        case 43:
            s_menuActionCallback("build_android_run");
            break;
        case 44:
            s_menuActionCallback("build_android_settings");
            break;
        default:
            break;
    }
}

// Required: tells macOS all menu items are valid (not grayed out).
- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    return YES;
}
@end

// ---------------------------------------------------------------------------
// BuildCancelTarget -- bridges the status bar's Cancel NSButton click to a
// C++ std::function held by CocoaEditorWindow::Impl.
// ---------------------------------------------------------------------------

@interface EditorBuildCancelTarget : NSObject
{
@public
    engine::editor::CocoaEditorWindow::BuildCancelHandler* handlerPtr;
}
- (void)cancelClicked:(id)sender;
@end

@implementation EditorBuildCancelTarget
- (void)cancelClicked:(id)sender
{
    (void)sender;
    if (handlerPtr && *handlerPtr)
    {
        (*handlerPtr)();
    }
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
    EditorMenuHandler* menuHandler = nil;
    id keyMonitor = nil;  // NSEvent local monitor for window-level key events
    CAMetalLayer* metalLayer = nil;

    // Split views.
    NSSplitView* verticalSplit = nil;    // top area + bottom console
    NSSplitView* horizontalSplit = nil;  // left + center + right

    // Native panel views.
    std::unique_ptr<CocoaHierarchyView> hierarchyView;
    std::unique_ptr<CocoaPropertiesView> propertiesView;
    std::unique_ptr<CocoaConsoleView> consoleView;
    std::unique_ptr<CocoaResourceView> resourceView;
    std::unique_ptr<CocoaAnimationView> animationView;

    // Bottom tab view for Console + Resources.
    NSTabView* bottomTabView = nil;

    // Status bar widgets (Android build progress).
    NSView* rootContainer = nil;     // root content view: split + status bar
    NSView* statusBar = nil;         // bottom strip (height ~24)
    NSTextField* statusLabel = nil;  // "Ready" / "Building APK…"
    NSProgressIndicator* statusSpinner = nil;
    NSButton* statusCancelButton = nil;
    EditorBuildCancelTarget* cancelTarget = nil;
    CocoaEditorWindow::BuildCancelHandler cancelHandler;  // C++ callback target

    uint32_t windowWidth = 0;
    uint32_t windowHeight = 0;

    // Mouse state
    double mouseX = 0.0;
    double mouseY = 0.0;
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
            impl_->menuHandler = [[EditorMenuHandler alloc] init];
            SEL menuSel = @selector(menuAction:);
            id target = impl_->menuHandler;

            auto addItem = [&](NSMenu* menu, NSString* title, NSString* key, NSInteger tag)
            {
                NSMenuItem* item = [menu addItemWithTitle:title action:menuSel keyEquivalent:key];
                item.target = target;
                item.tag = tag;
                return item;
            };

            NSMenu* menuBar = [[NSMenu alloc] init];

            // App menu
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
            addItem(fileMenu, @"New Scene", @"n", 1);
            addItem(fileMenu, @"Open Scene...", @"o", 2);
            addItem(fileMenu, @"Import Asset...", @"i", 11);
            [fileMenu addItem:[NSMenuItem separatorItem]];
            addItem(fileMenu, @"Save Scene", @"s", 3);
            NSMenuItem* saveAs = addItem(fileMenu, @"Save Scene As...", @"S", 4);
            saveAs.keyEquivalentModifierMask =
                NSEventModifierFlagCommand | NSEventModifierFlagShift;
            fileMenuItem.submenu = fileMenu;
            [menuBar addItem:fileMenuItem];

            // Edit menu
            NSMenuItem* editMenuItem = [[NSMenuItem alloc] init];
            NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
            addItem(editMenu, @"Undo", @"z", 5);
            NSMenuItem* redo = addItem(editMenu, @"Redo", @"Z", 6);
            redo.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
            [editMenu addItem:[NSMenuItem separatorItem]];
            addItem(editMenu, @"Delete", @"", 7);
            editMenuItem.submenu = editMenu;
            [menuBar addItem:editMenuItem];

            // Entity menu
            NSMenuItem* entityMenuItem = [[NSMenuItem alloc] init];
            NSMenu* entityMenu = [[NSMenu alloc] initWithTitle:@"Entity"];
            addItem(entityMenu, @"Create Empty", @"", 8);
            addItem(entityMenu, @"Create Cube", @"", 9);
            addItem(entityMenu, @"Create Light", @"", 10);
            entityMenuItem.submenu = entityMenu;
            [menuBar addItem:entityMenuItem];

            // Component menu — adds components to the primary-selected entity.
            NSMenuItem* componentMenuItem = [[NSMenuItem alloc] init];
            NSMenu* componentMenu = [[NSMenu alloc] initWithTitle:@"Component"];
            addItem(componentMenu, @"Add Directional Light", @"", 20);
            addItem(componentMenu, @"Add Point Light", @"", 21);
            addItem(componentMenu, @"Add Mesh (Cube)", @"", 22);
            [componentMenu addItem:[NSMenuItem separatorItem]];
            addItem(componentMenu, @"Add Rigid Body", @"", 23);
            addItem(componentMenu, @"Add Box Collider", @"", 24);
            componentMenuItem.submenu = componentMenu;
            [menuBar addItem:componentMenuItem];

            // View menu
            NSMenuItem* viewMenuItem = [[NSMenuItem alloc] init];
            NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
            [viewMenu addItemWithTitle:@"Toggle Console" action:nil keyEquivalent:@""];
            [viewMenu addItemWithTitle:@"Toggle Resources" action:nil keyEquivalent:@""];
            [viewMenu addItem:[NSMenuItem separatorItem]];
            addItem(viewMenu, @"Load Environment (Skybox)...", @"", 30);
            addItem(viewMenu, @"Load HDR Environment...", @"", 31);
            addItem(viewMenu, @"Load Cubemap (DDS/KTX)...", @"", 32);
            [viewMenu addItem:[NSMenuItem separatorItem]];
            addItem(viewMenu, @"Reset Skybox to Default", @"", 33);
            viewMenuItem.submenu = viewMenu;
            [menuBar addItem:viewMenuItem];

            // Build menu
            NSMenuItem* buildMenuItem = [[NSMenuItem alloc] init];
            NSMenu* buildMenu = [[NSMenu alloc] initWithTitle:@"Build"];
            addItem(buildMenu, @"Android (Low)", @"", 40);
            addItem(buildMenu, @"Android (Mid)", @"", 41);
            addItem(buildMenu, @"Android (High)", @"", 42);
            [buildMenu addItem:[NSMenuItem separatorItem]];
            // Build & Run uses the persisted default tier from Settings.
            // Cmd+R is the customary "run" shortcut on macOS.
            NSMenuItem* runItem = addItem(buildMenu, @"Android — Build & Run", @"r", 43);
            runItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            [buildMenu addItem:[NSMenuItem separatorItem]];
            addItem(buildMenu, @"Android Build Settings…", @"", 44);
            buildMenuItem.submenu = buildMenu;
            [menuBar addItem:buildMenuItem];

            [NSApp setMainMenu:menuBar];
        }

        impl_->windowWidth = w;
        impl_->windowHeight = h;

        // -- Create native panel views ----------------------------------------
        impl_->hierarchyView = std::make_unique<CocoaHierarchyView>();
        impl_->propertiesView = std::make_unique<CocoaPropertiesView>();
        impl_->consoleView = std::make_unique<CocoaConsoleView>();
        impl_->resourceView = std::make_unique<CocoaResourceView>();
        impl_->animationView = std::make_unique<CocoaAnimationView>();

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
        {
            NSTabViewItem* animationTab = [[NSTabViewItem alloc] initWithIdentifier:@"animation"];
            animationTab.label = @"Animation";
            animationTab.view = (__bridge NSView*)impl_->animationView->nativeView();
            [impl_->bottomTabView addTabViewItem:animationTab];
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

        CGFloat bottomHeight = 150.0;
        CGFloat topHeight = h - bottomHeight - 1.0;

        [leftView setFrameSize:NSMakeSize(leftWidth, topHeight)];
        [impl_->metalView setFrameSize:NSMakeSize(centerWidth, topHeight)];
        [rightView setFrameSize:NSMakeSize(rightWidth, topHeight)];

        // Add top and bottom to vertical split.
        [impl_->verticalSplit addSubview:impl_->horizontalSplit];
        [impl_->verticalSplit addSubview:bottomView];
        [impl_->horizontalSplit setFrameSize:NSMakeSize(w, topHeight)];
        [bottomView setFrameSize:NSMakeSize(w, bottomHeight)];

        // Set divider positions.
        [impl_->verticalSplit adjustSubviews];
        [impl_->horizontalSplit adjustSubviews];

        // -- Build status bar -------------------------------------------------
        // A thin strip pinned to the bottom of the window: spinner +
        // status label + cancel button. Idle state shows "Ready" with the
        // spinner hidden. While an Android APK build is running it shows
        // the current pipeline step parsed from the build script's output.
        constexpr CGFloat kStatusBarHeight = 24.0;
        impl_->statusBar = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, w, kStatusBarHeight)];
        impl_->statusBar.wantsLayer = YES;
        impl_->statusBar.layer.backgroundColor = [[NSColor controlBackgroundColor] CGColor];
        impl_->statusBar.translatesAutoresizingMaskIntoConstraints = NO;

        // Top edge separator so it visually detaches from the bottom panel.
        NSBox* statusSep = [[NSBox alloc] init];
        statusSep.boxType = NSBoxSeparator;
        statusSep.translatesAutoresizingMaskIntoConstraints = NO;
        [impl_->statusBar addSubview:statusSep];

        impl_->statusSpinner = [[NSProgressIndicator alloc] init];
        impl_->statusSpinner.style = NSProgressIndicatorStyleSpinning;
        impl_->statusSpinner.controlSize = NSControlSizeSmall;
        impl_->statusSpinner.indeterminate = YES;
        impl_->statusSpinner.displayedWhenStopped = NO;
        impl_->statusSpinner.translatesAutoresizingMaskIntoConstraints = NO;
        [impl_->statusBar addSubview:impl_->statusSpinner];

        impl_->statusLabel = [NSTextField labelWithString:@"Ready"];
        impl_->statusLabel.font = [NSFont systemFontOfSize:11.0];
        impl_->statusLabel.textColor = [NSColor secondaryLabelColor];
        impl_->statusLabel.lineBreakMode = NSLineBreakByTruncatingTail;
        impl_->statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
        [impl_->statusBar addSubview:impl_->statusLabel];

        impl_->cancelTarget = [[EditorBuildCancelTarget alloc] init];
        impl_->cancelTarget->handlerPtr = &impl_->cancelHandler;

        impl_->statusCancelButton = [[NSButton alloc] init];
        impl_->statusCancelButton.title = @"Cancel";
        impl_->statusCancelButton.bezelStyle = NSBezelStyleRounded;
        impl_->statusCancelButton.controlSize = NSControlSizeMini;
        impl_->statusCancelButton.font = [NSFont systemFontOfSize:10.0];
        impl_->statusCancelButton.target = impl_->cancelTarget;
        impl_->statusCancelButton.action = @selector(cancelClicked:);
        impl_->statusCancelButton.hidden = YES;
        impl_->statusCancelButton.translatesAutoresizingMaskIntoConstraints = NO;
        [impl_->statusBar addSubview:impl_->statusCancelButton];

        [NSLayoutConstraint activateConstraints:@[
            // Separator pinned to top of status bar.
            [statusSep.topAnchor constraintEqualToAnchor:impl_->statusBar.topAnchor],
            [statusSep.leadingAnchor constraintEqualToAnchor:impl_->statusBar.leadingAnchor],
            [statusSep.trailingAnchor constraintEqualToAnchor:impl_->statusBar.trailingAnchor],
            [statusSep.heightAnchor constraintEqualToConstant:1.0],
            // Spinner left-aligned.
            [impl_->statusSpinner.leadingAnchor
                constraintEqualToAnchor:impl_->statusBar.leadingAnchor
                               constant:8.0],
            [impl_->statusSpinner.centerYAnchor
                constraintEqualToAnchor:impl_->statusBar.centerYAnchor],
            [impl_->statusSpinner.widthAnchor constraintEqualToConstant:14.0],
            [impl_->statusSpinner.heightAnchor constraintEqualToConstant:14.0],
            // Label centered next to spinner, expands to fill.
            [impl_->statusLabel.leadingAnchor
                constraintEqualToAnchor:impl_->statusSpinner.trailingAnchor
                               constant:8.0],
            [impl_->statusLabel.centerYAnchor
                constraintEqualToAnchor:impl_->statusBar.centerYAnchor],
            [impl_->statusLabel.trailingAnchor
                constraintLessThanOrEqualToAnchor:impl_->statusCancelButton.leadingAnchor
                                         constant:-8.0],
            // Cancel button right-aligned.
            [impl_->statusCancelButton.trailingAnchor
                constraintEqualToAnchor:impl_->statusBar.trailingAnchor
                               constant:-8.0],
            [impl_->statusCancelButton.centerYAnchor
                constraintEqualToAnchor:impl_->statusBar.centerYAnchor],
        ]];

        // Wrap split + status bar in a root container so AppKit owns the
        // resize semantics correctly (split fills above, status bar fixed
        // at the bottom).
        impl_->rootContainer = [[NSView alloc] initWithFrame:frame];
        impl_->verticalSplit.translatesAutoresizingMaskIntoConstraints = NO;
        [impl_->rootContainer addSubview:impl_->verticalSplit];
        [impl_->rootContainer addSubview:impl_->statusBar];
        [NSLayoutConstraint activateConstraints:@[
            [impl_->verticalSplit.topAnchor constraintEqualToAnchor:impl_->rootContainer.topAnchor],
            [impl_->verticalSplit.leadingAnchor
                constraintEqualToAnchor:impl_->rootContainer.leadingAnchor],
            [impl_->verticalSplit.trailingAnchor
                constraintEqualToAnchor:impl_->rootContainer.trailingAnchor],
            [impl_->verticalSplit.bottomAnchor constraintEqualToAnchor:impl_->statusBar.topAnchor],

            [impl_->statusBar.leadingAnchor
                constraintEqualToAnchor:impl_->rootContainer.leadingAnchor],
            [impl_->statusBar.trailingAnchor
                constraintEqualToAnchor:impl_->rootContainer.trailingAnchor],
            [impl_->statusBar.bottomAnchor
                constraintEqualToAnchor:impl_->rootContainer.bottomAnchor],
            [impl_->statusBar.heightAnchor constraintEqualToConstant:kStatusBarHeight],
        ]];

        // Set as content view.
        [impl_->window setContentView:impl_->rootContainer];

        // Show the window.
        [impl_->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        // Make the metal view first responder for keyboard events.
        [impl_->window makeFirstResponder:impl_->metalView];

        // Install a window-level key monitor so key events are captured
        // regardless of which view has focus (e.g., Delete when hierarchy
        // panel's NSTableView is focused). But skip when a text field is
        // being edited — otherwise Delete removes an entity instead of
        // deleting a character in the text.
        EditorMetalView* metalViewRef = impl_->metalView;
        NSWindow* winRef = impl_->window;
        impl_->keyMonitor = [NSEvent
            addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                         handler:^NSEvent*(NSEvent* event) {
                                           // Don't capture keys when editing text.
                                           id firstResponder = [winRef firstResponder];
                                           if ([firstResponder isKindOfClass:[NSTextView class]] ||
                                               [firstResponder isKindOfClass:[NSTextField class]])
                                           {
                                               // Only capture Cmd+key combos (shortcuts),
                                               // not plain typing keys.
                                               if (!([event modifierFlags] &
                                                     NSEventModifierFlagCommand))
                                               {
                                                   return event;
                                               }
                                           }

                                           uint8_t code = mapKeyCode([event keyCode]);
                                           if (code != 0)
                                           {
                                               metalViewRef->keyPressed_[code] = YES;
                                           }
                                           // Update modifiers.
                                           NSEventModifierFlags mods = [event modifierFlags];
                                           metalViewRef->modCommand_ =
                                               (mods & NSEventModifierFlagCommand) != 0;
                                           metalViewRef->modShift_ =
                                               (mods & NSEventModifierFlagShift) != 0;
                                           return event;  // pass through
                                         }];

        return true;
    }
}

void CocoaEditorWindow::shutdown()
{
    @autoreleasepool
    {
        if (impl_->keyMonitor)
        {
            [NSEvent removeMonitor:impl_->keyMonitor];
            impl_->keyMonitor = nil;
        }

        impl_->hierarchyView.reset();
        impl_->propertiesView.reset();
        impl_->consoleView.reset();
        impl_->resourceView.reset();
        impl_->animationView.reset();
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
        impl_->statusBar = nil;
        impl_->statusLabel = nil;
        impl_->statusSpinner = nil;
        impl_->statusCancelButton = nil;
        impl_->cancelTarget = nil;
        impl_->rootContainer = nil;
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

        impl_->mouseX = mx;
        impl_->mouseY = my;

        // Mouse deltas from event accumulators — more reliable than position
        // differencing, especially during right-drag where
        // mouseLocationOutsideOfEventStream may not update.
        impl_->deltaX = impl_->metalView.mouseMoveAccumX;
        impl_->deltaY = impl_->metalView.mouseMoveAccumY;
        impl_->metalView.mouseMoveAccumX = 0.0;
        impl_->metalView.mouseMoveAccumY = 0.0;

        // Mouse buttons — tracked via NSView event handlers for reliability
        // on trackpads and with system right-click settings.
        impl_->leftDown = impl_->metalView.leftMouseHeld;
        impl_->rightDown = impl_->metalView.rightMouseHeld;

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

std::string CocoaEditorWindow::showOpenDialogMultiExt(const std::vector<std::string>& extensions,
                                                      const char* title)
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        if (title != nullptr)
        {
            panel.title = @(title);
        }
        NSMutableArray<UTType*>* types = [NSMutableArray arrayWithCapacity:extensions.size()];
        for (const auto& ext : extensions)
        {
            UTType* type = [UTType typeWithFilenameExtension:@(ext.c_str())];
            if (type != nil)
            {
                [types addObject:type];
            }
        }
        if (types.count > 0)
        {
            panel.allowedContentTypes = types;
        }
        panel.allowsMultipleSelection = NO;
        if ([panel runModal] == NSModalResponseOK)
        {
            return std::string([[panel.URLs[0] path] UTF8String]);
        }
        return {};
    }
}

std::string CocoaEditorWindow::showImportDialog()
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Import 3D Asset";
        panel.allowedContentTypes = @[
            [UTType typeWithFilenameExtension:@"glb"],
            [UTType typeWithFilenameExtension:@"gltf"],
            [UTType typeWithFilenameExtension:@"obj"],
        ];
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

CocoaAnimationView* CocoaEditorWindow::animationView() const
{
    return impl_->animationView.get();
}

void CocoaEditorWindow::setMenuCallback(MenuCallback callback)
{
    s_menuActionCallback = callback;
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void CocoaEditorWindow::setBuildStatus(const char* text, BuildStatusKind kind)
{
    // The build thread calls this; UI updates must happen on the main queue.
    NSString* nsText = [NSString stringWithUTF8String:(text ? text : "")];
    BuildStatusKind capturedKind = kind;
    NSProgressIndicator* spinner = impl_->statusSpinner;
    NSTextField* label = impl_->statusLabel;
    NSButton* cancel = impl_->statusCancelButton;

    void (^update)(void) = ^{
      if (label)
      {
          label.stringValue = nsText;
          switch (capturedKind)
          {
              case BuildStatusKind::Failure:
                  label.textColor = [NSColor systemRedColor];
                  break;
              case BuildStatusKind::Success:
                  label.textColor = [NSColor systemGreenColor];
                  break;
              case BuildStatusKind::Running:
                  label.textColor = [NSColor labelColor];
                  break;
              default:
                  label.textColor = [NSColor secondaryLabelColor];
                  break;
          }
      }
      if (spinner)
      {
          if (capturedKind == BuildStatusKind::Running)
              [spinner startAnimation:nil];
          else
              [spinner stopAnimation:nil];
      }
      if (cancel)
      {
          cancel.hidden = (capturedKind != BuildStatusKind::Running);
      }
    };

    if ([NSThread isMainThread])
    {
        update();
    }
    else
    {
        dispatch_async(dispatch_get_main_queue(), update);
    }
}

void CocoaEditorWindow::setBuildCancelHandler(BuildCancelHandler handler)
{
    impl_->cancelHandler = std::move(handler);
}

// ---------------------------------------------------------------------------
// Android build settings (NSUserDefaults)
// ---------------------------------------------------------------------------

namespace
{
constexpr const char* kKeyTier = "android.defaultTier";
constexpr const char* kKeyKeystore = "android.keystorePath";
constexpr const char* kKeyKsPassEnv = "android.keystorePassEnvVar";
constexpr const char* kKeyOutput = "android.outputApkPath";
constexpr const char* kKeyPackage = "android.packageId";
constexpr const char* kKeyDeviceSerial = "android.lastDeviceSerial";
constexpr const char* kKeyBuildAndRun = "android.buildAndRun";

NSString* nsKey(const char* k)
{
    return [NSString stringWithUTF8String:k];
}

std::string nsToStd(NSString* s)
{
    if (!s)
        return {};
    const char* c = [s UTF8String];
    return c ? std::string(c) : std::string();
}
}  // namespace

AndroidBuildSettings CocoaEditorWindow::loadAndroidBuildSettings() const
{
    AndroidBuildSettings out;
    @autoreleasepool
    {
        NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
        NSString* tier = [d stringForKey:nsKey(kKeyTier)];
        if (tier && tier.length > 0)
            out.defaultTier = nsToStd(tier);
        out.keystorePath = nsToStd([d stringForKey:nsKey(kKeyKeystore)]);
        out.keystorePasswordEnvVar = nsToStd([d stringForKey:nsKey(kKeyKsPassEnv)]);
        out.outputApkPath = nsToStd([d stringForKey:nsKey(kKeyOutput)]);
        NSString* pkg = [d stringForKey:nsKey(kKeyPackage)];
        if (pkg && pkg.length > 0)
            out.packageId = nsToStd(pkg);
        out.lastDeviceSerial = nsToStd([d stringForKey:nsKey(kKeyDeviceSerial)]);
        out.buildAndRun = [d boolForKey:nsKey(kKeyBuildAndRun)];
    }
    return out;
}

void CocoaEditorWindow::saveAndroidBuildSettings(const AndroidBuildSettings& s)
{
    @autoreleasepool
    {
        NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
        [d setObject:[NSString stringWithUTF8String:s.defaultTier.c_str()] forKey:nsKey(kKeyTier)];
        [d setObject:[NSString stringWithUTF8String:s.keystorePath.c_str()]
              forKey:nsKey(kKeyKeystore)];
        [d setObject:[NSString stringWithUTF8String:s.keystorePasswordEnvVar.c_str()]
              forKey:nsKey(kKeyKsPassEnv)];
        [d setObject:[NSString stringWithUTF8String:s.outputApkPath.c_str()]
              forKey:nsKey(kKeyOutput)];
        [d setObject:[NSString stringWithUTF8String:s.packageId.c_str()] forKey:nsKey(kKeyPackage)];
        [d setObject:[NSString stringWithUTF8String:s.lastDeviceSerial.c_str()]
              forKey:nsKey(kKeyDeviceSerial)];
        [d setBool:s.buildAndRun forKey:nsKey(kKeyBuildAndRun)];
        [d synchronize];
    }
}

bool CocoaEditorWindow::showAndroidBuildSettingsDialog(AndroidBuildSettings& outSettings)
{
    @autoreleasepool
    {
        AndroidBuildSettings current = loadAndroidBuildSettings();

        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Android Build Settings";
        alert.informativeText = @"Configure default tier, keystore, output APK path, "
                                @"package ID, and Build & Run behaviour. Settings persist "
                                @"across editor sessions.";
        [alert addButtonWithTitle:@"Save"];
        [alert addButtonWithTitle:@"Cancel"];

        // Build a vertical form view.
        constexpr CGFloat kRowHeight = 26.0;
        constexpr CGFloat kRowPad = 6.0;
        constexpr CGFloat kLabelWidth = 150.0;
        constexpr CGFloat kFieldWidth = 280.0;
        constexpr CGFloat kFormWidth = kLabelWidth + 8.0 + kFieldWidth;
        constexpr int kRowCount = 7;
        constexpr CGFloat kFormHeight = kRowCount * kRowHeight + (kRowCount - 1) * kRowPad;

        NSView* form = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, kFormWidth, kFormHeight)];

        auto rowY = [&](int row) -> CGFloat
        {
            // Top-down layout: row 0 sits at the top.
            return kFormHeight - (row + 1) * kRowHeight - row * kRowPad;
        };

        auto addLabel = [&](int row, NSString* text)
        {
            NSTextField* l = [NSTextField labelWithString:text];
            l.font = [NSFont systemFontOfSize:11.0];
            l.textColor = [NSColor secondaryLabelColor];
            l.frame = NSMakeRect(0, rowY(row), kLabelWidth, kRowHeight);
            l.alignment = NSTextAlignmentRight;
            [form addSubview:l];
        };

        auto addField = [&](int row, NSString* value) -> NSTextField*
        {
            NSTextField* f =
                [[NSTextField alloc] initWithFrame:NSMakeRect(kLabelWidth + 8.0, rowY(row),
                                                              kFieldWidth, kRowHeight - 4)];
            f.stringValue = value ? value : @"";
            f.font = [NSFont systemFontOfSize:11.0];
            [form addSubview:f];
            return f;
        };

        // Row 0: Default tier (popup)
        addLabel(0, @"Default tier:");
        NSPopUpButton* tierPopup = [[NSPopUpButton alloc]
            initWithFrame:NSMakeRect(kLabelWidth + 8.0, rowY(0), 120, kRowHeight - 2)
                pullsDown:NO];
        [tierPopup addItemsWithTitles:@[ @"low", @"mid", @"high" ]];
        [tierPopup selectItemWithTitle:[NSString stringWithUTF8String:current.defaultTier.c_str()]];
        [form addSubview:tierPopup];

        // Row 1: Keystore path
        addLabel(1, @"Keystore path:");
        NSTextField* keystoreField =
            addField(1, [NSString stringWithUTF8String:current.keystorePath.c_str()]);

        // Row 2: Keystore password env var
        addLabel(2, @"Keystore password env:");
        NSTextField* ksPassEnvField =
            addField(2, [NSString stringWithUTF8String:current.keystorePasswordEnvVar.c_str()]);

        // Row 3: Output APK path
        addLabel(3, @"Output APK path:");
        NSTextField* outputField =
            addField(3, [NSString stringWithUTF8String:current.outputApkPath.c_str()]);

        // Row 4: Package ID
        addLabel(4, @"Package ID:");
        NSTextField* packageField =
            addField(4, [NSString stringWithUTF8String:current.packageId.c_str()]);

        // Row 5: Device serial (with refresh button)
        addLabel(5, @"Device serial:");
        NSTextField* deviceField =
            addField(5, [NSString stringWithUTF8String:current.lastDeviceSerial.c_str()]);
        deviceField.placeholderString = @"(empty = first connected)";

        // Row 6: Build & Run after build
        addLabel(6, @"");
        NSButton* runCheckbox = [[NSButton alloc]
            initWithFrame:NSMakeRect(kLabelWidth + 8.0, rowY(6), kFieldWidth, kRowHeight - 2)];
        [runCheckbox setButtonType:NSButtonTypeSwitch];
        runCheckbox.title = @"Build & Run after build (auto-install + launch)";
        runCheckbox.font = [NSFont systemFontOfSize:11.0];
        runCheckbox.state = current.buildAndRun ? NSControlStateValueOn : NSControlStateValueOff;
        [form addSubview:runCheckbox];

        alert.accessoryView = form;
        alert.window.initialFirstResponder = keystoreField;

        NSModalResponse resp = [alert runModal];
        if (resp == NSAlertFirstButtonReturn)
        {
            AndroidBuildSettings updated;
            updated.defaultTier = nsToStd([tierPopup titleOfSelectedItem]);
            if (updated.defaultTier.empty())
                updated.defaultTier = "mid";
            updated.keystorePath = nsToStd(keystoreField.stringValue);
            updated.keystorePasswordEnvVar = nsToStd(ksPassEnvField.stringValue);
            updated.outputApkPath = nsToStd(outputField.stringValue);
            updated.packageId = nsToStd(packageField.stringValue);
            if (updated.packageId.empty())
                updated.packageId = "com.sama.game";
            updated.lastDeviceSerial = nsToStd(deviceField.stringValue);
            updated.buildAndRun = (runCheckbox.state == NSControlStateValueOn);
            saveAndroidBuildSettings(updated);
            outSettings = updated;
            return true;
        }
        return false;
    }
}

// ---------------------------------------------------------------------------
// adb device discovery + alerts
// ---------------------------------------------------------------------------

std::vector<CocoaEditorWindow::AdbDevice> CocoaEditorWindow::queryAdbDevices() const
{
    std::vector<AdbDevice> devices;
    // Use plain popen — `adb devices -l` prints a stable line-oriented format:
    //   List of devices attached
    //   <serial>\t<state> [model:Foo product:Bar device:Baz transport_id:N]
    FILE* pipe = popen("adb devices -l 2>/dev/null", "r");
    if (!pipe)
        return devices;
    char buffer[1024];
    bool firstLine = true;
    while (fgets(buffer, sizeof(buffer), pipe))
    {
        std::string line(buffer);
        if (!line.empty() && line.back() == '\n')
            line.pop_back();
        if (firstLine)
        {
            // Skip the "List of devices attached" header.
            firstLine = false;
            continue;
        }
        if (line.empty())
            continue;
        // Find the first whitespace separator.
        size_t tab = line.find_first_of(" \t");
        if (tab == std::string::npos)
            continue;
        AdbDevice d;
        d.serial = line.substr(0, tab);
        size_t rest = line.find_first_not_of(" \t", tab);
        if (rest != std::string::npos)
        {
            d.description = line.substr(rest);
        }
        // Only keep devices in the "device" state (skip "offline",
        // "unauthorized", etc.).
        if (d.description.find("device") != std::string::npos)
        {
            devices.push_back(std::move(d));
        }
    }
    pclose(pipe);
    return devices;
}

void CocoaEditorWindow::showAlert(const char* title, const char* message)
{
    @autoreleasepool
    {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithUTF8String:(title ? title : "")];
        alert.informativeText = [NSString stringWithUTF8String:(message ? message : "")];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}

}  // namespace engine::editor
