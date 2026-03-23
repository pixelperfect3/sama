#pragma once

#include <cstdint>
#include <memory>

namespace engine::platform
{

struct WindowDesc
{
    uint32_t width;
    uint32_t height;
    const char* title;
};

class IWindow
{
public:
    virtual ~IWindow() = default;

    virtual bool shouldClose() const = 0;
    virtual void pollEvents() = 0;
    virtual void* nativeWindowHandle() const = 0;   // NSView* on Mac, HWND on Windows
    virtual void* nativeDisplayHandle() const = 0;  // nullptr on Mac/Windows, Display* on Linux
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
};

std::unique_ptr<IWindow> createWindow(const WindowDesc&);

}  // namespace engine::platform
