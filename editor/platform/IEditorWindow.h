#pragma once

#include <cstdint>
#include <string>

namespace engine::editor
{

class IEditorWindow
{
public:
    virtual ~IEditorWindow() = default;

    virtual bool init(uint32_t width, uint32_t height, const char* title) = 0;
    virtual void shutdown() = 0;

    virtual bool shouldClose() const = 0;
    virtual void pollEvents() = 0;

    // Native handle for bgfx init (NSWindow* on Mac, HWND on Windows).
    virtual void* nativeHandle() const = 0;

    // Native layer for bgfx rendering (CAMetalLayer* on Mac, HWND on Windows).
    virtual void* nativeLayer() const = 0;

    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;

    // Framebuffer dimensions in physical pixels (width * contentScale).
    virtual uint32_t framebufferWidth() const = 0;
    virtual uint32_t framebufferHeight() const = 0;

    // HiDPI scale factor.
    virtual float contentScale() const = 0;

    // Mouse state for the viewport.
    virtual double mouseX() const = 0;
    virtual double mouseY() const = 0;
    virtual double mouseDeltaX() const = 0;
    virtual double mouseDeltaY() const = 0;
    virtual double scrollDeltaY() const = 0;
    virtual bool isLeftMouseDown() const = 0;
    virtual bool isRightMouseDown() const = 0;

    // Keyboard state — returns true if the key was pressed this frame.
    // keyCode uses platform-independent virtual key codes (ASCII for letters).
    virtual bool isKeyPressed(uint8_t keyCode) const = 0;

    // Modifier key state — returns true if the modifier is currently held.
    virtual bool isCommandDown() const = 0;
    virtual bool isShiftDown() const = 0;
    virtual bool isControlDown() const = 0;
    virtual bool isOptionDown() const = 0;

    // Native file dialogs. Return empty string if cancelled.
    virtual std::string showSaveDialog(const char* defaultName, const char* extension) = 0;
    virtual std::string showOpenDialog(const char* extension) = 0;

    // Window title.
    virtual void setWindowTitle(const char* title) = 0;
};

}  // namespace engine::editor
