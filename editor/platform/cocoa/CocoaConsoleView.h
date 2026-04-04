#pragma once

#include <cstdint>
#include <memory>

namespace engine::editor
{

// ---------------------------------------------------------------------------
// CocoaConsoleView -- native NSTextView-based console panel.
//
// Pimpl pattern: no AppKit headers leak into this header.
// Color-coded messages: info=green, warning=yellow, error=red.
// ---------------------------------------------------------------------------

class CocoaConsoleView
{
public:
    enum class MessageLevel : uint8_t
    {
        Info,
        Warning,
        Error,
    };

    CocoaConsoleView();
    ~CocoaConsoleView();

    CocoaConsoleView(const CocoaConsoleView&) = delete;
    CocoaConsoleView& operator=(const CocoaConsoleView&) = delete;

    // Returns the native NSScrollView* (as void*) to embed in the split view.
    void* nativeView() const;

    // Append a message with color coding. Scrolls to bottom.
    void appendMessage(MessageLevel level, const char* message);

    // Clear all console text.
    void clear();

    // Returns the number of messages currently displayed.
    uint32_t messageCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
