#include "editor/platform/cocoa/CocoaPropertiesView.h"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// ---------------------------------------------------------------------------
// PropertyFieldView -- a single property row (label + control).
// ---------------------------------------------------------------------------

@interface PropertyFieldView : NSView
@property(nonatomic, assign) int fieldId;
@property(nonatomic, copy) void (^onValueChanged)(int fieldId, float newValue);
@property(nonatomic, copy) void (^onColorChanged)(int fieldId, float r, float g, float b);
@end

@implementation PropertyFieldView
@end

// ---------------------------------------------------------------------------
// PropertiesFieldDelegate -- handles text field editing & slider/color changes.
// ---------------------------------------------------------------------------

@interface PropertiesFieldDelegate : NSObject <NSTextFieldDelegate>
@property(nonatomic, copy) void (^onValueChanged)(int fieldId, float newValue);
@property(nonatomic, copy) void (^onColorChanged)(int fieldId, float r, float g, float b);
@property(nonatomic, copy) void (^onIntChanged)(int fieldId, int newIndex);
@property(nonatomic, copy) void (^onTextureChanged)(int fieldId, NSString* path);
@property(nonatomic, copy) void (^onTextureCleared)(int fieldId);
@property(nonatomic, copy) void (^onAddComponent)(NSString* componentType);
@property(nonatomic, assign) BOOL suppressCallbacks;  // set during rebuild to prevent stale events
- (void)addComponentClicked:(NSButton*)sender;
- (void)addComponentMenuItemPicked:(NSMenuItem*)sender;
- (void)popupChanged:(NSPopUpButton*)sender;
- (void)textureBrowseClicked:(NSButton*)sender;
- (void)textureClearClicked:(NSButton*)sender;
@end

@implementation PropertiesFieldDelegate

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    if (_suppressCallbacks)
        return;
    NSTextField* field = notification.object;
    int fieldId = (int)field.tag;
    float value = field.floatValue;
    if (_onValueChanged)
    {
        _onValueChanged(fieldId, value);
    }
}

- (void)sliderChanged:(NSSlider*)sender
{
    int fieldId = (int)sender.tag;
    float value = (float)sender.doubleValue;
    if (_onValueChanged)
    {
        _onValueChanged(fieldId, value);
    }

    // Update the adjacent value label (tag = fieldId + 10000).
    NSStackView* row = (NSStackView*)sender.superview;
    if ([row isKindOfClass:[NSStackView class]])
    {
        for (NSView* sub in row.arrangedSubviews)
        {
            if (sub.tag == fieldId + 10000 && [sub isKindOfClass:[NSTextField class]])
            {
                ((NSTextField*)sub).stringValue = [NSString stringWithFormat:@"%.2f", value];
                break;
            }
        }
    }
}

- (void)colorWellChanged:(NSColorWell*)sender
{
    int fieldId = (int)sender.tag;
    NSColor* rgb = [sender.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!rgb)
        rgb = sender.color;
    CGFloat r, g, b, a;
    [rgb getRed:&r green:&g blue:&b alpha:&a];
    if (_onColorChanged)
    {
        _onColorChanged(fieldId, (float)r, (float)g, (float)b);
    }
}

- (void)addComponentClicked:(NSButton*)sender
{
    // Build a popup menu of addable components and show it under the button.
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Add Component"];
    menu.autoenablesItems = NO;

    auto add = ^(NSString* title, NSString* type) {
      NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                    action:@selector(addComponentMenuItemPicked:)
                                             keyEquivalent:@""];
      item.target = self;
      item.representedObject = type;
      item.enabled = YES;
      [menu addItem:item];
    };

    add(@"Directional Light", @"directional_light");
    add(@"Point Light", @"point_light");
    add(@"Mesh (Cube)", @"mesh");
    [menu addItem:[NSMenuItem separatorItem]];
    add(@"Rigid Body", @"rigid_body");
    add(@"Box Collider", @"box_collider");

    // Show the menu just below the button.
    NSPoint origin = NSMakePoint(0, sender.bounds.size.height + 4);
    [menu popUpMenuPositioningItem:nil atLocation:origin inView:sender];
}

- (void)addComponentMenuItemPicked:(NSMenuItem*)sender
{
    NSString* type = (NSString*)sender.representedObject;
    if (_onAddComponent && type)
    {
        _onAddComponent(type);
    }
}

- (void)popupChanged:(NSPopUpButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onIntChanged)
    {
        _onIntChanged((int)sender.tag, (int)sender.indexOfSelectedItem);
    }
}

- (void)textureBrowseClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message = @"Choose a texture image";
    if (@available(macOS 11.0, *))
    {
        // Modern API: filter by Uniform Type Identifiers.
        NSMutableArray<UTType*>* types = [NSMutableArray array];
        for (NSString* ext in @[ @"png", @"jpg", @"jpeg", @"tga", @"bmp", @"hdr", @"ktx", @"dds" ])
        {
            UTType* t = [UTType typeWithFilenameExtension:ext];
            if (t)
                [types addObject:t];
        }
        panel.allowedContentTypes = types;
    }
    else
    {
        // Pre-Big Sur fallback (deprecated in 12.0 but still works).
        panel.allowedFileTypes =
            @[ @"png", @"jpg", @"jpeg", @"tga", @"bmp", @"hdr", @"ktx", @"dds" ];
    }
    if ([panel runModal] != NSModalResponseOK || panel.URLs.count == 0)
    {
        return;
    }
    NSString* path = [panel.URLs.firstObject path];
    if (_onTextureChanged && path)
    {
        _onTextureChanged((int)sender.tag, path);
    }
}

- (void)textureClearClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onTextureCleared)
    {
        _onTextureCleared((int)sender.tag);
    }
}

@end

// ---------------------------------------------------------------------------
// Helper: create a label NSTextField.
// ---------------------------------------------------------------------------

static NSTextField* makeLabel(NSString* text, CGFloat fontSize)
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.font = [NSFont systemFontOfSize:fontSize];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

static NSTextField* makeHeader(NSString* text)
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.font = [NSFont boldSystemFontOfSize:12.0];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

static NSTextField* makeEditableField(float value, int fieldId, void (^onChange)(int, float))
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 70, 22)];
    field.stringValue = [NSString stringWithFormat:@"%.3f", value];
    field.font = [NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular];
    field.alignment = NSTextAlignmentRight;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    field.bezelStyle = NSTextFieldRoundedBezel;
    [field.widthAnchor constraintGreaterThanOrEqualToConstant:65].active = YES;
    return field;
}

// ---------------------------------------------------------------------------
// FlippedView -- NSView with origin at top-left (for top-aligned content).
// ---------------------------------------------------------------------------

@interface FlippedView : NSView
@end

@implementation FlippedView
- (BOOL)isFlipped
{
    return YES;
}
@end

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

namespace engine::editor
{

struct CocoaPropertiesView::Impl
{
    NSScrollView* scrollView = nil;
    NSStackView* stackView = nil;
    NSTextField* statusLabel = nil;
    PropertiesFieldDelegate* delegate = nil;
    ValueChangedCallback valueChangedCallback;
    ColorChangedCallback colorChangedCallback;
    IntChangedCallback intChangedCallback;
    TextureChangedCallback textureChangedCallback;
    TextureClearedCallback textureClearedCallback;
    AddComponentCallback addComponentCallback;
    std::vector<NSView*> fieldViews;  // Retains views for tag lookup
};

CocoaPropertiesView::CocoaPropertiesView() : impl_(std::make_unique<Impl>())
{
    @autoreleasepool
    {
        impl_->scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 250, 400)];
        impl_->scrollView.hasVerticalScroller = YES;
        impl_->scrollView.autohidesScrollers = YES;
        impl_->scrollView.drawsBackground = NO;

        // Use a flipped NSView as the document view to get top-to-bottom layout.
        // Standard NSView has origin at bottom-left; FlippedView puts origin at
        // top-left so the stack view content aligns to the top, not the bottom.
        NSView* container = [[FlippedView alloc] initWithFrame:NSMakeRect(0, 0, 250, 400)];

        impl_->stackView = [NSStackView stackViewWithViews:@[]];
        impl_->stackView.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->stackView.alignment = NSLayoutAttributeLeading;
        impl_->stackView.spacing = 4.0;
        impl_->stackView.translatesAutoresizingMaskIntoConstraints = NO;
        impl_->stackView.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);

        [container addSubview:impl_->stackView];
        [NSLayoutConstraint activateConstraints:@[
            [impl_->stackView.topAnchor constraintEqualToAnchor:container.topAnchor],
            [impl_->stackView.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
            [impl_->stackView.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
        ]];

        impl_->scrollView.documentView = container;

        // Create the delegate that handles field edits.
        impl_->delegate = [[PropertiesFieldDelegate alloc] init];

        // Status label shown when nothing is selected.
        impl_->statusLabel = makeLabel(@"No entity selected", 12.0);
        [impl_->stackView addArrangedSubview:impl_->statusLabel];
    }
}

CocoaPropertiesView::~CocoaPropertiesView()
{
    @autoreleasepool
    {
        impl_->scrollView = nil;
        impl_->stackView = nil;
        impl_->statusLabel = nil;
        impl_->delegate = nil;
        impl_->fieldViews.clear();
    }
}

void* CocoaPropertiesView::nativeView() const
{
    return (__bridge void*)impl_->scrollView;
}

void CocoaPropertiesView::setValueChangedCallback(ValueChangedCallback cb)
{
    impl_->valueChangedCallback = std::move(cb);
    auto& storedCb = impl_->valueChangedCallback;
    impl_->delegate.onValueChanged = ^(int fieldId, float newValue) {
      if (storedCb)
          storedCb(fieldId, newValue);
    };
}

void CocoaPropertiesView::setColorChangedCallback(ColorChangedCallback cb)
{
    impl_->colorChangedCallback = std::move(cb);
    auto& storedCb = impl_->colorChangedCallback;
    impl_->delegate.onColorChanged = ^(int fieldId, float r, float g, float b) {
      if (storedCb)
          storedCb(fieldId, r, g, b);
    };
}

void CocoaPropertiesView::setIntChangedCallback(IntChangedCallback cb)
{
    impl_->intChangedCallback = std::move(cb);
    auto& storedCb = impl_->intChangedCallback;
    impl_->delegate.onIntChanged = ^(int fieldId, int newIndex) {
      if (storedCb)
          storedCb(fieldId, newIndex);
    };
}

void CocoaPropertiesView::setTextureChangedCallback(TextureChangedCallback cb)
{
    impl_->textureChangedCallback = std::move(cb);
    auto& storedCb = impl_->textureChangedCallback;
    impl_->delegate.onTextureChanged = ^(int fieldId, NSString* path) {
      if (storedCb && path)
          storedCb(fieldId, std::string([path UTF8String]));
    };
}

void CocoaPropertiesView::setTextureClearedCallback(TextureClearedCallback cb)
{
    impl_->textureClearedCallback = std::move(cb);
    auto& storedCb = impl_->textureClearedCallback;
    impl_->delegate.onTextureCleared = ^(int fieldId) {
      if (storedCb)
          storedCb(fieldId);
    };
}

void CocoaPropertiesView::setAddComponentCallback(AddComponentCallback cb)
{
    impl_->addComponentCallback = std::move(cb);
    auto& storedCb = impl_->addComponentCallback;
    impl_->delegate.onAddComponent = ^(NSString* type) {
      if (storedCb && type)
      {
          storedCb(std::string([type UTF8String]));
      }
    };
}

void CocoaPropertiesView::setProperties(const std::vector<PropertyField>& fields)
{
    @autoreleasepool
    {
        // Suppress callbacks during rebuild — removing a focused text field fires
        // controlTextDidEndEditing with stale data that corrupts entity state.
        impl_->delegate.suppressCallbacks = YES;

        // Remove all existing views from the stack.
        // Resign first responder first to prevent end-editing on removal.
        NSWindow* win = impl_->scrollView.window;
        if (win && [[win firstResponder] isKindOfClass:[NSTextView class]])
        {
            [win makeFirstResponder:nil];
        }
        for (NSView* view in [impl_->stackView.arrangedSubviews copy])
        {
            [impl_->stackView removeArrangedSubview:view];
            [view removeFromSuperview];
        }
        impl_->fieldViews.clear();

        impl_->delegate.suppressCallbacks = NO;

        if (fields.empty())
        {
            impl_->statusLabel = makeLabel(@"No entity selected", 12.0);
            [impl_->stackView addArrangedSubview:impl_->statusLabel];
            return;
        }

        // Section grouping: each Header field starts a new "section" with a
        // tinted background and a separator line above it. Subsequent rows
        // are appended to that section's inner stack until the next Header.
        // The first Header (entity name / id) is treated as a plain title
        // sitting outside any section.
        NSStackView* currentSectionStack = nil;
        int sectionIndex = 0;
        bool sawFirstHeader = false;

        for (const auto& field : fields)
        {
            // Where to add this field's view: the active section's inner
            // stack if we're inside one, otherwise the outer stackView
            // (used for the entity-title header at the top).
            NSStackView* targetStack =
                currentSectionStack != nil ? currentSectionStack : impl_->stackView;

            switch (field.type)
            {
                case PropertyField::Type::Header:
                {
                    NSString* labelStr = [NSString stringWithUTF8String:field.label.c_str()];

                    if (!sawFirstHeader)
                    {
                        // Entity-title header: bare label, no section
                        // background. Sits at the top of the panel.
                        sawFirstHeader = true;
                        NSTextField* title = makeHeader(labelStr);
                        title.font = [NSFont boldSystemFontOfSize:13.0];
                        [impl_->stackView addArrangedSubview:title];
                        impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)title);
                        break;
                    }

                    // Component-section header: finalize the previous
                    // section (if any) and start a new tinted container.
                    NSView* sectionView = [[NSView alloc] init];
                    sectionView.wantsLayer = YES;
                    // Alternating dark/light tones for visual separation
                    // between component sections. Slight contrast — both
                    // tones stay dark so the section text reads cleanly,
                    // and the rounded border carries most of the structure.
                    const CGFloat tone = (sectionIndex % 2 == 0) ? 0.16 : 0.20;
                    sectionView.layer.backgroundColor =
                        [NSColor colorWithCalibratedWhite:tone alpha:1.0].CGColor;
                    sectionView.layer.cornerRadius = 6.0;
                    sectionView.layer.borderWidth = 1.0;
                    sectionView.layer.borderColor =
                        [NSColor colorWithCalibratedWhite:0.40 alpha:0.5].CGColor;
                    sectionView.translatesAutoresizingMaskIntoConstraints = NO;

                    NSStackView* inner = [NSStackView stackViewWithViews:@[]];
                    inner.orientation = NSUserInterfaceLayoutOrientationVertical;
                    inner.alignment = NSLayoutAttributeLeading;
                    inner.spacing = 4.0;
                    inner.translatesAutoresizingMaskIntoConstraints = NO;
                    inner.edgeInsets = NSEdgeInsetsMake(8, 10, 8, 10);

                    [sectionView addSubview:inner];
                    [NSLayoutConstraint activateConstraints:@[
                        [inner.topAnchor constraintEqualToAnchor:sectionView.topAnchor],
                        [inner.leadingAnchor constraintEqualToAnchor:sectionView.leadingAnchor],
                        [inner.trailingAnchor constraintEqualToAnchor:sectionView.trailingAnchor],
                        [inner.bottomAnchor constraintEqualToAnchor:sectionView.bottomAnchor],
                    ]];

                    [impl_->stackView addArrangedSubview:sectionView];
                    // Stretch the section view to the full width of the outer
                    // stack so the colored background spans the panel.
                    [sectionView.leadingAnchor
                        constraintEqualToAnchor:impl_->stackView.leadingAnchor]
                        .active = YES;
                    [sectionView.trailingAnchor
                        constraintEqualToAnchor:impl_->stackView.trailingAnchor]
                        .active = YES;
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)sectionView);

                    currentSectionStack = inner;
                    targetStack = inner;
                    ++sectionIndex;

                    NSTextField* header = makeHeader(labelStr);
                    [targetStack addArrangedSubview:header];
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)header);
                    break;
                }
                case PropertyField::Type::Label:
                {
                    NSString* text = [NSString stringWithUTF8String:field.label.c_str()];
                    NSTextField* label = makeLabel(text, 11.0);
                    [targetStack addArrangedSubview:label];
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)label);
                    break;
                }
                case PropertyField::Type::FloatField:
                {
                    NSStackView* row = [NSStackView stackViewWithViews:@[]];
                    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
                    row.spacing = 6.0;
                    row.translatesAutoresizingMaskIntoConstraints = NO;

                    NSString* labelStr = [NSString stringWithUTF8String:field.label.c_str()];
                    NSTextField* label = makeLabel(labelStr, 11.0);
                    [label.widthAnchor constraintEqualToConstant:60].active = YES;

                    NSTextField* valueField = makeEditableField(field.value, field.fieldId, nil);
                    valueField.tag = field.fieldId;
                    valueField.delegate = impl_->delegate;

                    [row addArrangedSubview:label];
                    [row addArrangedSubview:valueField];
                    [targetStack addArrangedSubview:row];
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)row);
                    break;
                }
                case PropertyField::Type::SliderField:
                {
                    NSStackView* row = [NSStackView stackViewWithViews:@[]];
                    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
                    row.spacing = 6.0;
                    row.translatesAutoresizingMaskIntoConstraints = NO;

                    NSString* labelStr = [NSString stringWithUTF8String:field.label.c_str()];
                    NSTextField* label = makeLabel(labelStr, 11.0);
                    [label.widthAnchor constraintEqualToConstant:60].active = YES;

                    NSSlider* slider = [NSSlider sliderWithValue:field.value
                                                        minValue:field.minVal
                                                        maxValue:field.maxVal
                                                          target:impl_->delegate
                                                          action:@selector(sliderChanged:)];
                    slider.translatesAutoresizingMaskIntoConstraints = NO;
                    slider.tag = field.fieldId;
                    slider.continuous = YES;

                    NSTextField* valueLabel =
                        makeLabel([NSString stringWithFormat:@"%.2f", field.value], 10.0);
                    valueLabel.tag = field.fieldId + 10000;

                    [row addArrangedSubview:label];
                    [row addArrangedSubview:slider];
                    [row addArrangedSubview:valueLabel];
                    [targetStack addArrangedSubview:row];
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)row);
                    break;
                }
                case PropertyField::Type::DropdownField:
                {
                    NSStackView* row = [NSStackView stackViewWithViews:@[]];
                    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
                    row.spacing = 6.0;
                    row.translatesAutoresizingMaskIntoConstraints = NO;

                    NSString* labelStr = [NSString stringWithUTF8String:field.label.c_str()];
                    NSTextField* label = makeLabel(labelStr, 11.0);
                    [label.widthAnchor constraintEqualToConstant:60].active = YES;

                    NSPopUpButton* popup =
                        [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 110, 24)
                                                   pullsDown:NO];
                    popup.translatesAutoresizingMaskIntoConstraints = NO;
                    popup.tag = field.fieldId;
                    popup.target = impl_->delegate;
                    popup.action = @selector(popupChanged:);
                    for (const auto& opt : field.options)
                    {
                        [popup addItemWithTitle:[NSString stringWithUTF8String:opt.c_str()]];
                    }
                    if (field.currentIndex >= 0 && field.currentIndex < (int)field.options.size())
                    {
                        [popup selectItemAtIndex:field.currentIndex];
                    }

                    [row addArrangedSubview:label];
                    [row addArrangedSubview:popup];
                    [targetStack addArrangedSubview:row];
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)row);
                    break;
                }
                case PropertyField::Type::ColorField:
                {
                    NSStackView* row = [NSStackView stackViewWithViews:@[]];
                    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
                    row.spacing = 6.0;
                    row.translatesAutoresizingMaskIntoConstraints = NO;

                    NSString* labelStr = [NSString stringWithUTF8String:field.label.c_str()];
                    NSTextField* label = makeLabel(labelStr, 11.0);
                    [label.widthAnchor constraintEqualToConstant:60].active = YES;

                    NSColorWell* colorWell;
                    if (@available(macOS 13.0, *))
                    {
                        colorWell = [NSColorWell colorWellWithStyle:NSColorWellStyleMinimal];
                    }
                    else
                    {
                        colorWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(0, 0, 44, 24)];
                    }
                    colorWell.color = [NSColor colorWithRed:field.color[0]
                                                      green:field.color[1]
                                                       blue:field.color[2]
                                                      alpha:1.0];
                    colorWell.tag = field.fieldId;
                    colorWell.translatesAutoresizingMaskIntoConstraints = NO;
                    colorWell.target = impl_->delegate;
                    colorWell.action = @selector(colorWellChanged:);

                    NSString* colorStr =
                        [NSString stringWithFormat:@"(%.2f, %.2f, %.2f)", field.color[0],
                                                   field.color[1], field.color[2]];
                    NSTextField* colorLabel = makeLabel(colorStr, 10.0);

                    [row addArrangedSubview:label];
                    [row addArrangedSubview:colorWell];
                    [row addArrangedSubview:colorLabel];
                    [targetStack addArrangedSubview:row];
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)row);
                    break;
                }
                case PropertyField::Type::TextureField:
                {
                    NSStackView* row = [NSStackView stackViewWithViews:@[]];
                    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
                    row.spacing = 6.0;
                    row.translatesAutoresizingMaskIntoConstraints = NO;

                    NSString* labelStr = [NSString stringWithUTF8String:field.label.c_str()];
                    NSTextField* label = makeLabel(labelStr, 11.0);
                    [label.widthAnchor constraintEqualToConstant:60].active = YES;

                    // Display the basename of the current texture path, or
                    // "(none)" if the slot is empty. Truncate long names so
                    // the row stays a fixed width.
                    std::string display = "(none)";
                    if (!field.texturePath.empty())
                    {
                        const auto slash = field.texturePath.find_last_of('/');
                        display = (slash == std::string::npos)
                                      ? field.texturePath
                                      : field.texturePath.substr(slash + 1);
                        if (display.size() > 22)
                            display = display.substr(0, 19) + "...";
                    }
                    NSTextField* nameLabel =
                        makeLabel([NSString stringWithUTF8String:display.c_str()], 10.0);
                    nameLabel.textColor = field.texturePath.empty() ? [NSColor secondaryLabelColor]
                                                                    : [NSColor labelColor];
                    [nameLabel.widthAnchor constraintEqualToConstant:140].active = YES;

                    NSButton* browseBtn =
                        [NSButton buttonWithTitle:@"..."
                                           target:impl_->delegate
                                           action:@selector(textureBrowseClicked:)];
                    browseBtn.bezelStyle = NSBezelStyleRounded;
                    browseBtn.tag = field.fieldId;
                    browseBtn.translatesAutoresizingMaskIntoConstraints = NO;
                    [browseBtn.widthAnchor constraintEqualToConstant:32].active = YES;

                    [row addArrangedSubview:label];
                    [row addArrangedSubview:nameLabel];
                    [row addArrangedSubview:browseBtn];

                    // Only show the clear button when there is a texture
                    // bound to the slot. A small borderless "X" matching the
                    // visual weight of the browse button next to it.
                    if (!field.texturePath.empty())
                    {
                        NSButton* clearBtn =
                            [NSButton buttonWithTitle:@"X"
                                               target:impl_->delegate
                                               action:@selector(textureClearClicked:)];
                        clearBtn.bezelStyle = NSBezelStyleInline;
                        clearBtn.bordered = NO;
                        clearBtn.tag = field.fieldId;
                        clearBtn.translatesAutoresizingMaskIntoConstraints = NO;
                        clearBtn.toolTip = @"Clear texture slot";
                        [clearBtn.widthAnchor constraintEqualToConstant:20].active = YES;
                        [row addArrangedSubview:clearBtn];
                    }

                    [targetStack addArrangedSubview:row];
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)row);
                    break;
                }
            }
        }

        // "+ Add Component" footer button — always visible while an entity is
        // selected, opens a popup menu of addable components.
        {
            NSBox* sep = [[NSBox alloc] init];
            sep.boxType = NSBoxSeparator;
            sep.translatesAutoresizingMaskIntoConstraints = NO;
            [impl_->stackView addArrangedSubview:sep];
            [sep.widthAnchor constraintGreaterThanOrEqualToConstant:200].active = YES;
            impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)sep);

            NSButton* addButton = [NSButton buttonWithTitle:@"+ Add Component"
                                                     target:impl_->delegate
                                                     action:@selector(addComponentClicked:)];
            addButton.bezelStyle = NSBezelStyleRounded;
            addButton.translatesAutoresizingMaskIntoConstraints = NO;
            [impl_->stackView addArrangedSubview:addButton];
            impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)addButton);
        }

        // Force layout update on the document view.
        NSView* docView = impl_->scrollView.documentView;
        CGFloat contentHeight = impl_->stackView.fittingSize.height + 16.0;
        CGFloat viewWidth = docView.frame.size.width;
        if (viewWidth < 100)
            viewWidth = 250;
        [docView setFrameSize:NSMakeSize(viewWidth, contentHeight)];
    }
}

void CocoaPropertiesView::updateFieldValue(int /*fieldId*/, float /*value*/)
{
    // For now, properties are rebuilt when dirty. Fine-grained update deferred.
}

void CocoaPropertiesView::clear(const char* message)
{
    @autoreleasepool
    {
        for (NSView* view in [impl_->stackView.arrangedSubviews copy])
        {
            [impl_->stackView removeArrangedSubview:view];
            [view removeFromSuperview];
        }
        impl_->fieldViews.clear();

        NSString* msg = [NSString stringWithUTF8String:message];
        impl_->statusLabel = makeLabel(msg, 12.0);
        [impl_->stackView addArrangedSubview:impl_->statusLabel];
    }
}

}  // namespace engine::editor
