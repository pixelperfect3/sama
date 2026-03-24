#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

#include "engine/rendering/RenderSettings.h"
#include "engine/rendering/ShaderUniforms.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// SsaoSystem — Screen-Space Ambient Occlusion render pass (Phase 8).
//
// Runs as a full-screen triangle pass after the opaque pass and before bloom.
// It reads the scene depth buffer (s_depth), reconstructs view-space positions
// using bgfx's predefined u_invProj matrix, samples a fixed hemisphere kernel
// (u_ssaoKernel), and writes the occlusion factor into an R8 texture
// (ssaoMap_) that the PBR shader later uses to modulate ambient lighting.
//
// Lifecycle:
//   init(w, h)     — allocate GPU resources, generate kernel, load shader
//   shutdown()     — free all GPU resources
//   submit(...)    — emit one bgfx draw call for the SSAO pass
//
// Headless (Noop) mode:
//   All bgfx::create* calls return BGFX_INVALID_HANDLE — submit() is a no-op.
// ---------------------------------------------------------------------------

class SsaoSystem
{
public:
    // Allocate the ssaoMap texture + framebuffer, generate the hemisphere
    // kernel with a fixed seed for determinism, and load the SSAO program.
    // Safe to call multiple times (idempotent on same dimensions).
    void init(uint16_t w, uint16_t h);

    // Destroy every bgfx handle owned by this system.
    void shutdown();

    // Emit one full-screen triangle draw call for the SSAO pass.
    //   settings  — radius, bias, sampleCount from PostProcessSettings.
    //   uniforms  — shared uniform handles (u_ssaoKernel, u_ssaoParams, s_depth).
    //   depthTex  — the scene depth texture (from PostProcessResources::sceneDepth()).
    //   viewId    — the bgfx view to submit to.
    //   fsTriVb   — the fullscreen triangle vertex buffer.
    void submit(const SsaoSettings& settings, const ShaderUniforms& uniforms,
                bgfx::TextureHandle depthTex, bgfx::ViewId viewId,
                bgfx::VertexBufferHandle fsTriVb);

    // The R8 occlusion texture written by this pass.
    // BGFX_INVALID_HANDLE before init() or after shutdown().
    bgfx::TextureHandle ssaoMap() const
    {
        return ssaoMap_;
    }

    uint16_t width() const
    {
        return width_;
    }

    uint16_t height() const
    {
        return height_;
    }

private:
    uint16_t width_ = 0;
    uint16_t height_ = 0;

    bgfx::TextureHandle ssaoMap_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle ssaoFb_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;

    // 16 hemisphere samples stored as vec4 (w = 0).
    // Generated once in init() with a fixed seed for determinism.
    float kernel_[16][4]{};
};

}  // namespace engine::rendering
