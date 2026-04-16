#include "editor/platform/cocoa/CocoaAnimationView.h"

#import <Cocoa/Cocoa.h>
#import "editor/platform/cocoa/StateMachineGraphView.h"

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
// TaggableView -- simple NSView subclass with a settable integer tag.
// Used for clickable row containers in the state/transition lists.
// ---------------------------------------------------------------------------

@interface TaggableView : NSView
@property(nonatomic, assign) NSInteger viewTag;
@end

@implementation TaggableView
- (NSInteger)tag
{
    return _viewTag;
}
@end

// ---------------------------------------------------------------------------
// GraphViewDelegateAdapter -- bridges StateMachineGraphViewDelegate calls
// to the C++ callbacks stored in the Impl struct.
// ---------------------------------------------------------------------------

@interface GraphViewDelegateAdapter : NSObject <StateMachineGraphViewDelegate>
{
    @public
    void (^_stateSelectedBlock)(int stateIndex);
    void (^_transitionSelectedBlock)(int stateIndex, int transIndex);
    void (^_stateForceSetBlock)(int stateIndex);
    void (^_stateAddedBlock)(void);
    void (^_stateRemovedBlock)(int stateIndex);
}
@end

@implementation GraphViewDelegateAdapter

- (void)graphView:(StateMachineGraphView*)view didSelectState:(int)stateIndex
{
    if (_stateSelectedBlock)
        _stateSelectedBlock(stateIndex);
}

- (void)graphView:(StateMachineGraphView*)view
    didSelectTransition:(int)stateIndex
        transitionIndex:(int)transIndex
{
    if (_transitionSelectedBlock)
        _transitionSelectedBlock(stateIndex, transIndex);
}

- (void)graphView:(StateMachineGraphView*)view didForceSetState:(int)stateIndex
{
    if (_stateForceSetBlock)
        _stateForceSetBlock(stateIndex);
}

- (void)graphViewDidRequestAddState:(StateMachineGraphView*)view
{
    if (_stateAddedBlock)
        _stateAddedBlock();
}

- (void)graphView:(StateMachineGraphView*)view didRequestRemoveState:(int)stateIndex
{
    if (_stateRemovedBlock)
        _stateRemovedBlock(stateIndex);
}

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
@property(nonatomic, copy) void (^onStateAdded)(void);
@property(nonatomic, copy) void (^onStateRemoved)(int stateIndex);
@property(nonatomic, copy) void (^onStateEdited)
    (int stateIndex, NSString* name, int clipIndex, float speed, BOOL loop);
@property(nonatomic, copy) void (^onTransitionAdded)(int fromState, int toState, float blend);
@property(nonatomic, copy) void (^onTransitionRemoved)(int fromState, int transIdx);
@property(nonatomic, copy) void (^onTransitionEdited)
    (int fromState, int transIdx, int targetState, float blend, float exitTime, BOOL hasExitTime);
@property(nonatomic, copy) void (^onConditionAdded)
    (int fromState, int transIdx, NSString* param, int compare, float threshold);
@property(nonatomic, copy) void (^onConditionRemoved)(int fromState, int transIdx, int condIdx);
@property(nonatomic, copy) void (^onParamAdded)(NSString* name, BOOL isBool);
@property(nonatomic, copy) void (^onStateSelected)(int stateIndex);
@property(nonatomic, copy) void (^onTransitionSelected)(int stateIndex, int transIdx);
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
- (void)addStateClicked:(NSButton*)sender;
- (void)removeStateClicked:(NSButton*)sender;
- (void)stateRowClicked:(NSClickGestureRecognizer*)sender;
- (void)stateNameEdited:(NSTextField*)sender;
- (void)stateClipChanged:(NSPopUpButton*)sender;
- (void)stateSpeedChanged:(NSSlider*)sender;
- (void)stateLoopChanged:(NSButton*)sender;
- (void)addTransitionClicked:(NSButton*)sender;
- (void)removeTransitionClicked:(NSButton*)sender;
- (void)transitionRowClicked:(NSClickGestureRecognizer*)sender;
- (void)transTargetChanged:(NSPopUpButton*)sender;
- (void)transBlendEdited:(NSTextField*)sender;
- (void)transExitTimeCheckChanged:(NSButton*)sender;
- (void)transExitTimeEdited:(NSTextField*)sender;
- (void)addConditionClicked:(NSButton*)sender;
- (void)removeConditionClicked:(NSButton*)sender;
- (void)condParamEdited:(NSTextField*)sender;
- (void)condCompareChanged:(NSPopUpButton*)sender;
- (void)condThresholdEdited:(NSTextField*)sender;
- (void)addParamClicked:(NSButton*)sender;
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
- (void)addStateClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onStateAdded)
        _onStateAdded();
    (void)sender;
}
- (void)removeStateClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onStateRemoved)
        _onStateRemoved(-1);  // resolved by C++ side
    (void)sender;
}
- (void)stateRowClicked:(NSClickGestureRecognizer*)sender
{
    if (_suppressCallbacks)
        return;
    NSView* view = sender.view;
    int idx = (int)view.tag;
    if (_onStateSelected)
        _onStateSelected(idx);
}
- (void)stateNameEdited:(NSTextField*)sender
{
    if (_suppressCallbacks)
        return;
    // Tag encodes the state index.
    if (_onStateEdited)
        _onStateEdited((int)sender.tag, sender.stringValue, -1, -1.0f, NO);
    (void)sender;
}
- (void)stateClipChanged:(NSPopUpButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onStateEdited)
        _onStateEdited((int)sender.tag, nil, (int)sender.indexOfSelectedItem, -1.0f, NO);
}
- (void)stateSpeedChanged:(NSSlider*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onStateEdited)
        _onStateEdited((int)sender.tag, nil, -1, (float)sender.doubleValue, NO);
}
- (void)stateLoopChanged:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onStateEdited)
        _onStateEdited((int)sender.tag, nil, -1, -1.0f, sender.state == NSControlStateValueOn);
}
- (void)addTransitionClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onTransitionAdded)
        _onTransitionAdded((int)sender.tag, -1, 0.3f);
    (void)sender;
}
- (void)removeTransitionClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onTransitionRemoved)
        _onTransitionRemoved((int)sender.tag, -1);  // resolved by C++ side
    (void)sender;
}
- (void)transitionRowClicked:(NSClickGestureRecognizer*)sender
{
    if (_suppressCallbacks)
        return;
    NSView* view = sender.view;
    // tag encodes stateIndex * 1000 + transIdx
    int combined = (int)view.tag;
    int stateIdx = combined / 1000;
    int transIdx = combined % 1000;
    if (_onTransitionSelected)
        _onTransitionSelected(stateIdx, transIdx);
}
- (void)transTargetChanged:(NSPopUpButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onTransitionEdited)
    {
        int combined = (int)sender.tag;
        int stateIdx = combined / 1000;
        int transIdx = combined % 1000;
        _onTransitionEdited(stateIdx, transIdx, (int)sender.indexOfSelectedItem, -1.0f, -1.0f, NO);
    }
}
- (void)transBlendEdited:(NSTextField*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onTransitionEdited)
    {
        int combined = (int)sender.tag;
        int stateIdx = combined / 1000;
        int transIdx = combined % 1000;
        _onTransitionEdited(stateIdx, transIdx, -1, (float)sender.doubleValue, -1.0f, NO);
    }
}
- (void)transExitTimeCheckChanged:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onTransitionEdited)
    {
        int combined = (int)sender.tag;
        int stateIdx = combined / 1000;
        int transIdx = combined % 1000;
        BOOL checked = (sender.state == NSControlStateValueOn);
        _onTransitionEdited(stateIdx, transIdx, -1, -1.0f, -1.0f, checked);
    }
}
- (void)transExitTimeEdited:(NSTextField*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onTransitionEdited)
    {
        int combined = (int)sender.tag;
        int stateIdx = combined / 1000;
        int transIdx = combined % 1000;
        _onTransitionEdited(stateIdx, transIdx, -1, -1.0f, (float)sender.doubleValue, NO);
    }
}
- (void)addConditionClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onConditionAdded)
    {
        int combined = (int)sender.tag;
        int stateIdx = combined / 1000;
        int transIdx = combined % 1000;
        _onConditionAdded(stateIdx, transIdx, @"param", 0, 0.0f);
    }
}
- (void)removeConditionClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onConditionRemoved)
    {
        int combined = (int)sender.tag;
        int stateIdx = combined / 1000;
        int transIdx = combined % 1000;
        _onConditionRemoved(stateIdx, transIdx, -1);  // remove last
    }
}
- (void)condParamEdited:(NSTextField*)sender
{
    if (_suppressCallbacks)
        return;
    // Not directly re-dispatched; handled via full condition edit flow
    (void)sender;
}
- (void)condCompareChanged:(NSPopUpButton*)sender
{
    if (_suppressCallbacks)
        return;
    // Not directly re-dispatched
    (void)sender;
}
- (void)condThresholdEdited:(NSTextField*)sender
{
    if (_suppressCallbacks)
        return;
    // Not directly re-dispatched
    (void)sender;
}
- (void)addParamClicked:(NSButton*)sender
{
    if (_suppressCallbacks)
        return;
    if (_onParamAdded)
        _onParamAdded(@"param", NO);
    (void)sender;
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

    // State machine editing section.
    NSStackView* smStateListStack = nil;
    NSButton* addStateButton = nil;
    NSButton* removeStateButton = nil;
    int smSelectedStateIndex = -1;
    int smSelectedTransitionIndex = -1;

    // Selected state properties.
    NSStackView* smStatePropsStack = nil;
    NSTextField* smStateNameField = nil;
    NSPopUpButton* smStateClipDropdown = nil;
    NSSlider* smStateSpeedSlider = nil;
    NSTextField* smStateSpeedLabel = nil;
    NSButton* smStateLoopCheckbox = nil;

    // Transition list for selected state.
    NSStackView* smTransitionListStack = nil;
    NSButton* addTransitionButton = nil;
    NSButton* removeTransitionButton = nil;

    // Transition properties.
    NSStackView* smTransPropsStack = nil;
    NSPopUpButton* smTransTargetDropdown = nil;
    NSTextField* smTransBlendField = nil;
    NSButton* smTransExitTimeCheck = nil;
    NSTextField* smTransExitTimeField = nil;

    // Condition list for selected transition.
    NSStackView* smConditionListStack = nil;
    NSButton* addConditionButton = nil;
    NSButton* removeConditionButton = nil;

    // Add parameter button.
    NSButton* addParamButton = nil;

    // State machine graph view.
    StateMachineGraphView* smGraphView = nil;
    GraphViewDelegateAdapter* smGraphDelegate = nil;

    // Cached state data for the editing widgets.
    int lastStateInfoCount = -1;

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
    StateAddedCallback stateAddedCb;
    StateRemovedCallback stateRemovedCb;
    StateEditedCallback stateEditedCb;
    TransitionAddedCallback transitionAddedCb;
    TransitionRemovedCallback transitionRemovedCb;
    TransitionEditedCallback transitionEditedCb;
    ConditionAddedCallback conditionAddedCb;
    ConditionRemovedCallback conditionRemovedCb;
    ParamAddedCallback paramAddedCb;
    StateSelectedCallback stateSelectedCb;
    TransitionSelectedCallback transitionSelectedCb;
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

        // State list header: "States" label + add/remove buttons.
        impl_->addStateButton = [NSButton buttonWithTitle:@"+"
                                                   target:impl_->delegate
                                                   action:@selector(addStateClicked:)];
        impl_->addStateButton.bezelStyle = NSBezelStyleRounded;
        impl_->addStateButton.controlSize = NSControlSizeSmall;
        impl_->addStateButton.font = [NSFont systemFontOfSize:11.0];

        impl_->removeStateButton = [NSButton buttonWithTitle:@"\u2212"
                                                      target:impl_->delegate
                                                      action:@selector(removeStateClicked:)];
        impl_->removeStateButton.bezelStyle = NSBezelStyleRounded;
        impl_->removeStateButton.controlSize = NSControlSizeSmall;
        impl_->removeStateButton.font = [NSFont systemFontOfSize:11.0];

        NSTextField* statesCaption = [NSTextField labelWithString:@"States"];
        statesCaption.font = [NSFont boldSystemFontOfSize:11.0];
        statesCaption.textColor = [NSColor labelColor];

        NSStackView* stateHeaderRow = [NSStackView
            stackViewWithViews:@[ statesCaption, impl_->addStateButton, impl_->removeStateButton ]];
        stateHeaderRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        stateHeaderRow.alignment = NSLayoutAttributeCenterY;
        stateHeaderRow.spacing = 4.0;

        impl_->smStateListStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
        impl_->smStateListStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->smStateListStack.alignment = NSLayoutAttributeLeading;
        impl_->smStateListStack.spacing = 2.0;
        impl_->smStateListStack.translatesAutoresizingMaskIntoConstraints = NO;

        // State properties section (shown when a state is selected).
        impl_->smStateNameField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 100, 20)];
        impl_->smStateNameField.font = [NSFont systemFontOfSize:11.0];
        impl_->smStateNameField.bordered = YES;
        impl_->smStateNameField.bezeled = YES;
        impl_->smStateNameField.editable = YES;
        impl_->smStateNameField.bezelStyle = NSTextFieldRoundedBezel;
        impl_->smStateNameField.controlSize = NSControlSizeSmall;
        impl_->smStateNameField.target = impl_->delegate;
        impl_->smStateNameField.action = @selector(stateNameEdited:);
        impl_->smStateNameField.placeholderString = @"Name";

        impl_->smStateClipDropdown = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 120, 22)
                                                                pullsDown:NO];
        impl_->smStateClipDropdown.controlSize = NSControlSizeSmall;
        impl_->smStateClipDropdown.font = [NSFont systemFontOfSize:11.0];
        impl_->smStateClipDropdown.target = impl_->delegate;
        impl_->smStateClipDropdown.action = @selector(stateClipChanged:);

        impl_->smStateSpeedSlider = [NSSlider sliderWithValue:1.0
                                                     minValue:0.0
                                                     maxValue:3.0
                                                       target:impl_->delegate
                                                       action:@selector(stateSpeedChanged:)];
        impl_->smStateSpeedSlider.controlSize = NSControlSizeSmall;
        impl_->smStateSpeedSlider.continuous = YES;
        [impl_->smStateSpeedSlider
            setContentHuggingPriority:200
                       forOrientation:NSLayoutConstraintOrientationHorizontal];

        impl_->smStateSpeedLabel = [NSTextField labelWithString:@"1.00"];
        impl_->smStateSpeedLabel.font =
            [NSFont monospacedDigitSystemFontOfSize:10.0 weight:NSFontWeightRegular];
        impl_->smStateSpeedLabel.textColor = [NSColor secondaryLabelColor];

        impl_->smStateLoopCheckbox = [NSButton checkboxWithTitle:@"Loop"
                                                          target:impl_->delegate
                                                          action:@selector(stateLoopChanged:)];
        impl_->smStateLoopCheckbox.controlSize = NSControlSizeSmall;
        impl_->smStateLoopCheckbox.font = [NSFont systemFontOfSize:11.0];

        NSTextField* nameCaption = [NSTextField labelWithString:@"Name:"];
        nameCaption.font = [NSFont systemFontOfSize:10.0];
        nameCaption.textColor = [NSColor secondaryLabelColor];
        NSTextField* clipCaption = [NSTextField labelWithString:@"Clip:"];
        clipCaption.font = [NSFont systemFontOfSize:10.0];
        clipCaption.textColor = [NSColor secondaryLabelColor];
        NSTextField* spdCaption = [NSTextField labelWithString:@"Speed:"];
        spdCaption.font = [NSFont systemFontOfSize:10.0];
        spdCaption.textColor = [NSColor secondaryLabelColor];

        NSStackView* statePropsRow1 = [NSStackView stackViewWithViews:@[
            nameCaption, impl_->smStateNameField, clipCaption, impl_->smStateClipDropdown
        ]];
        statePropsRow1.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        statePropsRow1.alignment = NSLayoutAttributeCenterY;
        statePropsRow1.spacing = 4.0;

        NSStackView* statePropsRow2 = [NSStackView stackViewWithViews:@[
            spdCaption, impl_->smStateSpeedSlider, impl_->smStateSpeedLabel,
            impl_->smStateLoopCheckbox
        ]];
        statePropsRow2.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        statePropsRow2.alignment = NSLayoutAttributeCenterY;
        statePropsRow2.spacing = 4.0;

        impl_->smStatePropsStack =
            [NSStackView stackViewWithViews:@[ statePropsRow1, statePropsRow2 ]];
        impl_->smStatePropsStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->smStatePropsStack.alignment = NSLayoutAttributeLeading;
        impl_->smStatePropsStack.spacing = 4.0;
        impl_->smStatePropsStack.hidden = YES;

        // Transition list section.
        NSTextField* transCaption = [NSTextField labelWithString:@"Transitions"];
        transCaption.font = [NSFont boldSystemFontOfSize:11.0];
        transCaption.textColor = [NSColor labelColor];

        impl_->addTransitionButton = [NSButton buttonWithTitle:@"+"
                                                        target:impl_->delegate
                                                        action:@selector(addTransitionClicked:)];
        impl_->addTransitionButton.bezelStyle = NSBezelStyleRounded;
        impl_->addTransitionButton.controlSize = NSControlSizeSmall;
        impl_->addTransitionButton.font = [NSFont systemFontOfSize:11.0];

        impl_->removeTransitionButton =
            [NSButton buttonWithTitle:@"\u2212"
                               target:impl_->delegate
                               action:@selector(removeTransitionClicked:)];
        impl_->removeTransitionButton.bezelStyle = NSBezelStyleRounded;
        impl_->removeTransitionButton.controlSize = NSControlSizeSmall;
        impl_->removeTransitionButton.font = [NSFont systemFontOfSize:11.0];

        NSStackView* transHeaderRow = [NSStackView stackViewWithViews:@[
            transCaption, impl_->addTransitionButton, impl_->removeTransitionButton
        ]];
        transHeaderRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        transHeaderRow.alignment = NSLayoutAttributeCenterY;
        transHeaderRow.spacing = 4.0;

        impl_->smTransitionListStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
        impl_->smTransitionListStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->smTransitionListStack.alignment = NSLayoutAttributeLeading;
        impl_->smTransitionListStack.spacing = 2.0;
        impl_->smTransitionListStack.translatesAutoresizingMaskIntoConstraints = NO;

        // Transition properties section.
        NSTextField* tgtCaption = [NSTextField labelWithString:@"Target:"];
        tgtCaption.font = [NSFont systemFontOfSize:10.0];
        tgtCaption.textColor = [NSColor secondaryLabelColor];

        impl_->smTransTargetDropdown =
            [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 120, 22) pullsDown:NO];
        impl_->smTransTargetDropdown.controlSize = NSControlSizeSmall;
        impl_->smTransTargetDropdown.font = [NSFont systemFontOfSize:11.0];
        impl_->smTransTargetDropdown.target = impl_->delegate;
        impl_->smTransTargetDropdown.action = @selector(transTargetChanged:);

        NSTextField* blendCaption = [NSTextField labelWithString:@"Blend:"];
        blendCaption.font = [NSFont systemFontOfSize:10.0];
        blendCaption.textColor = [NSColor secondaryLabelColor];

        impl_->smTransBlendField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 50, 20)];
        impl_->smTransBlendField.font = [NSFont systemFontOfSize:11.0];
        impl_->smTransBlendField.bordered = YES;
        impl_->smTransBlendField.bezeled = YES;
        impl_->smTransBlendField.editable = YES;
        impl_->smTransBlendField.bezelStyle = NSTextFieldRoundedBezel;
        impl_->smTransBlendField.controlSize = NSControlSizeSmall;
        impl_->smTransBlendField.target = impl_->delegate;
        impl_->smTransBlendField.action = @selector(transBlendEdited:);

        NSTextField* blendUnit = [NSTextField labelWithString:@"s"];
        blendUnit.font = [NSFont systemFontOfSize:10.0];
        blendUnit.textColor = [NSColor secondaryLabelColor];

        impl_->smTransExitTimeCheck =
            [NSButton checkboxWithTitle:@"Exit time:"
                                 target:impl_->delegate
                                 action:@selector(transExitTimeCheckChanged:)];
        impl_->smTransExitTimeCheck.controlSize = NSControlSizeSmall;
        impl_->smTransExitTimeCheck.font = [NSFont systemFontOfSize:11.0];

        impl_->smTransExitTimeField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 50, 20)];
        impl_->smTransExitTimeField.font = [NSFont systemFontOfSize:11.0];
        impl_->smTransExitTimeField.bordered = YES;
        impl_->smTransExitTimeField.bezeled = YES;
        impl_->smTransExitTimeField.editable = YES;
        impl_->smTransExitTimeField.bezelStyle = NSTextFieldRoundedBezel;
        impl_->smTransExitTimeField.controlSize = NSControlSizeSmall;
        impl_->smTransExitTimeField.target = impl_->delegate;
        impl_->smTransExitTimeField.action = @selector(transExitTimeEdited:);
        impl_->smTransExitTimeField.enabled = NO;

        NSStackView* transPropsRow1 = [NSStackView stackViewWithViews:@[
            tgtCaption, impl_->smTransTargetDropdown, blendCaption, impl_->smTransBlendField,
            blendUnit
        ]];
        transPropsRow1.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        transPropsRow1.alignment = NSLayoutAttributeCenterY;
        transPropsRow1.spacing = 4.0;

        NSStackView* transPropsRow2 = [NSStackView
            stackViewWithViews:@[ impl_->smTransExitTimeCheck, impl_->smTransExitTimeField ]];
        transPropsRow2.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        transPropsRow2.alignment = NSLayoutAttributeCenterY;
        transPropsRow2.spacing = 4.0;

        // Condition list section.
        NSTextField* condCaption = [NSTextField labelWithString:@"Conditions"];
        condCaption.font = [NSFont boldSystemFontOfSize:10.0];
        condCaption.textColor = [NSColor labelColor];

        impl_->addConditionButton = [NSButton buttonWithTitle:@"+"
                                                       target:impl_->delegate
                                                       action:@selector(addConditionClicked:)];
        impl_->addConditionButton.bezelStyle = NSBezelStyleRounded;
        impl_->addConditionButton.controlSize = NSControlSizeSmall;
        impl_->addConditionButton.font = [NSFont systemFontOfSize:11.0];

        impl_->removeConditionButton =
            [NSButton buttonWithTitle:@"\u2212"
                               target:impl_->delegate
                               action:@selector(removeConditionClicked:)];
        impl_->removeConditionButton.bezelStyle = NSBezelStyleRounded;
        impl_->removeConditionButton.controlSize = NSControlSizeSmall;
        impl_->removeConditionButton.font = [NSFont systemFontOfSize:11.0];

        NSStackView* condHeaderRow = [NSStackView stackViewWithViews:@[
            condCaption, impl_->addConditionButton, impl_->removeConditionButton
        ]];
        condHeaderRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        condHeaderRow.alignment = NSLayoutAttributeCenterY;
        condHeaderRow.spacing = 4.0;

        impl_->smConditionListStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
        impl_->smConditionListStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->smConditionListStack.alignment = NSLayoutAttributeLeading;
        impl_->smConditionListStack.spacing = 2.0;
        impl_->smConditionListStack.translatesAutoresizingMaskIntoConstraints = NO;

        impl_->smTransPropsStack = [NSStackView stackViewWithViews:@[
            transPropsRow1, transPropsRow2, condHeaderRow, impl_->smConditionListStack
        ]];
        impl_->smTransPropsStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        impl_->smTransPropsStack.alignment = NSLayoutAttributeLeading;
        impl_->smTransPropsStack.spacing = 4.0;
        impl_->smTransPropsStack.hidden = YES;

        // Add parameter button.
        impl_->addParamButton = [NSButton buttonWithTitle:@"+ Parameter"
                                                   target:impl_->delegate
                                                   action:@selector(addParamClicked:)];
        impl_->addParamButton.bezelStyle = NSBezelStyleRounded;
        impl_->addParamButton.controlSize = NSControlSizeSmall;
        impl_->addParamButton.font = [NSFont systemFontOfSize:11.0];

        // State machine graph view (node graph visualization).
        impl_->smGraphView =
            [[StateMachineGraphView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
        impl_->smGraphView.translatesAutoresizingMaskIntoConstraints = NO;
        [impl_->smGraphView
            addConstraint:[NSLayoutConstraint constraintWithItem:impl_->smGraphView
                                                       attribute:NSLayoutAttributeHeight
                                                       relatedBy:NSLayoutRelationEqual
                                                          toItem:nil
                                                       attribute:NSLayoutAttributeNotAnAttribute
                                                      multiplier:1.0
                                                        constant:200.0]];

        // Wire graph view delegate to forward events to C++ callbacks.
        {
            auto* implPtr = impl_.get();
            impl_->smGraphDelegate = [[GraphViewDelegateAdapter alloc] init];
            impl_->smGraphDelegate->_stateSelectedBlock = ^(int stateIndex) {
              if (implPtr->stateSelectedCb)
                  implPtr->stateSelectedCb(stateIndex);
            };
            impl_->smGraphDelegate->_transitionSelectedBlock = ^(int stateIndex, int transIndex) {
              if (implPtr->transitionSelectedCb)
                  implPtr->transitionSelectedCb(stateIndex, transIndex);
            };
            impl_->smGraphDelegate->_stateForceSetBlock = ^(int stateIndex) {
              if (implPtr->stateForceSetCb)
                  implPtr->stateForceSetCb(stateIndex);
            };
            impl_->smGraphDelegate->_stateAddedBlock = ^{
              if (implPtr->stateAddedCb)
                  implPtr->stateAddedCb();
            };
            impl_->smGraphDelegate->_stateRemovedBlock = ^(int stateIndex) {
              if (implPtr->stateRemovedCb)
                  implPtr->stateRemovedCb(stateIndex);
            };
            [impl_->smGraphView setGraphDelegate:impl_->smGraphDelegate];
        }

        impl_->smSectionStack = [NSStackView stackViewWithViews:@[
            smCaption, impl_->smGraphView, smStateLabelRow, impl_->smStateDropdown,
            stateHeaderRow, impl_->smStateListStack, impl_->smStatePropsStack, transHeaderRow,
            impl_->smTransitionListStack, impl_->smTransPropsStack, impl_->addParamButton,
            impl_->smParamStack
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
                constraintEqualToAnchor:impl_->containerView.trailingAnchor
                               constant:-8],
            // Align the event marker view with the scrubber track.
            [impl_->eventMarkerView.leadingAnchor
                constraintEqualToAnchor:impl_->scrubber.leadingAnchor],
            [impl_->eventMarkerView.trailingAnchor
                constraintEqualToAnchor:impl_->scrubber.trailingAnchor],
            // Stretch child stacks to fill width.
            [impl_->eventListStack.leadingAnchor
                constraintEqualToAnchor:impl_->rootStack.leadingAnchor],
            [impl_->eventListStack.trailingAnchor
                constraintEqualToAnchor:impl_->rootStack.trailingAnchor],
            [impl_->smSectionStack.leadingAnchor
                constraintEqualToAnchor:impl_->rootStack.leadingAnchor],
            [impl_->smSectionStack.trailingAnchor
                constraintEqualToAnchor:impl_->rootStack.trailingAnchor],
            [impl_->smParamStack.leadingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.leadingAnchor],
            [impl_->smParamStack.trailingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.trailingAnchor],
            [impl_->smStateListStack.leadingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.leadingAnchor],
            [impl_->smStateListStack.trailingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.trailingAnchor],
            [impl_->smTransitionListStack.leadingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.leadingAnchor],
            [impl_->smTransitionListStack.trailingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.trailingAnchor],
            [impl_->smConditionListStack.leadingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.leadingAnchor],
            [impl_->smConditionListStack.trailingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.trailingAnchor],
            [impl_->smGraphView.leadingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.leadingAnchor],
            [impl_->smGraphView.trailingAnchor
                constraintEqualToAnchor:impl_->smSectionStack.trailingAnchor],
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
        impl_->smStateListStack = nil;
        impl_->addStateButton = nil;
        impl_->removeStateButton = nil;
        impl_->smStatePropsStack = nil;
        impl_->smStateNameField = nil;
        impl_->smStateClipDropdown = nil;
        impl_->smStateSpeedSlider = nil;
        impl_->smStateSpeedLabel = nil;
        impl_->smStateLoopCheckbox = nil;
        impl_->smTransitionListStack = nil;
        impl_->addTransitionButton = nil;
        impl_->removeTransitionButton = nil;
        impl_->smTransPropsStack = nil;
        impl_->smTransTargetDropdown = nil;
        impl_->smTransBlendField = nil;
        impl_->smTransExitTimeCheck = nil;
        impl_->smTransExitTimeField = nil;
        impl_->smConditionListStack = nil;
        impl_->addConditionButton = nil;
        impl_->removeConditionButton = nil;
        impl_->addParamButton = nil;
        impl_->smGraphView = nil;
        impl_->smGraphDelegate = nil;
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

            // Update graph view with current state data.
            {
                NSMutableArray<NSString*>* gNames = [NSMutableArray array];
                NSMutableArray<NSString*>* gClips = [NSMutableArray array];
                for (const auto& si : s.stateInfos)
                {
                    [gNames addObject:[NSString stringWithUTF8String:si.name.c_str()]];
                    [gClips addObject:[NSString stringWithUTF8String:si.clipName.c_str()]];
                }
                [impl_->smGraphView updateWithStateCount:(int)s.stateInfos.size()
                                            names:gNames
                                        clipNames:gClips
                                     currentState:s.currentStateIndex
                                    selectedState:impl_->smSelectedStateIndex
                               selectedTransition:impl_->smSelectedTransitionIndex];

                // Build flat transition arrays for the graph view.
                NSMutableArray<NSNumber*>* tFrom = [NSMutableArray array];
                NSMutableArray<NSNumber*>* tTo = [NSMutableArray array];
                NSMutableArray<NSString*>* tLabels = [NSMutableArray array];
                NSMutableArray<NSNumber*>* tCombined = [NSMutableArray array];

                for (size_t si = 0; si < s.stateInfos.size(); ++si)
                {
                    const auto& state = s.stateInfos[si];
                    for (size_t ti = 0; ti < state.transitions.size(); ++ti)
                    {
                        const auto& tr = state.transitions[ti];
                        [tFrom addObject:@((int)si)];
                        [tTo addObject:@(tr.targetState)];

                        // Build condition summary label.
                        NSMutableString* condStr = [NSMutableString string];
                        static const char* compareNames[] = {">", "<", "==", "!=", "true", "false"};
                        for (size_t ci = 0; ci < tr.conditions.size(); ++ci)
                        {
                            const auto& cond = tr.conditions[ci];
                            int cmpIdx = cond.compare;
                            if (cmpIdx < 0 || cmpIdx > 5)
                                cmpIdx = 0;
                            if (ci > 0)
                                [condStr appendString:@", "];
                            if (cmpIdx >= 4)
                            {
                                [condStr appendFormat:@"%s %s", cond.paramName.c_str(),
                                                      compareNames[cmpIdx]];
                            }
                            else
                            {
                                [condStr appendFormat:@"%s %s %.1f", cond.paramName.c_str(),
                                                      compareNames[cmpIdx], cond.threshold];
                            }
                        }
                        [tLabels addObject:condStr];
                        [tCombined addObject:@((int)si * 1000 + (int)ti)];
                    }
                }
                [impl_->smGraphView setTransitions:tFrom
                                              to:tTo
                                          labels:tLabels
                                 stateTransIdxes:tCombined];
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

            // State list rows.
            {
                int selState = s.selectedStateIndex;
                impl_->smSelectedStateIndex = selState;

                // Always rebuild the state list (state counts are small).
                NSArray<NSView*>* oldStateRows = [impl_->smStateListStack.arrangedSubviews copy];
                for (NSView* v in oldStateRows)
                {
                    [impl_->smStateListStack removeArrangedSubview:v];
                    [v removeFromSuperview];
                }

                for (size_t i = 0; i < s.stateInfos.size(); ++i)
                {
                    const auto& si = s.stateInfos[i];
                    NSString* bullet = ((int)i == selState) ? @"\u25CF" : @"\u25CB";
                    NSString* loopStr = si.loop ? @"loop" : @"once";
                    NSString* rowText = [NSString
                        stringWithFormat:@"%@ %s  (%s, %.1fx, %@)", bullet, si.name.c_str(),
                                         si.clipName.c_str(), si.speed, loopStr];

                    NSTextField* rowLabel = [NSTextField labelWithString:rowText];
                    rowLabel.font = [NSFont systemFontOfSize:11.0];
                    rowLabel.textColor = ((int)i == s.currentStateIndex)
                                             ? [NSColor systemGreenColor]
                                             : [NSColor labelColor];

                    TaggableView* rowContainer = [[TaggableView alloc] initWithFrame:NSZeroRect];
                    rowContainer.translatesAutoresizingMaskIntoConstraints = NO;
                    rowContainer.viewTag = (int)i;
                    [rowContainer addSubview:rowLabel];
                    rowLabel.translatesAutoresizingMaskIntoConstraints = NO;
                    [NSLayoutConstraint activateConstraints:@[
                        [rowLabel.leadingAnchor constraintEqualToAnchor:rowContainer.leadingAnchor
                                                               constant:4],
                        [rowLabel.trailingAnchor constraintEqualToAnchor:rowContainer.trailingAnchor
                                                                constant:-4],
                        [rowLabel.topAnchor constraintEqualToAnchor:rowContainer.topAnchor
                                                           constant:1],
                        [rowLabel.bottomAnchor constraintEqualToAnchor:rowContainer.bottomAnchor
                                                              constant:-1],
                    ]];

                    if ((int)i == selState)
                    {
                        rowContainer.wantsLayer = YES;
                        rowContainer.layer.backgroundColor =
                            [NSColor selectedContentBackgroundColor].CGColor;
                        rowContainer.layer.cornerRadius = 3.0;
                    }

                    NSClickGestureRecognizer* click = [[NSClickGestureRecognizer alloc]
                        initWithTarget:impl_->delegate
                                action:@selector(stateRowClicked:)];
                    [rowContainer addGestureRecognizer:click];

                    [impl_->smStateListStack addArrangedSubview:rowContainer];
                }

                // Update the remove-state button tag.
                impl_->removeStateButton.tag = selState;
            }

            // State properties (when a state is selected).
            {
                int selState = s.selectedStateIndex;
                impl_->smStatePropsStack.hidden = (selState < 0);
                if (selState >= 0 && (size_t)selState < s.stateInfos.size())
                {
                    const auto& si = s.stateInfos[selState];
                    impl_->smStateNameField.stringValue =
                        [NSString stringWithUTF8String:si.name.c_str()];
                    impl_->smStateNameField.tag = selState;

                    // Populate clip dropdown.
                    [impl_->smStateClipDropdown removeAllItems];
                    for (const auto& cn : s.clipNames)
                    {
                        [impl_->smStateClipDropdown
                            addItemWithTitle:[NSString stringWithUTF8String:cn.c_str()]];
                    }
                    // Select matching clip.
                    NSString* clipName = [NSString stringWithUTF8String:si.clipName.c_str()];
                    NSInteger clipIdx = [impl_->smStateClipDropdown indexOfItemWithTitle:clipName];
                    if (clipIdx >= 0)
                        [impl_->smStateClipDropdown selectItemAtIndex:clipIdx];
                    impl_->smStateClipDropdown.tag = selState;

                    impl_->smStateSpeedSlider.doubleValue = si.speed;
                    impl_->smStateSpeedSlider.tag = selState;
                    impl_->smStateSpeedLabel.stringValue =
                        [NSString stringWithFormat:@"%.2f", si.speed];

                    impl_->smStateLoopCheckbox.state =
                        si.loop ? NSControlStateValueOn : NSControlStateValueOff;
                    impl_->smStateLoopCheckbox.tag = selState;
                }
            }

            // Transition list for selected state.
            {
                int selState = s.selectedStateIndex;
                int selTrans = s.selectedTransitionIndex;
                impl_->smSelectedTransitionIndex = selTrans;

                // Set tags for add/remove buttons.
                impl_->addTransitionButton.tag = selState;
                impl_->removeTransitionButton.tag = selState;

                NSArray<NSView*>* oldTransRows =
                    [impl_->smTransitionListStack.arrangedSubviews copy];
                for (NSView* v in oldTransRows)
                {
                    [impl_->smTransitionListStack removeArrangedSubview:v];
                    [v removeFromSuperview];
                }

                if (selState >= 0 && (size_t)selState < s.stateInfos.size())
                {
                    const auto& si = s.stateInfos[selState];
                    for (size_t ti = 0; ti < si.transitions.size(); ++ti)
                    {
                        const auto& tr = si.transitions[ti];
                        NSMutableString* condStr = [NSMutableString string];
                        for (size_t ci = 0; ci < tr.conditions.size(); ++ci)
                        {
                            const auto& cond = tr.conditions[ci];
                            static const char* compareNames[] = {
                                ">", "<", "==", "!=", "true", "false"};
                            int cmpIdx = cond.compare;
                            if (cmpIdx < 0 || cmpIdx > 5)
                                cmpIdx = 0;
                            if (ci > 0)
                                [condStr appendString:@", "];
                            if (cmpIdx >= 4)
                            {
                                [condStr appendFormat:@"%s %s", cond.paramName.c_str(),
                                                      compareNames[cmpIdx]];
                            }
                            else
                            {
                                [condStr appendFormat:@"%s %s %.1f", cond.paramName.c_str(),
                                                      compareNames[cmpIdx], cond.threshold];
                            }
                        }

                        NSString* rowText;
                        if (condStr.length > 0)
                        {
                            rowText = [NSString
                                stringWithFormat:@"\u2192 %s (blend: %.1fs) when %@",
                                                 tr.targetName.c_str(), tr.blendDuration, condStr];
                        }
                        else
                        {
                            rowText =
                                [NSString stringWithFormat:@"\u2192 %s (blend: %.1fs)",
                                                           tr.targetName.c_str(), tr.blendDuration];
                        }

                        NSTextField* label = [NSTextField labelWithString:rowText];
                        label.font = [NSFont systemFontOfSize:11.0];
                        label.textColor = [NSColor labelColor];

                        TaggableView* rowContainer =
                            [[TaggableView alloc] initWithFrame:NSZeroRect];
                        rowContainer.translatesAutoresizingMaskIntoConstraints = NO;
                        rowContainer.viewTag = selState * 1000 + (int)ti;
                        [rowContainer addSubview:label];
                        label.translatesAutoresizingMaskIntoConstraints = NO;
                        [NSLayoutConstraint activateConstraints:@[
                            [label.leadingAnchor constraintEqualToAnchor:rowContainer.leadingAnchor
                                                                constant:8],
                            [label.trailingAnchor
                                constraintEqualToAnchor:rowContainer.trailingAnchor
                                               constant:-4],
                            [label.topAnchor constraintEqualToAnchor:rowContainer.topAnchor
                                                            constant:1],
                            [label.bottomAnchor constraintEqualToAnchor:rowContainer.bottomAnchor
                                                               constant:-1],
                        ]];

                        if ((int)ti == selTrans)
                        {
                            rowContainer.wantsLayer = YES;
                            rowContainer.layer.backgroundColor =
                                [NSColor selectedContentBackgroundColor].CGColor;
                            rowContainer.layer.cornerRadius = 3.0;
                        }

                        NSClickGestureRecognizer* click = [[NSClickGestureRecognizer alloc]
                            initWithTarget:impl_->delegate
                                    action:@selector(transitionRowClicked:)];
                        [rowContainer addGestureRecognizer:click];

                        [impl_->smTransitionListStack addArrangedSubview:rowContainer];
                    }
                }
            }

            // Transition properties (when a transition is selected).
            {
                int selState = s.selectedStateIndex;
                int selTrans = s.selectedTransitionIndex;
                bool showTransProps =
                    (selState >= 0 && (size_t)selState < s.stateInfos.size() && selTrans >= 0 &&
                     (size_t)selTrans < s.stateInfos[selState].transitions.size());
                impl_->smTransPropsStack.hidden = !showTransProps;

                if (showTransProps)
                {
                    const auto& tr = s.stateInfos[selState].transitions[selTrans];
                    int combined = selState * 1000 + selTrans;

                    // Target dropdown.
                    [impl_->smTransTargetDropdown removeAllItems];
                    for (const auto& sn : s.stateNames)
                    {
                        [impl_->smTransTargetDropdown
                            addItemWithTitle:[NSString stringWithUTF8String:sn.c_str()]];
                    }
                    if (tr.targetState >= 0 &&
                        tr.targetState < (int)impl_->smTransTargetDropdown.numberOfItems)
                    {
                        [impl_->smTransTargetDropdown selectItemAtIndex:tr.targetState];
                    }
                    impl_->smTransTargetDropdown.tag = combined;

                    impl_->smTransBlendField.stringValue =
                        [NSString stringWithFormat:@"%.2f", tr.blendDuration];
                    impl_->smTransBlendField.tag = combined;

                    impl_->smTransExitTimeCheck.state =
                        tr.hasExitTime ? NSControlStateValueOn : NSControlStateValueOff;
                    impl_->smTransExitTimeCheck.tag = combined;

                    impl_->smTransExitTimeField.stringValue =
                        [NSString stringWithFormat:@"%.2f", tr.exitTime];
                    impl_->smTransExitTimeField.tag = combined;
                    impl_->smTransExitTimeField.enabled = tr.hasExitTime;

                    // Condition tags.
                    impl_->addConditionButton.tag = combined;
                    impl_->removeConditionButton.tag = combined;

                    // Condition list rows.
                    NSArray<NSView*>* oldCondRows =
                        [impl_->smConditionListStack.arrangedSubviews copy];
                    for (NSView* v in oldCondRows)
                    {
                        [impl_->smConditionListStack removeArrangedSubview:v];
                        [v removeFromSuperview];
                    }

                    for (size_t ci = 0; ci < tr.conditions.size(); ++ci)
                    {
                        const auto& cond = tr.conditions[ci];

                        NSTextField* paramField =
                            [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 80, 20)];
                        paramField.stringValue =
                            [NSString stringWithUTF8String:cond.paramName.c_str()];
                        paramField.font = [NSFont systemFontOfSize:11.0];
                        paramField.bordered = YES;
                        paramField.bezeled = YES;
                        paramField.editable = YES;
                        paramField.bezelStyle = NSTextFieldRoundedBezel;
                        paramField.controlSize = NSControlSizeSmall;

                        NSPopUpButton* compareBtn =
                            [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 80, 22)
                                                       pullsDown:NO];
                        compareBtn.controlSize = NSControlSizeSmall;
                        compareBtn.font = [NSFont systemFontOfSize:11.0];
                        [compareBtn addItemsWithTitles:@[
                            @">", @"<", @"==", @"!=", @"BoolTrue", @"BoolFalse"
                        ]];
                        if (cond.compare >= 0 && cond.compare < 6)
                            [compareBtn selectItemAtIndex:cond.compare];

                        NSTextField* threshField =
                            [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 50, 20)];
                        threshField.stringValue =
                            [NSString stringWithFormat:@"%.2f", cond.threshold];
                        threshField.font = [NSFont systemFontOfSize:11.0];
                        threshField.bordered = YES;
                        threshField.bezeled = YES;
                        threshField.editable = YES;
                        threshField.bezelStyle = NSTextFieldRoundedBezel;
                        threshField.controlSize = NSControlSizeSmall;

                        // Wire up editing: when any of these three change, fire
                        // the condition edited callback with all three values.
                        // We use a block-based approach capturing all three.
                        auto* implPtr = impl_.get();
                        int condIdx = (int)ci;
                        auto fireCondEdit = ^{
                          if (implPtr->conditionAddedCb)
                          {
                              // We actually use the onConditionAdded with
                              // remove + re-add pattern, but simpler to just
                              // wire individually. For now the user edits
                              // condition fields then hits enter; we re-add.
                          }
                        };
                        (void)fireCondEdit;

                        // Wire all three to fire a combined condition-edited
                        // callback when any field commits. We use the
                        // conditionRemovedCb + conditionAddedCb pattern:
                        // remove old condition, add new one.
                        __block NSTextField* pf = paramField;
                        __block NSPopUpButton* cb = compareBtn;
                        __block NSTextField* tf = threshField;
                        int sIdx = selState;
                        int tIdx = selTrans;

                        // Use a single action for all three that re-adds.
                        auto makeCondAction = ^{
                          if (implPtr->conditionRemovedCb)
                              implPtr->conditionRemovedCb(sIdx, tIdx, condIdx);
                          if (implPtr->conditionAddedCb)
                          {
                              std::string pName([pf.stringValue UTF8String]);
                              int comp = (int)cb.indexOfSelectedItem;
                              float thresh = (float)tf.doubleValue;
                              implPtr->conditionAddedCb(sIdx, tIdx, pName, comp, thresh);
                          }
                        };

                        paramField.target = implPtr->delegate;
                        paramField.action = @selector(condParamEdited:);
                        paramField.tag = sIdx * 100000 + tIdx * 100 + condIdx;

                        compareBtn.target = implPtr->delegate;
                        compareBtn.action = @selector(condCompareChanged:);
                        compareBtn.tag = sIdx * 100000 + tIdx * 100 + condIdx;

                        threshField.target = implPtr->delegate;
                        threshField.action = @selector(condThresholdEdited:);
                        threshField.tag = sIdx * 100000 + tIdx * 100 + condIdx;

                        // Store the combined action block on the row for use by
                        // delegate methods (not used currently; editing is done
                        // via remove+add in the callbacks).
                        (void)makeCondAction;

                        NSStackView* condRow = [NSStackView
                            stackViewWithViews:@[ paramField, compareBtn, threshField ]];
                        condRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
                        condRow.alignment = NSLayoutAttributeCenterY;
                        condRow.spacing = 4.0;
                        [impl_->smConditionListStack addArrangedSubview:condRow];
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

void CocoaAnimationView::setStateAddedCallback(StateAddedCallback cb)
{
    impl_->stateAddedCb = std::move(cb);
    auto* captured = &impl_->stateAddedCb;
    impl_->delegate.onStateAdded = ^{
      if (*captured)
          (*captured)();
    };
}

void CocoaAnimationView::setStateRemovedCallback(StateRemovedCallback cb)
{
    impl_->stateRemovedCb = std::move(cb);
    auto* implPtr = impl_.get();
    impl_->delegate.onStateRemoved = ^(int index) {
      int resolved = (index < 0) ? implPtr->smSelectedStateIndex : index;
      if (resolved >= 0 && implPtr->stateRemovedCb)
          implPtr->stateRemovedCb(resolved);
    };
}

void CocoaAnimationView::setStateEditedCallback(StateEditedCallback cb)
{
    impl_->stateEditedCb = std::move(cb);
    auto* implPtr = impl_.get();
    impl_->delegate.onStateEdited =
        ^(int stateIndex, NSString* name, int clipIndex, float speed, BOOL loop) {
          if (!implPtr->stateEditedCb)
              return;
          // The delegate fires partial edits (name-only, clip-only, etc.)
          // with sentinel values for unchanged fields.
          std::string nameStr = name ? std::string([name UTF8String]) : "";
          implPtr->stateEditedCb(stateIndex, nameStr, clipIndex, speed, loop);
        };
}

void CocoaAnimationView::setTransitionAddedCallback(TransitionAddedCallback cb)
{
    impl_->transitionAddedCb = std::move(cb);
    auto* implPtr = impl_.get();
    impl_->delegate.onTransitionAdded = ^(int fromState, int toState, float blend) {
      if (fromState < 0)
          fromState = implPtr->smSelectedStateIndex;
      if (implPtr->transitionAddedCb)
          implPtr->transitionAddedCb(fromState, toState, blend);
    };
}

void CocoaAnimationView::setTransitionRemovedCallback(TransitionRemovedCallback cb)
{
    impl_->transitionRemovedCb = std::move(cb);
    auto* implPtr = impl_.get();
    impl_->delegate.onTransitionRemoved = ^(int fromState, int transIdx) {
      int resolved = (transIdx < 0) ? implPtr->smSelectedTransitionIndex : transIdx;
      if (resolved >= 0 && implPtr->transitionRemovedCb)
          implPtr->transitionRemovedCb(fromState, resolved);
    };
}

void CocoaAnimationView::setTransitionEditedCallback(TransitionEditedCallback cb)
{
    impl_->transitionEditedCb = std::move(cb);
    auto* captured = &impl_->transitionEditedCb;
    impl_->delegate.onTransitionEdited = ^(int fromState, int transIdx, int targetState,
                                           float blend, float exitTime, BOOL hasExitTime) {
      if (*captured)
          (*captured)(fromState, transIdx, targetState, blend, exitTime, hasExitTime);
    };
}

void CocoaAnimationView::setConditionAddedCallback(ConditionAddedCallback cb)
{
    impl_->conditionAddedCb = std::move(cb);
    auto* captured = &impl_->conditionAddedCb;
    impl_->delegate.onConditionAdded =
        ^(int fromState, int transIdx, NSString* param, int compare, float threshold) {
          if (*captured)
              (*captured)(fromState, transIdx, std::string([param UTF8String]), compare, threshold);
        };
}

void CocoaAnimationView::setConditionRemovedCallback(ConditionRemovedCallback cb)
{
    impl_->conditionRemovedCb = std::move(cb);
    auto* captured = &impl_->conditionRemovedCb;
    impl_->delegate.onConditionRemoved = ^(int fromState, int transIdx, int condIdx) {
      if (*captured)
          (*captured)(fromState, transIdx, condIdx);
    };
}

void CocoaAnimationView::setParamAddedCallback(ParamAddedCallback cb)
{
    impl_->paramAddedCb = std::move(cb);
    auto* captured = &impl_->paramAddedCb;
    impl_->delegate.onParamAdded = ^(NSString* name, BOOL isBool) {
      if (*captured)
          (*captured)(std::string([name UTF8String]), isBool);
    };
}

void CocoaAnimationView::setStateSelectedCallback(StateSelectedCallback cb)
{
    impl_->stateSelectedCb = std::move(cb);
    auto* captured = &impl_->stateSelectedCb;
    impl_->delegate.onStateSelected = ^(int stateIndex) {
      if (*captured)
          (*captured)(stateIndex);
    };
}

void CocoaAnimationView::setTransitionSelectedCallback(TransitionSelectedCallback cb)
{
    impl_->transitionSelectedCb = std::move(cb);
    auto* captured = &impl_->transitionSelectedCb;
    impl_->delegate.onTransitionSelected = ^(int stateIndex, int transIdx) {
      if (*captured)
          (*captured)(stateIndex, transIdx);
    };
}

}  // namespace engine::editor
