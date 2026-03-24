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
    void beginFrame();
    void endFrame();  // calls bgfx::frame()
    void shutdown();
    void resize(uint32_t w, uint32_t h);
    bool isHeadless() const;

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

private:
    bool initialized_ = false;
    bool headless_ = false;

    ShaderUniforms uniforms_;
    PostProcessSystem postProcess_;
};

}  // namespace engine::rendering
