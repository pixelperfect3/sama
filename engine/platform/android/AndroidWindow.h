#pragma once

#include <android/native_window.h>

#include <cstdint>

namespace engine::platform
{

// ---------------------------------------------------------------------------
// AndroidWindow — wraps ANativeWindow* lifecycle and surface dimensions.
//
// On Android the native window is created/destroyed by the OS and delivered
// via APP_CMD_INIT_WINDOW / APP_CMD_TERM_WINDOW.  This class caches the
// pointer and surface dimensions so the rest of the engine can query them
// without touching Android APIs directly.
// ---------------------------------------------------------------------------

class AndroidWindow
{
public:
    void setNativeWindow(ANativeWindow* window);
    void clearNativeWindow();

    [[nodiscard]] ANativeWindow* nativeWindow() const
    {
        return window_;
    }
    [[nodiscard]] uint32_t width() const
    {
        return width_;
    }
    [[nodiscard]] uint32_t height() const
    {
        return height_;
    }
    [[nodiscard]] float contentScale() const
    {
        return contentScale_;
    }
    [[nodiscard]] bool isReady() const
    {
        return window_ != nullptr;
    }

    /// Re-query width/height from the native window.  Call when the window
    /// surface may have changed size (e.g. orientation change).
    void updateSize();

    /// Set content scale from AConfiguration density (DPI).
    /// 160 dpi = 1.0x, 320 dpi = 2.0x, etc.
    void setDensity(int32_t dpi);

private:
    ANativeWindow* window_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    float contentScale_ = 1.0f;
};

}  // namespace engine::platform
