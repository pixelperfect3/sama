#pragma once
#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>
struct GLFWwindow;

namespace engine::screenshot
{

// ---------------------------------------------------------------------------
// BgfxContext — initialize bgfx once per process.
//
// bgfx::init / bgfx::shutdown may only be called once per process.
// Construct one BgfxContext at program startup (in main()) before running
// any Catch2 tests.  All ScreenshotFixture instances share it implicitly.
// ---------------------------------------------------------------------------
class BgfxContext
{
public:
    static constexpr uint16_t kWidth = 320;
    static constexpr uint16_t kHeight = 240;

    BgfxContext();
    ~BgfxContext();

    BgfxContext(const BgfxContext&) = delete;
    BgfxContext& operator=(const BgfxContext&) = delete;

private:
    GLFWwindow* window_ = nullptr;
};

// ---------------------------------------------------------------------------
// ScreenshotFixture — per-test off-screen render target.
//
// Owns a BGRA8 render target (rtTex_) and a separate blit/readback texture
// (blitTex_) following the bgfx 30-picking example pattern.
// Requires BgfxContext to be alive (created in main before tests run).
// After drawing, call captureFrame() to get RGBA pixels (B/R already swapped).
// ---------------------------------------------------------------------------
class ScreenshotFixture
{
public:
    static constexpr uint16_t kWidth = 320;
    static constexpr uint16_t kHeight = 240;

    ScreenshotFixture();
    ~ScreenshotFixture();

    ScreenshotFixture(const ScreenshotFixture&) = delete;
    ScreenshotFixture& operator=(const ScreenshotFixture&) = delete;

    uint16_t width() const
    {
        return kWidth;
    }
    uint16_t height() const
    {
        return kHeight;
    }

    // Render-target framebuffer — bind to any view as the output target.
    bgfx::FrameBufferHandle captureFb() const
    {
        return captureFb_;
    }

    // After all draw calls are submitted for this frame, call captureFrame().
    // It blits rtTex_ -> blitTex_, schedules readTexture, pumps bgfx::frame()
    // until the readback is ready, and returns RGBA8 pixels (width*height*4 bytes).
    std::vector<uint8_t> captureFrame();

private:
    bgfx::TextureHandle rtTex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTex_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle captureFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blitTex_ = BGFX_INVALID_HANDLE;
};

}  // namespace engine::screenshot
