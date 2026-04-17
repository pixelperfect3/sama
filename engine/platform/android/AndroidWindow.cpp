#include "engine/platform/android/AndroidWindow.h"

#include <android/native_window.h>

namespace engine::platform
{

void AndroidWindow::setNativeWindow(ANativeWindow* window)
{
    window_ = window;
    if (window_)
    {
        updateSize();
    }
}

void AndroidWindow::clearNativeWindow()
{
    window_ = nullptr;
    width_ = 0;
    height_ = 0;
}

void AndroidWindow::updateSize()
{
    if (!window_)
        return;

    int32_t w = ANativeWindow_getWidth(window_);
    int32_t h = ANativeWindow_getHeight(window_);
    if (w > 0 && h > 0)
    {
        width_ = static_cast<uint32_t>(w);
        height_ = static_cast<uint32_t>(h);
    }
}

void AndroidWindow::setDensity(int32_t dpi)
{
    // Android's baseline density is 160 dpi (mdpi = 1.0x).
    if (dpi > 0)
    {
        contentScale_ = static_cast<float>(dpi) / 160.0f;
    }
}

}  // namespace engine::platform
