#pragma once

#include <cstdint>

#include "engine/rendering/RenderSettings.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/systems/PostProcessSystem.h"

namespace engine::rendering
{

struct RendererDesc
{
    void* nativeWindowHandle;
    void* nativeDisplayHandle;
    uint32_t width;
    uint32_t height;
    bool headless;  // if true, use RendererType::Noop (for unit tests)
};

class Renderer
{
public:
    bool init(const RendererDesc& desc);

    // Routes kViewOpaque/Transparent to the HDR scene framebuffer.  endFrame()
    // automatically submits the post-process chain (tonemap is mandatory; bloom,
    // SSAO, FXAA are gated by the active RenderSettings).  Call this every frame
    // — the legacy "direct to backbuffer" path was removed when the inline
    // Reinhard tonemap was stripped from fs_pbr.sc (see NOTES.md "Phase 7
    // unified post-process pipeline").
    void beginFrame();

    void endFrame();  // submits post-process if not headless, then bgfx::frame()
    void shutdown();
    void resize(uint32_t w, uint32_t h);
    bool isHeadless() const;

    // Render settings used by the auto post-process submit in endFrame().
    // Defaults match a low-overhead "tonemap + gamma only" configuration:
    // ACES tonemap on, bloom off, SSAO off, FXAA off.  Game code that wants
    // bloom/FXAA/SSAO sets this once during init.
    void setRenderSettings(const RenderSettings& settings)
    {
        renderSettings_ = settings;
    }

    [[nodiscard]] const RenderSettings& renderSettings() const
    {
        return renderSettings_;
    }

    // Access the shared uniform handles (created once during init).
    // Needed by callers that submit draw calls or post-process passes.
    [[nodiscard]] const ShaderUniforms& uniforms() const
    {
        return uniforms_;
    }

    // Access the post-process system (e.g. to retrieve sceneFb for the
    // opaque and transparent passes).
    PostProcessSystem& postProcess()
    {
        return postProcess_;
    }

    const PostProcessSystem& postProcess() const
    {
        return postProcess_;
    }

    [[nodiscard]] bool isInitialized() const noexcept
    {
        return initialized_;
    }

private:
    // Label the engine's built-in views (Shadow 0..7, Depth Prepass, Opaque,
    // Transparent, ImGui, UI) via bgfx::setViewName so they show up in perf
    // overlays and GPU debuggers (RenderDoc, AGI, Instruments).  Called once
    // from init() after bgfx::init succeeds.  kViewGameUi (48) and
    // kViewDebugHud (49) are owned by the game / DebugHud respectively;
    // post-process sub-pass views are labelled by PostProcessSystem.
    void setupDefaultViewNames();

    // Default settings — cheapest valid post-process: ACES tonemap on, every
    // bloom/SSAO/FXAA pass off.  Cheap enough to run unconditionally.
    static RenderSettings makeDefaultSettings();

    bool initialized_ = false;
    bool headless_ = false;

    ShaderUniforms uniforms_;
    PostProcessSystem postProcess_;
    RenderSettings renderSettings_ = makeDefaultSettings();
};

}  // namespace engine::rendering
