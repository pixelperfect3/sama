#include "editor/platform/cocoa/CocoaPropertiesView.h"

#import <Cocoa/Cocoa.h>

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
// Impl
// ---------------------------------------------------------------------------

namespace engine::editor
{

struct CocoaPropertiesView::Impl
{
    NSScrollView* scrollView = nil;
    NSStackView* stackView = nil;
    NSTextField* statusLabel = nil;
    ValueChangedCallback valueChangedCallback;
    ColorChangedCallback colorChangedCallback;
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
        NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 250, 400)];

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
}

void CocoaPropertiesView::setColorChangedCallback(ColorChangedCallback cb)
{
    impl_->colorChangedCallback = std::move(cb);
}

void CocoaPropertiesView::setProperties(const std::vector<PropertyField>& fields)
{
    @autoreleasepool
    {
        // Remove all existing views from the stack.
        for (NSView* view in [impl_->stackView.arrangedSubviews copy])
        {
            [impl_->stackView removeArrangedSubview:view];
            [view removeFromSuperview];
        }
        impl_->fieldViews.clear();

        if (fields.empty())
        {
            impl_->statusLabel = makeLabel(@"No entity selected", 12.0);
            [impl_->stackView addArrangedSubview:impl_->statusLabel];
            return;
        }

        for (const auto& field : fields)
        {
            switch (field.type)
            {
                case PropertyField::Type::Header:
                {
                    NSTextField* header =
                        makeHeader([NSString stringWithUTF8String:field.label.c_str()]);
                    [impl_->stackView addArrangedSubview:header];
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)header);
                    break;
                }
                case PropertyField::Type::Label:
                {
                    NSString* text = [NSString stringWithUTF8String:field.label.c_str()];
                    NSTextField* label = makeLabel(text, 11.0);
                    [impl_->stackView addArrangedSubview:label];
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

                    [row addArrangedSubview:label];
                    [row addArrangedSubview:valueField];
                    [impl_->stackView addArrangedSubview:row];
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
                                                          target:nil
                                                          action:nil];
                    slider.translatesAutoresizingMaskIntoConstraints = NO;
                    slider.tag = field.fieldId;

                    NSTextField* valueLabel =
                        makeLabel([NSString stringWithFormat:@"%.2f", field.value], 10.0);
                    valueLabel.tag = field.fieldId + 10000;

                    [row addArrangedSubview:label];
                    [row addArrangedSubview:slider];
                    [row addArrangedSubview:valueLabel];
                    [impl_->stackView addArrangedSubview:row];
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

                    NSString* colorStr =
                        [NSString stringWithFormat:@"(%.2f, %.2f, %.2f)", field.color[0],
                                                   field.color[1], field.color[2]];
                    NSTextField* colorLabel = makeLabel(colorStr, 10.0);

                    [row addArrangedSubview:label];
                    [row addArrangedSubview:colorWell];
                    [row addArrangedSubview:colorLabel];
                    [impl_->stackView addArrangedSubview:row];
                    impl_->fieldViews.push_back((__bridge NSView*)(__bridge void*)row);
                    break;
                }
            }
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
