#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace engine::editor
{

struct ResourceStats;

// ---------------------------------------------------------------------------
// CocoaResourceView -- native NSView with text labels showing live stats.
//
// Pimpl pattern: no AppKit headers leak into this header.
// ---------------------------------------------------------------------------

class CocoaResourceView
{
public:
    CocoaResourceView();
    ~CocoaResourceView();

    CocoaResourceView(const CocoaResourceView&) = delete;
    CocoaResourceView& operator=(const CocoaResourceView&) = delete;

    // Returns the native NSView* (as void*) to embed in the split view.
    void* nativeView() const;

    // Update displayed stats. Called each frame.
    void updateStats(const ResourceStats& stats);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
