#pragma once

#include "engine/platform/Window.h"

struct GLFWwindow;

namespace engine::platform
{

class GlfwWindow : public IWindow
{
public:
    explicit GlfwWindow(const WindowDesc& desc);
    ~GlfwWindow() override;

    bool shouldClose() const override;
    void pollEvents() override;
    void* nativeWindowHandle() const override;
    void* nativeDisplayHandle() const override;
    uint32_t width() const override;
    uint32_t height() const override;

    // Return the underlying GLFW handle for use with input backends.
    [[nodiscard]] GLFWwindow* glfwHandle() const
    {
        return window_;
    }

private:
    GLFWwindow* window_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

}  // namespace engine::platform
