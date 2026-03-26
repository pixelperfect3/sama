#include "engine/platform/desktop/GlfwWindow.h"

#include <stdexcept>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
// Use the Objective-C runtime C API so this file can stay as plain C++.
// objc_msgSend is ABI-equivalent to an ObjC message send on Apple platforms.
#include <objc/message.h>
#include <objc/runtime.h>
#elif defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace engine::platform
{

GlfwWindow::GlfwWindow(const WindowDesc& desc) : width_(desc.width), height_(desc.height)
{
    if (!glfwInit())
    {
        throw std::runtime_error("GlfwWindow: glfwInit() failed");
    }

    // bgfx manages the graphics API — tell GLFW not to create an OpenGL context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window_ = glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height),
                               desc.title, nullptr, nullptr);

    if (!window_)
    {
        glfwTerminate();
        throw std::runtime_error("GlfwWindow: glfwCreateWindow() failed");
    }
}

GlfwWindow::~GlfwWindow()
{
    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool GlfwWindow::shouldClose() const
{
    return glfwWindowShouldClose(window_) != 0;
}

void GlfwWindow::pollEvents()
{
    glfwPollEvents();
}

void* GlfwWindow::nativeWindowHandle() const
{
#if defined(__APPLE__)
    // bgfx Metal backend requires a CAMetalLayer*, not a raw NSView*.
    // GLFW_NO_API does not attach any layer, so we create and attach one here.
    // Uses the ObjC runtime C API so this file can stay plain C++ (no .mm).
    void* nsWindow = glfwGetCocoaWindow(window_);

    using MsgSend0 = void* (*)(void*, SEL);
    using MsgSendObj = void (*)(void*, SEL, void*);
    using MsgSendBOOL = void (*)(void*, SEL, BOOL);

    void* nsView = reinterpret_cast<MsgSend0>(objc_msgSend)(nsWindow, sel_getUid("contentView"));

    // [nsView setWantsLayer:YES]
    reinterpret_cast<MsgSendBOOL>(objc_msgSend)(nsView, sel_getUid("setWantsLayer:"), YES);

    // metalLayer = [CAMetalLayer layer]
    void* metalLayerCls = reinterpret_cast<void*>(objc_getClass("CAMetalLayer"));
    void* metalLayer = reinterpret_cast<MsgSend0>(objc_msgSend)(metalLayerCls, sel_getUid("layer"));

    // [nsView setLayer:metalLayer]  — view retains the layer
    reinterpret_cast<MsgSendObj>(objc_msgSend)(nsView, sel_getUid("setLayer:"), metalLayer);

    return metalLayer;
#elif defined(_WIN32)
    return glfwGetWin32Window(window_);
#else
    return nullptr;
#endif
}

void* GlfwWindow::nativeDisplayHandle() const
{
    // nullptr on macOS and Windows; on Linux this would be the X11 Display*
    return nullptr;
}

uint32_t GlfwWindow::width() const
{
    return width_;
}

uint32_t GlfwWindow::height() const
{
    return height_;
}

// Factory function defined in Window.h
std::unique_ptr<IWindow> createWindow(const WindowDesc& desc)
{
    return std::make_unique<GlfwWindow>(desc);
}

}  // namespace engine::platform
