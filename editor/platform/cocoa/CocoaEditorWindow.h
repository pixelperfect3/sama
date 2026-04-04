#pragma once

#include <memory>

#include "editor/platform/IEditorWindow.h"

namespace engine::editor
{

// ---------------------------------------------------------------------------
// CocoaEditorWindow -- native macOS window backed by NSWindow + CAMetalLayer.
//
// Pimpl pattern: no Cocoa/AppKit headers leak into this header.
// The .mm implementation creates NSApplication, NSWindow, a custom NSView
// with a CAMetalLayer, and polls events each frame.
// ---------------------------------------------------------------------------

class CocoaEditorWindow final : public IEditorWindow
{
public:
    CocoaEditorWindow();
    ~CocoaEditorWindow() override;

    // Non-copyable, non-movable.
    CocoaEditorWindow(const CocoaEditorWindow&) = delete;
    CocoaEditorWindow& operator=(const CocoaEditorWindow&) = delete;
    CocoaEditorWindow(CocoaEditorWindow&&) = delete;
    CocoaEditorWindow& operator=(CocoaEditorWindow&&) = delete;

    bool init(uint32_t width, uint32_t height, const char* title) override;
    void shutdown() override;

    bool shouldClose() const override;
    void pollEvents() override;

    void* nativeHandle() const override;
    void* nativeLayer() const override;

    uint32_t width() const override;
    uint32_t height() const override;
    uint32_t framebufferWidth() const override;
    uint32_t framebufferHeight() const override;
    float contentScale() const override;

    double mouseX() const override;
    double mouseY() const override;
    double mouseDeltaX() const override;
    double mouseDeltaY() const override;
    double scrollDeltaY() const override;
    bool isLeftMouseDown() const override;
    bool isRightMouseDown() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
