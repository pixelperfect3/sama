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

    // Routes kViewOpaque/Transparent to the HDR scene framebuffer for the
    // full post-process chain (tonemap, bloom, FXAA).  Call this when shaders
    // output linear HDR colour and the PostProcessSystem handles tonemapping.
    void beginFrame();

    // Routes kViewOpaque directly to the backbuffer — no post-processing.
    // Use when the fragment shader handles tonemapping inline (e.g. a demo
    // with the Phase-3 Reinhard placeholder in fs_pbr.sc).
    void beginFrameDirect();

    void endFrame();  // calls bgfx::frame()
    void shutdown();
    void resize(uint32_t w, uint32_t h);
    bool isHeadless() const;

    // Access the shared uniform handles (created once during init).
    // Needed by callers that submit draw calls or post-process passes.
    [[nodiscard]] const ShaderUniforms& uniforms() const
    {
        return uniforms_;
    }

    // Access the post-process system (e.g. to retrieve sceneFb for the
    // opaque and transparent passes, or to call submit() at end of frame).
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
    bool initialized_ = false;
    bool headless_ = false;

    ShaderUniforms uniforms_;
    PostProcessSystem postProcess_;
};

}  // namespace engine::rendering
