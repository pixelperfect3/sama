#include "editor/platform/cocoa/CocoaAnimationView.h"

#import <Cocoa/Cocoa.h>

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
@property(nonatomic, assign) BOOL suppressCallbacks;
- (void)clipChanged:(NSPopUpButton*)sender;
- (void)playClicked:(NSButton*)sender;
- (void)pauseClicked:(NSButton*)sender;
- (void)stopClicked:(NSButton*)sender;
- (void)scrubberChanged:(NSSlider*)sender;
- (void)speedChanged:(NSSlider*)sender;
- (void)loopChanged:(NSButton*)sender;
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

    AnimationViewDelegate* delegate = nil;

    ClipSelectedCallback clipSelectedCb;
    PlayCallback playCb;
    PauseCallback pauseCb;
    StopCallback stopCb;
    TimeChangedCallback timeChangedCb;
    SpeedChangedCallback speedChangedCb;
    LoopChangedCallback loopChangedCb;
};

CocoaAnimationView::CocoaAnimationView() : impl_(std::make_unique<Impl>())
{
    @autoreleasepool
    {
        impl_->containerView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 150)];
        impl_->containerView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

        impl_->delegate = [[AnimationViewDelegate alloc] init];

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

        // Root vertical stack.
        impl_->rootStack =
            [NSStackView stackViewWithViews:@[ topRow, transportRow, scrubberRow, speedRow ]];
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

}  // namespace engine::editor
