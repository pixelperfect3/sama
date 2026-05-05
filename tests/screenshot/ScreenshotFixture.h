#pragma once
#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

#include "engine/rendering/HandleTypes.h"
#include "engine/rendering/RenderSettings.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/systems/PostProcessSystem.h"

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
// Owns:
//   * A PostProcessSystem with its own HDR scene FB.  Lit / PBR / scene tests
//     bind sceneFb() as the opaque pass target; their shader output is linear
//     HDR.  runTonemap() submits a single tonemap pass that writes captureFb_.
//   * captureFb_ — LDR BGRA8 target read back to disk as the golden image.
//                  UI tests that already produce LDR output (text, panels,
//                  sprites) bind captureFb() directly and skip runTonemap().
//
// Requires BgfxContext to be alive (created in main before tests run).
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

    // HDR scene framebuffer (RGBA16F) owned by the internal PostProcessSystem.
    // Lit / PBR tests bind this as the opaque pass target; runTonemap() then
    // converts it into captureFb_ for readback.
    engine::rendering::FrameBufferHandle sceneFb() const;

    // LDR capture framebuffer (RGBA8).  UI / unlit tests that already write
    // gamma-correct LDR output bind this directly and skip runTonemap().
    engine::rendering::FrameBufferHandle captureFb() const
    {
        return {captureFb_.idx};
    }

    // Submit the tonemap pass on viewId.  Reads the HDR scene fb and writes
    // captureFb_.  Internally calls PostProcessSystem::submit with bloom /
    // SSAO / FXAA all off, redirecting the tonemap output via the optional
    // finalTarget parameter.  Mirrors the desktop / iOS / Android runtime
    // path (Renderer::endFrame) — the goldens then represent what real
    // hardware would actually produce.
    void runTonemap(engine::rendering::ViewId viewId);

    // After all draw calls (and runTonemap, for HDR tests) are submitted,
    // call captureFrame().  It blits captureFb_'s color attachment into the
    // readback texture, schedules readTexture, pumps bgfx::frame() until
    // the readback is ready, and returns RGBA8 pixels (width*height*4 bytes).
    std::vector<uint8_t> captureFrame();

    // 1×1 RGBA8 white texture for default sampler bindings.
    bgfx::TextureHandle whiteTex() const
    {
        return whiteTex_;
    }

    // 1×1 neutral normal map: (128, 128, 255) → tangent-space (0, 0, 1).
    bgfx::TextureHandle neutralNormalTex() const
    {
        return neutralNormalTex_;
    }

private:
    // PostProcess + uniforms — owns the HDR scene FB and the tonemap shader.
    engine::rendering::PostProcessSystem postProcess_;
    engine::rendering::ShaderUniforms uniforms_;

    // LDR capture target (RGBA8 + D24).
    bgfx::TextureHandle rtTex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTex_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle captureFb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blitTex_ = BGFX_INVALID_HANDLE;

    bgfx::TextureHandle whiteTex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle neutralNormalTex_ = BGFX_INVALID_HANDLE;
};

}  // namespace engine::screenshot
