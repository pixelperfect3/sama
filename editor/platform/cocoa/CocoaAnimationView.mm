#include "editor/platform/cocoa/CocoaAnimationView.h"

#import <Cocoa/Cocoa.h>

// ---------------------------------------------------------------------------
// EventRowView -- a single row in the event list stack view, showing name
// and time as editable text fields. Defined before the delegate so that
// the delegate's methods can access its properties.
// ---------------------------------------------------------------------------

@interface EventRowView : NSStackView
@property(nonatomic, strong) NSTextField* nameField;
@property(nonatomic, strong) NSTextField* timeField;
@property(nonatomic, assign) int eventIndex;
@property(nonatomic, assign) BOOL isFired;
@end

@implementation EventRowView
@end

// ---------------------------------------------------------------------------
// AnimationViewDelegate -- target for the clip dropdown, transport buttons,
// scrubber, speed slider and loop checkbox. Holds owning blocks that wrap
// the C++ callbacks. suppressCallbacks is set during state pushes so we
// don't re-enter the UI when the C++ side updates widget values.
// ---------------------------------------------------------------------------

@interface AnimationViewDelegate : NSObject
@property(nonatomic, copy) void (^onClipSelected)(int index);
@property(nonatomic, copy) void (^onPlay)(void);
@property(nonatomic, copy) void (^onPause)(void);
@property(nonatomic, copy) void (^onStop)(void);
@property(nonatomic, copy) void (^onTimeChanged)(float time);
@property(nonatomic, copy) void (^onSpeedChanged)(float speed);
@property(nonatomic, copy) void (^onLoopChanged)(BOOL looping);
@property(nonatomic, copy) void (^onEventAdded)(float time, NSString* name);
@property(nonatomic, copy) void (^onEventRemoved)(int index);
@property(nonatomic, copy) void (^onEventEdited)(int index, float newTime, NSString* newName);
@property(nonatomic, copy) void (^onEventRowSelected)(int index);
@property(nonatomic, copy) void (^onStateForceSet)(int stateIndex);
@property(nonatomic, copy) void (^onParamChanged)(NSString* paramName, float value);
@property(nonatomic, assign) BOOL suppressCallbacks;
- (void)clipChanged:(NSPopUpButton*)sender;
- (void)playClicked:(NSButton*)sender;
- (void)pauseClicked:(NSButton*)sender;
- (void)stopClicked:(NSButton*)sender;
- (void)scrubberChanged:(NSSlider*)sender;
- (void)speedChanged:(NSSlider*)sender;
- (void)loopChanged:(NSButton*)sender;
- (void)addEventClicked:(NSButton*)sender;
- (void)removeEventClicked:(NSButton*)sender;
- (void)eventFieldEdited:(NSTextField*)sender;
- (void)eventRowClicked:(NSClickGestureRecognizer*)sender;
- (void)stateDropdownChanged:(NSPopUpButton*)sender;
- (void)paramSliderChanged:(NSSlider*)sender;
- (void)paramCheckboxChanged:(NSButton*)sender;
@end

@implementation AnimationViewDelegate
- (void)clipChanged:(NSPopUpButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onClipSelected)
        _onClipSelected((int)sender.indexOfSelectedItem);
}
- (void)playClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onPlay)
        _onPlay();
}
- (void)pauseClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onPause)
        _onPause();
}
- (void)stopClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onStop)
        _onStop();
}
- (void)scrubberChanged:(NSSlider*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onTimeChanged)
        _onTimeChanged((float)sender.doubleValue);
}
- (void)speedChanged:(NSSlider*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onSpeedChanged)
        _onSpeedChanged((float)sender.doubleValue);
}
- (void)loopChanged:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onLoopChanged)
        _onLoopChanged(sender.state == NSControlStateValueOn);
}
- (void)addEventClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    // The C++ side will set onEventAdded with the proper time and name.
    // We pass 0.0f / "event" here; the actual time comes from the C++ wrapper
    // which reads the current scrubber time before invoking. However, since
    // the delegate doesn't have direct access to the scrubber time, we use a
    // sentinel that the setEventAddedCallback wiring overrides. In practice,
    // the C++ wrapper sets the block to capture the current time.
    if (_onEventAdded)
        _onEventAdded(0.0f, @"event");
    (void)sender;
}
- (void)removeEventClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onEventRemoved)
        _onEventRemoved(-1);  // -1 = selected index; resolved by C++ side.
    (void)sender;
}
- (void)eventFieldEdited:(NSTextField*)sender
{
    if (_suppressCallbacks)
        return;
    if (!_onEventEdited)
        return;

    // Walk up to find the EventRowView parent.
    NSView* parent = sender.superview;
    while (parent && ![parent isKindOfClass:[EventRowView class]])
        parent = parent.superview;
    if (!parent)
        return;

    EventRowView* row = (EventRowView*)parent;
    int idx = row.eventIndex;
    NSString* newName = row.nameField.stringValue;
    float newTime = (float)row.timeField.doubleValue;
    _onEventEdited(idx, newTime, newName);
}
- (void)eventRowClicked:(NSClickGestureRecognizer*)sender
{
    NSView* view = sender.view;
    if (![view isKindOfClass:[EventRowView class]])
        return;
    EventRowView* row = (EventRowView*)view;
    if (_onEventRowSelected)
        _onEventRowSelected(row.eventIndex);
}
- (void)stateDropdownChanged:(NSPopUpButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onStateForceSet)
        _onStateForceSet((int)sender.indexOfSelectedItem);
}
- (void)paramSliderChanged:(NSSlider*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onParamChanged)
    {
        NSString* paramName = sender.toolTip;
        if (paramName)
            _onParamChanged(paramName, (float)sender.doubleValue);
    }
}
- (void)paramCheckboxChanged:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onParamChanged)
    {
        NSString* paramName = sender.toolTip;
        if (paramName)
        {
            float value = (sender.state == NSControlStateValueOn) ? 1.0f : 0.0f;
            _onParamChanged(paramName, value);
        }
    }
}
@end

// ---------------------------------------------------------------------------
// EventMarkerView -- draws small inverted triangles above the scrubber
// at normalized positions corresponding to animation events.
// ---------------------------------------------------------------------------

@interface EventMarkerView : NSView
@property(nonatomic, strong) NSArray<NSNumber*>* markerPositions;
@property(nonatomic, strong) NSSet<NSNumber*>* firedIndices;
@property(nonatomic, assign) int selectedIndex;
@end

@implementation EventMarkerView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        _markerPositions = @[];
        _firedIndices = [NSSet set];
        _selectedIndex = -1;
    }
    return self;
}

- (void)drawRect:(NSRect)dirtyRect
{
    [super drawRect:dirtyRect];

    NSRect bounds = self.bounds;
    CGFloat markerSize = 6.0;
    CGFloat halfMarker = markerSize / 2.0;

    for (NSUInteger i = 0; i < _markerPositions.count; ++i)
    {
        CGFloat normalized = _markerPositions[i].doubleValue;
        CGFloat x = bounds.origin.x + normalized * bounds.size.width;
        CGFloat midY = bounds.size.height / 2.0;

        NSBezierPath* triangle = [NSBezierPath bezierPath];
        [triangle moveToPoint:NSMakePoint(x - halfMarker, midY + halfMarker)];
        [triangle lineToPoint:NSMakePoint(x + halfMarker, midY + halfMarker)];
        [triangle lineToPoint:NSMakePoint(x, midY - halfMarker)];
        [triangle closePath];

        NSColor* fillColor;
        if ([_firedIndices containsObject:@(i)])
            fillColor = [NSColor systemYellowColor];
        else if ((int)i == _selectedIndex)
            fillColor = [NSColor systemBlueColor];
        else
            fillColor = [NSColor systemGrayColor];

        [fillColor setFill];
        [triangle fill];
    }
}

@end

namespace engine::editor
{

struct CocoaAnimationView::Impl
{
    NSView* containerView = nil;
    NSStackView* rootStack = nil;

    NSTextField* statusLabel = nil;
    NSPopUpButton* clipDropdown = nil;

    NSButton* playButton = nil;
    NSButton* pauseButton = nil;
    NSButton* stopButton = nil;

    NSSlider* scrubber = nil;
    NSTextField* timeLabel = nil;

    NSSlider* speedSlider = nil;
    NSTextField* speedLabel = nil;
    NSButton* loopCheckbox = nil;

    EventMarkerView* eventMarkerView = nil;
    NSStackView* eventListStack = nil;
    NSButton* addEventButton = nil;
    NSButton* removeEventButton = nil;
    int selectedEventIndex = -1;
    int eventCounter = 0;  // for generating default event names
    float currentScrubberTime = 0.0f;

    // State machine section.
    NSStackView* smSectionStack = nil;
    NSTextField* smStateLabel = nil;
    NSPopUpButton* smStateDropdown = nil;
    NSStackView* smParamStack = nil;

    AnimationViewDelegate* delegate = nil;

    ClipSelectedCallback clipSelectedCb;
    PlayCallback playCb;
    PauseCallback pauseCb;
    StopCallback stopCb;
    TimeChangedCallback timeChangedCb;
    SpeedChangedCallback speedChangedCb;
    LoopChangedCallback loopChangedCb;
    EventAddedCallback eventAddedCb;
    EventRemovedCallback eventRemovedCb;
    EventEditedCallback eventEditedCb;
    StateForceSetCallback stateForceSetCb;
    ParamChangedCallback paramChangedCb;
};

CocoaAnimationView::CocoaAnimationView() : impl_(std::make_unique<Impl>())
{
    @autoreleasepool
    {
        impl_->containerView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 150)];
        impl_->containerView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

        impl_->delegate = [[AnimationViewDelegate alloc] init];

        // Wire row-selection to update the selected event index.
        auto* implPtr = impl_.get();
        impl_->delegate.onEventRowSelected = ^(int index) {
          implPtr->selectedEventIndex = index;
        };

        // Top row: status label + clip dropdown.
        impl_->statusLabel =
            [NSTextField labelWithString:@"Animation: (no animated entity selected)"];
        impl_->statusLabel.font = [NSFont systemFontOfSize:11.0];
        impl_->statusLabel.textColor = [NSColor labelColor];

        impl_->clipDropdown = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 200, 22)
                                                         pullsDown:NO];
        impl_->clipDropdown.controlSize = NSControlSizeSmall;
        impl_->clipDropdown.font = [NSFont systemFontOfSize:11.0];
        impl_->clipDropdown.target = impl_->delegate;
        impl_->clipDropdown.action = @selector(clipChanged:);
        [impl_->clipDropdown addItemWithTitle:@"(no clip)"];
        impl_->clipDropdown.enabled = NO;

        NSStackView* topRow =
            [NSStackView stackViewWithViews:@[ impl_->statusLabel, impl_->clipDropdown ]];
        topRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        topRow.alignment = NSLayoutAttributeCenterY;
        topRow.spacing = 8.0;

        // Middle row: transport buttons.
        auto makeTransportButton = [](NSString* title, SEL action, id target) -> NSButton*
        {
            NSButton* b = [NSButton buttonWithTitle:title target:target action:action];
            b.bezelStyle = NSBezelStyleRounded;
            b.controlSize = NSControlSizeSmall;
            b.font = [NSFont systemFontOfSize:11.0];
            b.enabled = NO;
            return b;
        };

        impl_->playButton =
            makeTransportButton(@"\u25B6", @selector(playClicked:), impl_->delegate);
        impl_->pauseButton =
            makeTransportButton(@"\u23F8", @selector(pauseClicked:), impl_->delegate);
        impl_->stopButton =
            makeTransportButton(@"\u23F9", @selector(stopClicked:), impl_->delegate);

        NSStackView* transportRow = [NSStackView
            stackViewWithViews:@[ impl_->playButton, impl_->pauseButton, impl_->stopButton ]];
        transportRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        transportRow.alignment = NSLayoutAttributeCenterY;
        transportRow.spacing = 6.0;

        // Scrubber row: time slider + time label.
        impl_->scrubber = [NSSlider sliderWithValue:0.0
                                           minValue:0.0
                                           maxValue:1.0
                                             target:impl_->delegate
                                             action:@selector(scrubberChanged:)];
        impl_->scrubber.controlSize = NSControlSizeSmall;
        impl_->scrubber.continuous = YES;
        impl_->scrubber.enabled = NO;
        [impl_->scrubber setContentHuggingPriority:200
                                    forOrientation:NSLayoutConstraintOrientationHorizontal];

        impl_->timeLabel = [NSTextField labelWithString:@"0.00 / 0.00"];
        impl_->timeLabel.font = [NSFont monospacedDigitSystemFontOfSize:10.0
                                                                 weight:NSFontWeightRegular];
        impl_->timeLabel.textColor = [NSColor secondaryLabelColor];
        [impl_->timeLabel setContentHuggingPriority:750
                                     forOrientation:NSLayoutConstraintOrientationHorizontal];

        NSStackView* scrubberRow =
            [NSStackView stackViewWithViews:@[ impl_->scrubber, impl_->timeLabel ]];
        scrubberRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        scrubberRow.alignment = NSLayoutAttributeCenterY;
        scrubberRow.spacing = 8.0;

        // Speed + loop row: "Speed:" label, speed slider, speed value label, loop checkbox.
        NSTextField* speedCaption = [NSTextField labelWithString:@"Speed:"];
        speedCaption.font = [NSFont systemFontOfSize:11.0];
        speedCaption.textColor = [NSColor labelColor];

        impl_->speedSlider = [NSSlider sliderWithValue:1.0
                                              minValue:0.0
                                              maxValue:3.0
                                                target:impl_->delegate
                                                action:@selector(speedChanged:)];
        impl_->speedSlider.controlSize = NSControlSizeSmall;
        impl_->speedSlider.continuous = YES;
        impl_->speedSlider.enabled = NO;
        [impl_->speedSlider setContentHuggingPriority:200
                                       forOrientation:NSLayoutConstraintOrientationHorizontal];

        impl_->speedLabel = [NSTextField labelWithString:@"1.00x"];
        impl_->speedLabel.font = [NSFont monospacedDigitSystemFontOfSize:10.0
                                                                  weight:NSFontWeightRegular];
        impl_->speedLabel.textColor = [NSColor secondaryLabelColor];
        [impl_->speedLabel setContentHuggingPriority:750
                                      forOrientation:NSLayoutConstraintOrientationHorizontal];

        impl_->loopCheckbox = [NSButton checkboxWithTitle:@"Loop"
                                                   target:impl_->delegate
                                                   action:@selector(loopChanged:)];
        impl_->loopCheckbox.controlSize = NSControlSizeSmall;
        impl_->loopCheckbox.font = [NSFont systemFontOfSize:11.0];
        impl_->loopCheckbox.enabled = NO;

        NSStackView* speedRow = [NSStackView stackViewWithViews:@[
            speedCaption, impl_->speedSlider, impl_->speedLabel, impl_->loopCheckbox
        ]];
        speedRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        speedRow.alignment = NSLayoutAttributeCenterY;
        speedRow.spacing = 6.0;

        // Event marker overlay (sits below the scrubber row).
        impl_->eventMarkerView = [[EventMarkerView alloc] initWithFrame:NSMakeRect(0, 0, 400, 14)];
        impl_->eventMarkerView.translatesAutoresizingMaskIntoConstraints = NO;
        [impl_->eventMarkerView
            addConstraint:[NSLayoutConstraint constraintWithItem:impl_->eventMarkerView
                                                       attribute:NSLayoutAttributeHeight
                                                       relatedBy:NSLayoutRelationEqual
                                                          toItem:nil
                                                       attribute:NSLayoutAttributeNotAnAttribute
                                                      multiplier:1.0
                                                        constant:14.0]];

        // Event list header: "Events" label + add/remove buttons.
        NSTextField* eventsCaption = [NSTextField labelWithString:@"Events"];
        eventsCaption.font = [NSFont boldSystemFontOfSize:11.0];
        eventsCaption.textColor = [NSColor labelColor];

        impl_->addEventButton = [NSButton buttonWithTitle:@"+" target:nil action:nil];
        impl_->addEventButton.bezelStyle = NSBezelStyleRounded;
        impl_->addEventButton.controlSize = NSControlSizeSmall;
        impl_->addEventButton.font = [NSFont systemFontOfSize:11.0];
        impl_->addEventButton.enabled = NO;

        impl_->removeEventButton = [NSButton buttonWithTitle:@"\u2212" target:nil action:nil];
        impl_->removeEventButton.bezelStyle = NSBezelStyleRounded;
        impl_->removeEventButton.controlSize = NSControlSizeSmall;
        impl_->removeEventButton.font = [NSFont systemFontOfSize:11.0];
        impl_->removeEventButton.enabled = NO;

        NSStackView* eventHeaderRow = [NSStackView
            stackViewWithViews:@[ eventsCaption, impl_->addEventButton, impl_->removeEventButton ]];
        eventHeaderRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        eventHeaderRow.alignment = NSLayoutAttributeCenterY;
        eventHeaderRow.spacing = 4.0;

        // Event list: simple stack view (no scroll view — event counts are small).
        impl_->eventListStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
        impl_->eventListStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->eventListStack.alignment = NSLayoutAttributeLeading;
        impl_->eventListStack.spacing = 2.0;
        impl_->eventListStack.translatesAutoresizingMaskIntoConstraints = NO;

        // State Machine section (hidden by default; shown when hasStateMachine).
        NSTextField* smCaption = [NSTextField labelWithString:@"State Machine"];
        smCaption.font = [NSFont boldSystemFontOfSize:11.0];
        smCaption.textColor = [NSColor labelColor];

        NSTextField* smStateCaptionLabel = [NSTextField labelWithString:@"State:"];
        smStateCaptionLabel.font = [NSFont systemFontOfSize:11.0];
        smStateCaptionLabel.textColor = [NSColor labelColor];

        impl_->smStateLabel = [NSTextField labelWithString:@"(none)"];
        impl_->smStateLabel.font = [NSFont systemFontOfSize:11.0];
        impl_->smStateLabel.textColor = [NSColor secondaryLabelColor];

        NSStackView* smStateLabelRow =
            [NSStackView stackViewWithViews:@[ smStateCaptionLabel, impl_->smStateLabel ]];
        smStateLabelRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        smStateLabelRow.alignment = NSLayoutAttributeCenterY;
        smStateLabelRow.spacing = 4.0;

        impl_->smStateDropdown = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 200, 22)
                                                            pullsDown:NO];
        impl_->smStateDropdown.controlSize = NSControlSizeSmall;
        impl_->smStateDropdown.font = [NSFont systemFontOfSize:11.0];
        impl_->smStateDropdown.target = impl_->delegate;
        impl_->smStateDropdown.action = @selector(stateDropdownChanged:);
        [impl_->smStateDropdown addItemWithTitle:@"(none)"];
        impl_->smStateDropdown.enabled = NO;

        impl_->smParamStack = [[NSStackView alloc] initWithFrame:NSMakeRect(0, 0, 400, 0)];
        impl_->smParamStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->smParamStack.alignment = NSLayoutAttributeLeading;
        impl_->smParamStack.spacing = 4.0;
        impl_->smParamStack.translatesAutoresizingMaskIntoConstraints = NO;

        impl_->smSectionStack = [NSStackView stackViewWithViews:@[
            smCaption, smStateLabelRow, impl_->smStateDropdown, impl_->smParamStack
        ]];
        impl_->smSectionStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->smSectionStack.alignment = NSLayoutAttributeLeading;
        impl_->smSectionStack.spacing = 4.0;
        impl_->smSectionStack.hidden = YES;

        // Root vertical stack.
        impl_->rootStack = [NSStackView stackViewWithViews:@[
            topRow, transportRow, scrubberRow, impl_->eventMarkerView, speedRow, eventHeaderRow,
            impl_->eventListStack, impl_->smSectionStack
        ]];
        impl_->rootStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->rootStack.alignment = NSLayoutAttributeLeading;
        impl_->rootStack.spacing = 6.0;
        impl_->rootStack.translatesAutoresizingMaskIntoConstraints = NO;

        [impl_->containerView addSubview:impl_->rootStack];
        [NSLayoutConstraint activateConstraints:@[
            [impl_->rootStack.topAnchor constraintEqualToAnchor:impl_->containerView.topAnchor
                                                       constant:8],
            [impl_->rootStack.leadingAnchor
                constraintEqualToAnchor:impl_->containerView.leadingAnchor
                               constant:8],
            [impl_->rootStack.trailingAnchor
                constraintLessThanOrEqualToAnchor:impl_->containerView.trailingAnchor
                                         constant:-8],
            // Align the event marker view with the scrubber track so
            // triangle positions match the slider's visual range.
            [impl_->eventMarkerView.leadingAnchor
                constraintEqualToAnchor:impl_->scrubber.leadingAnchor],
            [impl_->eventMarkerView.trailingAnchor
                constraintEqualToAnchor:impl_->scrubber.trailingAnchor],
            // Make the event list stretch too.
            [impl_->eventListStack.leadingAnchor
                constraintEqualToAnchor:impl_->rootStack.leadingAnchor],
            [impl_->eventListStack.trailingAnchor
                constraintEqualToAnchor:impl_->rootStack.trailingAnchor],
        ]];
    }
}

CocoaAnimationView::~CocoaAnimationView()
{
    @autoreleasepool
    {
        impl_->containerView = nil;
        impl_->rootStack = nil;
        impl_->statusLabel = nil;
        impl_->clipDropdown = nil;
        impl_->playButton = nil;
        impl_->pauseButton = nil;
        impl_->stopButton = nil;
        impl_->scrubber = nil;
        impl_->timeLabel = nil;
        impl_->speedSlider = nil;
        impl_->speedLabel = nil;
        impl_->loopCheckbox = nil;
        impl_->eventMarkerView = nil;
        impl_->eventListStack = nil;
        impl_->addEventButton = nil;
        impl_->removeEventButton = nil;
        impl_->smSectionStack = nil;
        impl_->smStateLabel = nil;
        impl_->smStateDropdown = nil;
        impl_->smParamStack = nil;
        impl_->delegate = nil;
    }
}

void* CocoaAnimationView::nativeView() const
{
    return (__bridge void*)impl_->containerView;
}

void CocoaAnimationView::setState(const AnimationViewState& s)
{
    @autoreleasepool
    {
        impl_->delegate.suppressCallbacks = YES;

        // Status label.
        NSString* statusText;
        if (!s.hasAnimation)
        {
            statusText = @"Animation: (no animated entity selected)";
        }
        else
        {
            NSString* asset = [NSString
                stringWithUTF8String:(s.assetLabel.empty() ? "entity" : s.assetLabel.c_str())];
            NSString* clipName = @"(none)";
            if (s.currentClipIndex >= 0 &&
                static_cast<size_t>(s.currentClipIndex) < s.clipNames.size())
            {
                clipName = [NSString stringWithUTF8String:s.clipNames[s.currentClipIndex].c_str()];
            }
            statusText = [NSString stringWithFormat:@"Animation: %@ / \"%@\"", asset, clipName];
        }
        impl_->statusLabel.stringValue = statusText;

        // Rebuild dropdown items only when the list changed.
        NSMutableArray<NSString*>* newItems = [NSMutableArray array];
        for (const auto& n : s.clipNames)
            [newItems addObject:[NSString stringWithUTF8String:n.c_str()]];

        BOOL listChanged = NO;
        if ((NSInteger)newItems.count != impl_->clipDropdown.numberOfItems)
        {
            listChanged = YES;
        }
        else
        {
            for (NSInteger i = 0; i < (NSInteger)newItems.count; ++i)
            {
                if (![[impl_->clipDropdown itemTitleAtIndex:i] isEqualToString:newItems[i]])
                {
                    listChanged = YES;
                    break;
                }
            }
        }

        if (listChanged)
        {
            [impl_->clipDropdown removeAllItems];
            if (newItems.count == 0)
            {
                [impl_->clipDropdown addItemWithTitle:@"(no clip)"];
            }
            else
            {
                [impl_->clipDropdown addItemsWithTitles:newItems];
            }
        }

        if (s.hasAnimation && s.currentClipIndex >= 0 &&
            s.currentClipIndex < impl_->clipDropdown.numberOfItems)
        {
            [impl_->clipDropdown selectItemAtIndex:s.currentClipIndex];
        }

        // Scrubber.
        impl_->scrubber.maxValue = (s.duration > 0.0f) ? s.duration : 1.0;
        impl_->scrubber.doubleValue = s.currentTime;

        NSString* timeText = [NSString stringWithFormat:@"%.2f / %.2f", s.currentTime, s.duration];
        impl_->timeLabel.stringValue = timeText;

        // Speed slider + label.
        impl_->speedSlider.doubleValue = s.speed;
        impl_->speedLabel.stringValue = [NSString stringWithFormat:@"%.2fx", s.speed];

        // Loop checkbox.
        impl_->loopCheckbox.state = s.looping ? NSControlStateValueOn : NSControlStateValueOff;

        // Enable/disable controls.
        const BOOL enabled = s.hasAnimation ? YES : NO;
        impl_->clipDropdown.enabled = enabled;
        impl_->playButton.enabled = enabled;
        impl_->pauseButton.enabled = enabled;
        impl_->stopButton.enabled = enabled;
        impl_->scrubber.enabled = enabled;
        impl_->speedSlider.enabled = enabled;
        impl_->loopCheckbox.enabled = enabled;
        impl_->addEventButton.enabled = enabled;
        impl_->removeEventButton.enabled = enabled;

        // Track current scrubber time for the add-event button.
        impl_->currentScrubberTime = s.currentTime;

        // Build a set of fired event names for flash highlighting.
        NSMutableSet<NSString*>* firedNames = [NSMutableSet set];
        for (const auto& f : s.firedEvents)
            [firedNames addObject:[NSString stringWithUTF8String:f.c_str()]];

        // Update event marker overlay.
        {
            NSMutableArray<NSNumber*>* positions = [NSMutableArray array];
            NSMutableSet<NSNumber*>* firedIdx = [NSMutableSet set];
            const float dur = (s.duration > 0.0f) ? s.duration : 1.0f;
            for (size_t i = 0; i < s.events.size(); ++i)
            {
                float normalized = s.events[i].time / dur;
                [positions addObject:@(normalized)];
                NSString* eName = [NSString stringWithUTF8String:s.events[i].name.c_str()];
                if ([firedNames containsObject:eName])
                    [firedIdx addObject:@(i)];
            }
            impl_->eventMarkerView.markerPositions = positions;
            impl_->eventMarkerView.firedIndices = firedIdx;
            impl_->eventMarkerView.selectedIndex = impl_->selectedEventIndex;
            [impl_->eventMarkerView setNeedsDisplay:YES];
        }

        // Update event list rows.
        // Only rebuild if the event count changed; otherwise just update
        // fired/selected state on existing rows to avoid flicker.
        {
            NSArray<NSView*>* existing = impl_->eventListStack.arrangedSubviews;
            bool needsRebuild = (existing.count != s.events.size());
            if (!needsRebuild)
            {
                for (NSUInteger i = 0; i < existing.count; ++i)
                {
                    if (![existing[i] isKindOfClass:[EventRowView class]])
                    {
                        needsRebuild = true;
                        break;
                    }
                    EventRowView* row = (EventRowView*)existing[i];
                    NSString* eName = [NSString stringWithUTF8String:s.events[i].name.c_str()];
                    if (![row.nameField.stringValue isEqualToString:eName])
                    {
                        needsRebuild = true;
                        break;
                    }
                }
            }

            if (!needsRebuild)
            {
                // Fast path: just update fired/selected highlights.
                for (NSUInteger i = 0; i < existing.count; ++i)
                {
                    EventRowView* row = (EventRowView*)existing[i];
                    NSString* eName = [NSString stringWithUTF8String:s.events[i].name.c_str()];
                    BOOL fired = [firedNames containsObject:eName];
                    row.isFired = fired;
                    row.nameField.textColor =
                        fired ? [NSColor systemYellowColor] : [NSColor labelColor];
                    row.timeField.textColor =
                        fired ? [NSColor systemYellowColor] : [NSColor secondaryLabelColor];

                    if ((int)i == impl_->selectedEventIndex)
                    {
                        row.wantsLayer = YES;
                        row.layer.backgroundColor =
                            [NSColor selectedContentBackgroundColor].CGColor;
                        row.layer.cornerRadius = 3.0;
                    }
                    else
                    {
                        row.layer.backgroundColor = [NSColor clearColor].CGColor;
                    }
                }
            }
            else
            {
                // Full rebuild: remove all rows and recreate.
                NSArray<NSView*>* oldRows = [existing copy];
                for (NSView* v in oldRows)
                {
                    [impl_->eventListStack removeArrangedSubview:v];
                    [v removeFromSuperview];
                }

                // Weak-capture impl for edit callbacks.
                auto* implPtr = impl_.get();

                for (size_t i = 0; i < s.events.size(); ++i)
                {
                    EventRowView* row = [[EventRowView alloc] initWithFrame:NSZeroRect];
                    row.translatesAutoresizingMaskIntoConstraints = NO;
                    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
                    row.alignment = NSLayoutAttributeCenterY;
                    row.spacing = 6.0;
                    row.eventIndex = (int)i;

                    NSString* eName = [NSString stringWithUTF8String:s.events[i].name.c_str()];
                    BOOL fired = [firedNames containsObject:eName];
                    row.isFired = fired;

                    // Name field (editable).
                    NSTextField* nameField =
                        [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 150, 20)];
                    nameField.stringValue = eName;
                    nameField.font = [NSFont systemFontOfSize:11.0];
                    nameField.bordered = YES;
                    nameField.bezeled = YES;
                    nameField.editable = YES;
                    nameField.selectable = YES;
                    nameField.bezelStyle = NSTextFieldRoundedBezel;
                    nameField.controlSize = NSControlSizeSmall;
                    if (fired)
                        nameField.textColor = [NSColor systemYellowColor];
                    else
                        nameField.textColor = [NSColor labelColor];
                    [nameField setContentHuggingPriority:200
                                          forOrientation:NSLayoutConstraintOrientationHorizontal];
                    row.nameField = nameField;

                    // Time field (editable).
                    NSTextField* timeField =
                        [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 60, 20)];
                    timeField.stringValue = [NSString stringWithFormat:@"%.3f", s.events[i].time];
                    timeField.font = [NSFont monospacedDigitSystemFontOfSize:10.0
                                                                      weight:NSFontWeightRegular];
                    timeField.bordered = YES;
                    timeField.bezeled = YES;
                    timeField.editable = YES;
                    timeField.selectable = YES;
                    timeField.bezelStyle = NSTextFieldRoundedBezel;
                    timeField.controlSize = NSControlSizeSmall;
                    if (fired)
                        timeField.textColor = [NSColor systemYellowColor];
                    else
                        timeField.textColor = [NSColor secondaryLabelColor];
                    [timeField setContentHuggingPriority:750
                                          forOrientation:NSLayoutConstraintOrientationHorizontal];
                    row.timeField = timeField;

                    // Wire up editing via target/action on the delegate.
                    nameField.target = implPtr->delegate;
                    nameField.tag = (int)i;
                    timeField.target = implPtr->delegate;
                    timeField.tag = (int)i;
                    nameField.action = @selector(eventFieldEdited:);
                    timeField.action = @selector(eventFieldEdited:);

                    // Highlight the selected row.
                    if ((int)i == implPtr->selectedEventIndex)
                    {
                        row.wantsLayer = YES;
                        row.layer.backgroundColor =
                            [NSColor selectedContentBackgroundColor].CGColor;
                        row.layer.cornerRadius = 3.0;
                    }

                    // Add a click gesture to select this row.
                    NSClickGestureRecognizer* click = [[NSClickGestureRecognizer alloc]
                        initWithTarget:implPtr->delegate
                                action:@selector(eventRowClicked:)];
                    [row addGestureRecognizer:click];

                    [row addArrangedSubview:nameField];
                    [row addArrangedSubview:timeField];
                    [impl_->eventListStack addArrangedSubview:row];
                }

                [impl_->eventListStack layoutSubtreeIfNeeded];
            }  // end else (full rebuild)
        }

        // Update state machine section.
        impl_->smSectionStack.hidden = !s.hasStateMachine;
        if (s.hasStateMachine)
        {
            // Current state label.
            NSString* stateStr = s.currentStateName.empty()
                                     ? @"(none)"
                                     : [NSString stringWithUTF8String:s.currentStateName.c_str()];
            impl_->smStateLabel.stringValue = stateStr;

            // State dropdown.
            {
                NSMutableArray<NSString*>* stateItems = [NSMutableArray array];
                for (const auto& sn : s.stateNames)
                    [stateItems addObject:[NSString stringWithUTF8String:sn.c_str()]];

                BOOL stateListChanged = NO;
                if ((NSInteger)stateItems.count != impl_->smStateDropdown.numberOfItems)
                {
                    stateListChanged = YES;
                }
                else
                {
                    for (NSInteger i = 0; i < (NSInteger)stateItems.count; ++i)
                    {
                        if (![[impl_->smStateDropdown itemTitleAtIndex:i]
                                isEqualToString:stateItems[i]])
                        {
                            stateListChanged = YES;
                            break;
                        }
                    }
                }

                if (stateListChanged)
                {
                    [impl_->smStateDropdown removeAllItems];
                    if (stateItems.count == 0)
                        [impl_->smStateDropdown addItemWithTitle:@"(none)"];
                    else
                        [impl_->smStateDropdown addItemsWithTitles:stateItems];
                }

                impl_->smStateDropdown.enabled = (stateItems.count > 0);
                if (s.currentStateIndex >= 0 &&
                    s.currentStateIndex < impl_->smStateDropdown.numberOfItems)
                {
                    [impl_->smStateDropdown selectItemAtIndex:s.currentStateIndex];
                }
            }

            // Parameter rows.
            {
                NSArray<NSView*>* existingParams = [impl_->smParamStack.arrangedSubviews copy];
                for (NSView* v in existingParams)
                {
                    [impl_->smParamStack removeArrangedSubview:v];
                    [v removeFromSuperview];
                }

                for (const auto& p : s.params)
                {
                    NSString* paramName = [NSString stringWithUTF8String:p.name.c_str()];

                    NSTextField* label = [NSTextField labelWithString:paramName];
                    label.font = [NSFont systemFontOfSize:11.0];
                    label.textColor = [NSColor labelColor];
                    [label setContentHuggingPriority:750
                                      forOrientation:NSLayoutConstraintOrientationHorizontal];

                    if (p.isBool)
                    {
                        NSButton* checkbox =
                            [NSButton checkboxWithTitle:@""
                                                 target:impl_->delegate
                                                 action:@selector(paramCheckboxChanged:)];
                        checkbox.controlSize = NSControlSizeSmall;
                        checkbox.state =
                            (p.value > 0.5f) ? NSControlStateValueOn : NSControlStateValueOff;
                        checkbox.toolTip = paramName;

                        NSStackView* row = [NSStackView stackViewWithViews:@[ label, checkbox ]];
                        row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
                        row.alignment = NSLayoutAttributeCenterY;
                        row.spacing = 6.0;
                        [impl_->smParamStack addArrangedSubview:row];
                    }
                    else
                    {
                        NSSlider* slider =
                            [NSSlider sliderWithValue:p.value
                                             minValue:0.0
                                             maxValue:3.0
                                               target:impl_->delegate
                                               action:@selector(paramSliderChanged:)];
                        slider.controlSize = NSControlSizeSmall;
                        slider.continuous = YES;
                        slider.toolTip = paramName;
                        [slider setContentHuggingPriority:200
                                           forOrientation:NSLayoutConstraintOrientationHorizontal];

                        NSTextField* valueLabel = [NSTextField
                            labelWithString:[NSString stringWithFormat:@"%.2f", p.value]];
                        valueLabel.font =
                            [NSFont monospacedDigitSystemFontOfSize:10.0
                                                             weight:NSFontWeightRegular];
                        valueLabel.textColor = [NSColor secondaryLabelColor];
                        [valueLabel
                            setContentHuggingPriority:750
                                       forOrientation:NSLayoutConstraintOrientationHorizontal];

                        NSStackView* row =
                            [NSStackView stackViewWithViews:@[ label, slider, valueLabel ]];
                        row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
                        row.alignment = NSLayoutAttributeCenterY;
                        row.spacing = 6.0;
                        [impl_->smParamStack addArrangedSubview:row];
                    }
                }
            }
        }

        impl_->delegate.suppressCallbacks = NO;
    }
}

void CocoaAnimationView::setClipSelectedCallback(ClipSelectedCallback cb)
{
    impl_->clipSelectedCb = std::move(cb);
    auto* captured = &impl_->clipSelectedCb;
    impl_->delegate.onClipSelected = ^(int index) {
      if (*captured)
          (*captured)(index);
    };
}

void CocoaAnimationView::setPlayCallback(PlayCallback cb)
{
    impl_->playCb = std::move(cb);
    auto* captured = &impl_->playCb;
    impl_->delegate.onPlay = ^{
      if (*captured)
          (*captured)();
    };
}

void CocoaAnimationView::setPauseCallback(PauseCallback cb)
{
    impl_->pauseCb = std::move(cb);
    auto* captured = &impl_->pauseCb;
    impl_->delegate.onPause = ^{
      if (*captured)
          (*captured)();
    };
}

void CocoaAnimationView::setStopCallback(StopCallback cb)
{
    impl_->stopCb = std::move(cb);
    auto* captured = &impl_->stopCb;
    impl_->delegate.onStop = ^{
      if (*captured)
          (*captured)();
    };
}

void CocoaAnimationView::setTimeChangedCallback(TimeChangedCallback cb)
{
    impl_->timeChangedCb = std::move(cb);
    auto* captured = &impl_->timeChangedCb;
    impl_->delegate.onTimeChanged = ^(float time) {
      if (*captured)
          (*captured)(time);
    };
}

void CocoaAnimationView::setSpeedChangedCallback(SpeedChangedCallback cb)
{
    impl_->speedChangedCb = std::move(cb);
    auto* captured = &impl_->speedChangedCb;
    impl_->delegate.onSpeedChanged = ^(float speed) {
      if (*captured)
          (*captured)(speed);
    };
}

void CocoaAnimationView::setLoopChangedCallback(LoopChangedCallback cb)
{
    impl_->loopChangedCb = std::move(cb);
    auto* captured = &impl_->loopChangedCb;
    impl_->delegate.onLoopChanged = ^(BOOL looping) {
      if (*captured)
          (*captured)(looping);
    };
}

void CocoaAnimationView::setEventAddedCallback(EventAddedCallback cb)
{
    impl_->eventAddedCb = std::move(cb);
    auto* implPtr = impl_.get();

    // The delegate's addEventClicked: calls onEventAdded with a sentinel
    // time of 0.0f. We override the block to use the real scrubber time
    // and generate a unique default name.
    impl_->delegate.onEventAdded = ^(float /*unusedTime*/, NSString* /*unusedName*/) {
      float time = implPtr->currentScrubberTime;
      std::string name = "event_" + std::to_string(implPtr->eventCounter++);
      if (implPtr->eventAddedCb)
          implPtr->eventAddedCb(time, name);
    };

    impl_->addEventButton.target = impl_->delegate;
    impl_->addEventButton.action = @selector(addEventClicked:);
}

void CocoaAnimationView::setEventRemovedCallback(EventRemovedCallback cb)
{
    impl_->eventRemovedCb = std::move(cb);
    auto* implPtr = impl_.get();

    // The delegate's removeEventClicked: calls onEventRemoved with -1.
    // We resolve -1 to the actual selected event index here.
    impl_->delegate.onEventRemoved = ^(int index) {
      int resolvedIndex = (index < 0) ? implPtr->selectedEventIndex : index;
      if (resolvedIndex >= 0 && implPtr->eventRemovedCb)
          implPtr->eventRemovedCb(resolvedIndex);
      implPtr->selectedEventIndex = -1;
    };

    impl_->removeEventButton.target = impl_->delegate;
    impl_->removeEventButton.action = @selector(removeEventClicked:);
}

void CocoaAnimationView::setEventEditedCallback(EventEditedCallback cb)
{
    impl_->eventEditedCb = std::move(cb);
    auto* implPtr = impl_.get();

    impl_->delegate.onEventEdited = ^(int index, float newTime, NSString* newName) {
      if (implPtr->eventEditedCb)
          implPtr->eventEditedCb(index, newTime, std::string([newName UTF8String]));
    };
}

void CocoaAnimationView::setStateForceSetCallback(StateForceSetCallback cb)
{
    impl_->stateForceSetCb = std::move(cb);
    auto* captured = &impl_->stateForceSetCb;
    impl_->delegate.onStateForceSet = ^(int stateIndex) {
      if (*captured)
          (*captured)(stateIndex);
    };
}

void CocoaAnimationView::setParamChangedCallback(ParamChangedCallback cb)
{
    impl_->paramChangedCb = std::move(cb);
    auto* captured = &impl_->paramChangedCb;
    impl_->delegate.onParamChanged = ^(NSString* paramName, float value) {
      if (*captured)
          (*captured)(std::string([paramName UTF8String]), value);
    };
}

}  // namespace engine::editor
