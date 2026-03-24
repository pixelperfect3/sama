#include "ScreenshotFixture.h"

#include <stdexcept>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
// Use the Objective-C runtime C API so this file can stay as plain C++.
#include <objc/message.h>
#include <objc/runtime.h>
#elif defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#endif

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

namespace engine::screenshot
{

// ---------------------------------------------------------------------------
// BgfxContext — owns the hidden GLFW window and bgfx lifetime.
// Construct exactly once per process in main(), before running any tests.
//
// Single-threaded bgfx mode:
//   bgfx::renderFrame() is called ONCE, before bgfx::init(), to tell bgfx
//   that the calling thread is both the main thread and the render thread.
//   After init(), only bgfx::frame() is used — never bgfx::renderFrame() again.
//   Calling renderFrame() a second time causes double-processing of command
//   buffers and a crash in Metal texture creation.
// ---------------------------------------------------------------------------

BgfxContext::BgfxContext()
{
    if (!glfwInit())
        throw std::runtime_error("BgfxContext: glfwInit() failed");

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window_ = glfwCreateWindow(kWidth, kHeight, "screenshot", nullptr, nullptr);
    if (!window_)
    {
        glfwTerminate();
        throw std::runtime_error("BgfxContext: glfwCreateWindow() failed");
    }

    void* nwh = nullptr;
#if defined(__APPLE__)
    void* nsWindow = glfwGetCocoaWindow(window_);
    using MsgSendFn = void* (*)(void*, SEL);
    using MsgBoolFn = void (*)(void*, SEL, bool);
    using MsgLayerFn = void (*)(void*, SEL, void*);
    using ClassFn = void* (*)(void*, SEL);

    void* nsView = reinterpret_cast<MsgSendFn>(objc_msgSend)(nsWindow, sel_getUid("contentView"));

    // GLFW with GLFW_NO_API does not set up a CAMetalLayer automatically.
    // Attach one explicitly so the bgfx Metal backend can use it.
    reinterpret_cast<MsgBoolFn>(objc_msgSend)(nsView, sel_getUid("setWantsLayer:"), true);
    void* metalLayerClass = objc_getClass("CAMetalLayer");
    void* metalLayer =
        reinterpret_cast<ClassFn>(objc_msgSend)(metalLayerClass, sel_getUid("layer"));
    reinterpret_cast<MsgLayerFn>(objc_msgSend)(nsView, sel_getUid("setLayer:"), metalLayer);

    // bgfx Metal backend expects a CAMetalLayer* as nwh (not NSView*)
    nwh = metalLayer;
#elif defined(_WIN32)
    nwh = glfwGetWin32Window(window_);
#elif defined(__linux__)
    nwh = reinterpret_cast<void*>(glfwGetX11Window(window_));
#endif

    // Call bgfx::renderFrame() ONCE before bgfx::init() to enter single-threaded
    // mode.  After this point, bgfx::frame() is the ONLY frame pump call used.
    // A second call to bgfx::renderFrame() would double-process the command
    // buffers, corrupting the texture creation pipeline and causing a SIGSEGV.
    bgfx::renderFrame();

    bgfx::Init init;
#if defined(__APPLE__)
    init.type = bgfx::RendererType::Metal;
#else
    init.type = bgfx::RendererType::Vulkan;
#endif
    init.platformData.nwh = nwh;
    init.resolution.width = kWidth;
    init.resolution.height = kHeight;
    init.resolution.reset = BGFX_RESET_NONE;

    if (!bgfx::init(init))
    {
        glfwDestroyWindow(window_);
        glfwTerminate();
        throw std::runtime_error("BgfxContext: bgfx::init() failed");
    }
}

BgfxContext::~BgfxContext()
{
    bgfx::frame();  // flush any pending commands before shutdown
    bgfx::shutdown();
    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

// ---------------------------------------------------------------------------
// ScreenshotFixture — per-test off-screen render target.
// BgfxContext must be alive (created in main before tests run).
// ---------------------------------------------------------------------------

ScreenshotFixture::ScreenshotFixture()
{
    // RGBA8 render target + D24 depth.
    // BGRA8 is not universally supported as an RT on Metal/bgfx.
    rtTex_ = bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8,
                                   BGFX_TEXTURE_RT);

    depthTex_ =
        bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::D24, BGFX_TEXTURE_RT);

    bgfx::TextureHandle attachments[2] = {rtTex_, depthTex_};
    captureFb_ = bgfx::createFrameBuffer(2, attachments, false);

    // Separate blit/readback texture (bgfx 30-picking pattern).
    // Cannot combine RT + READ_BACK on all Metal configurations.
    blitTex_ = bgfx::createTexture2D(kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8,
                                     BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK |
                                         BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
                                         BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP |
                                         BGFX_SAMPLER_V_CLAMP);

    // Process all deferred texture creation commands.
    bgfx::frame();
}

ScreenshotFixture::~ScreenshotFixture()
{
    // Flush any pending commands before destroying resources.
    bgfx::frame();

    if (bgfx::isValid(blitTex_))
        bgfx::destroy(blitTex_);
    if (bgfx::isValid(captureFb_))
        bgfx::destroy(captureFb_);
    if (bgfx::isValid(depthTex_))
        bgfx::destroy(depthTex_);
    if (bgfx::isValid(rtTex_))
        bgfx::destroy(rtTex_);
}

std::vector<uint8_t> ScreenshotFixture::captureFrame()
{
    // Blit RT to readback texture (view 200 — well above pipeline's 0-47).
    bgfx::blit(200, blitTex_, 0, 0, rtTex_, 0, 0, kWidth, kHeight);

    // Schedule readback.  bgfx fills rgba after readbackFrame pump cycles.
    std::vector<uint8_t> rgba(kWidth * kHeight * 4);
    uint32_t readbackFrame = bgfx::readTexture(blitTex_, rgba.data());

    // In single-threaded bgfx mode, bgfx::frame() handles both CPU and GPU work.
    // Do NOT call bgfx::renderFrame() here — it causes double-command-buffer
    // processing and crashes in Metal texture creation (see BgfxContext notes).
    uint32_t f;
    do
    {
        f = bgfx::frame();
    } while (f < readbackFrame);

    return rgba;
}

}  // namespace engine::screenshot
