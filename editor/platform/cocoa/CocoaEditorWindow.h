#pragma once

#include <memory>

#include "editor/platform/IEditorWindow.h"

namespace engine::editor
{

class CocoaHierarchyView;
class CocoaPropertiesView;
class CocoaConsoleView;
class CocoaResourceView;

// ---------------------------------------------------------------------------
// CocoaEditorWindow -- native macOS window backed by NSWindow + NSSplitView
// layout with native panels and a central CAMetalLayer viewport.
//
// Pimpl pattern: no Cocoa/AppKit headers leak into this header.
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
    bool isKeyPressed(uint8_t keyCode) const override;

    bool isCommandDown() const override;
    bool isShiftDown() const override;
    bool isControlDown() const override;
    bool isOptionDown() const override;

    // --- Viewport-specific dimensions ----------------------------------------
    // These return the size of just the center 3D viewport panel,
    // not the entire window.

    uint32_t viewportWidth() const;
    uint32_t viewportHeight() const;
    uint32_t viewportFramebufferWidth() const;
    uint32_t viewportFramebufferHeight() const;

    // Returns true if the mouse is currently over the 3D viewport panel.
    bool isMouseOverViewport() const;

    // --- Native panel views --------------------------------------------------
    CocoaHierarchyView* hierarchyView() const;
    CocoaPropertiesView* propertiesView() const;
    CocoaConsoleView* consoleView() const;
    CocoaResourceView* resourceView() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
