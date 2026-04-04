#include "editor/platform/cocoa/CocoaResourceView.h"

#import <Cocoa/Cocoa.h>

#include "editor/panels/ResourcePanel.h"

namespace engine::editor
{

struct CocoaResourceView::Impl
{
    NSView* containerView = nil;
    NSTextField* fpsLabel = nil;
    NSTextField* frameTimeLabel = nil;
    NSTextField* drawCallsLabel = nil;
    NSTextField* trianglesLabel = nil;
    NSTextField* texMemLabel = nil;
    NSTextField* entityCountLabel = nil;
    NSTextField* arenaLabel = nil;
};

CocoaResourceView::CocoaResourceView() : impl_(std::make_unique<Impl>())
{
    @autoreleasepool
    {
        impl_->containerView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 200)];
        impl_->containerView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

        auto makeLabel = [](NSString* text) -> NSTextField*
        {
            NSTextField* label = [NSTextField labelWithString:text];
            label.font = [NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular];
            label.textColor = [NSColor labelColor];
            label.translatesAutoresizingMaskIntoConstraints = NO;
            return label;
        };

        impl_->fpsLabel = makeLabel(@"FPS: --");
        impl_->frameTimeLabel = makeLabel(@"Frame: -- ms");
        impl_->drawCallsLabel = makeLabel(@"Draw calls: --");
        impl_->trianglesLabel = makeLabel(@"Triangles: --");
        impl_->texMemLabel = makeLabel(@"Tex mem: --");
        impl_->entityCountLabel = makeLabel(@"Entities: --");
        impl_->arenaLabel = makeLabel(@"Arena: --");

        // Stack labels vertically.
        NSStackView* stack = [NSStackView stackViewWithViews:@[
            impl_->fpsLabel,
            impl_->frameTimeLabel,
            impl_->drawCallsLabel,
            impl_->trianglesLabel,
            impl_->texMemLabel,
            impl_->entityCountLabel,
            impl_->arenaLabel,
        ]];
        stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        stack.alignment = NSLayoutAttributeLeading;
        stack.spacing = 4.0;
        stack.translatesAutoresizingMaskIntoConstraints = NO;

        [impl_->containerView addSubview:stack];

        [NSLayoutConstraint activateConstraints:@[
            [stack.topAnchor constraintEqualToAnchor:impl_->containerView.topAnchor constant:8],
            [stack.leadingAnchor constraintEqualToAnchor:impl_->containerView.leadingAnchor
                                                constant:8],
            [stack.trailingAnchor
                constraintLessThanOrEqualToAnchor:impl_->containerView.trailingAnchor
                                         constant:-8],
        ]];
    }
}

CocoaResourceView::~CocoaResourceView() = default;

void* CocoaResourceView::nativeView() const
{
    return (__bridge void*)impl_->containerView;
}

void CocoaResourceView::updateStats(const ResourceStats& stats)
{
    @autoreleasepool
    {
        impl_->fpsLabel.stringValue = [NSString stringWithFormat:@"FPS: %.0f", stats.fps];
        impl_->frameTimeLabel.stringValue =
            [NSString stringWithFormat:@"Frame: %.2f ms", stats.frameTimeMs];
        impl_->drawCallsLabel.stringValue =
            [NSString stringWithFormat:@"Draw calls: %u", stats.drawCalls];
        impl_->trianglesLabel.stringValue =
            [NSString stringWithFormat:@"Triangles: %u", stats.numTriangles];

        // Format texture memory nicely.
        double texMB = static_cast<double>(stats.textureMemUsed) / (1024.0 * 1024.0);
        impl_->texMemLabel.stringValue = [NSString stringWithFormat:@"Tex mem: %.1f MB", texMB];

        impl_->entityCountLabel.stringValue =
            [NSString stringWithFormat:@"Entities: %u", stats.entityCount];

        double arenaKB = static_cast<double>(stats.arenaUsed) / 1024.0;
        double arenaTotalKB = static_cast<double>(stats.arenaCapacity) / 1024.0;
        impl_->arenaLabel.stringValue =
            [NSString stringWithFormat:@"Arena: %.1f / %.1f KB", arenaKB, arenaTotalKB];
    }
}

}  // namespace engine::editor
