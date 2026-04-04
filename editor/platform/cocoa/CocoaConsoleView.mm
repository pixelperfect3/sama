#include "editor/platform/cocoa/CocoaConsoleView.h"

#import <Cocoa/Cocoa.h>

namespace engine::editor
{

struct CocoaConsoleView::Impl
{
    NSScrollView* scrollView = nil;
    NSTextView* textView = nil;
    uint32_t messageCount = 0;
};

CocoaConsoleView::CocoaConsoleView() : impl_(std::make_unique<Impl>())
{
    @autoreleasepool
    {
        impl_->scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 600, 150)];
        impl_->scrollView.hasVerticalScroller = YES;
        impl_->scrollView.autohidesScrollers = YES;

        impl_->textView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 600, 150)];
        impl_->textView.editable = NO;
        impl_->textView.selectable = YES;
        impl_->textView.backgroundColor = [NSColor textBackgroundColor];
        impl_->textView.font = [NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular];
        impl_->textView.textContainerInset = NSMakeSize(4, 4);
        impl_->textView.autoresizingMask = NSViewWidthSizable;
        impl_->textView.textContainer.widthTracksTextView = YES;

        impl_->scrollView.documentView = impl_->textView;
    }
}

CocoaConsoleView::~CocoaConsoleView()
{
    @autoreleasepool
    {
        impl_->scrollView = nil;
        impl_->textView = nil;
    }
}

void* CocoaConsoleView::nativeView() const
{
    return (__bridge void*)impl_->scrollView;
}

void CocoaConsoleView::appendMessage(MessageLevel level, const char* message)
{
    @autoreleasepool
    {
        NSColor* color;
        NSString* prefix;
        switch (level)
        {
            case MessageLevel::Info:
                color = [NSColor colorWithRed:0.3 green:0.75 blue:0.3 alpha:1.0];
                prefix = @"[I] ";
                break;
            case MessageLevel::Warning:
                color = [NSColor colorWithRed:0.9 green:0.75 blue:0.1 alpha:1.0];
                prefix = @"[W] ";
                break;
            case MessageLevel::Error:
                color = [NSColor colorWithRed:0.9 green:0.2 blue:0.2 alpha:1.0];
                prefix = @"[E] ";
                break;
        }

        NSString* text = [NSString stringWithFormat:@"%@%s\n", prefix, message];
        NSDictionary* attrs = @{
            NSForegroundColorAttributeName : color,
            NSFontAttributeName : [NSFont monospacedSystemFontOfSize:11.0
                                                              weight:NSFontWeightRegular],
        };
        NSAttributedString* attrStr = [[NSAttributedString alloc] initWithString:text
                                                                      attributes:attrs];

        [impl_->textView.textStorage appendAttributedString:attrStr];

        // Scroll to bottom.
        [impl_->textView scrollRangeToVisible:NSMakeRange(impl_->textView.string.length, 0)];
        impl_->messageCount++;
    }
}

void CocoaConsoleView::clear()
{
    @autoreleasepool
    {
        [impl_->textView.textStorage
            setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
        impl_->messageCount = 0;
    }
}

uint32_t CocoaConsoleView::messageCount() const
{
    return impl_->messageCount;
}

}  // namespace engine::editor
