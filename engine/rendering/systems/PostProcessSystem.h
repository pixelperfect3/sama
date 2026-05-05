#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

#include "engine/rendering/PostProcessResources.h"
#include "engine/rendering/RenderSettings.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/SsaoSystem.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// PostProcessSystem — submits all post-process passes for one frame.
//
// Pass sequence (when bloom + FXAA are both enabled):
//
//   viewId+0   Bloom threshold   hdrColor → bloomLevel[0]  (full res)
//   viewId+1   Bloom downsample  bloomLevel[0] → bloomLevel[1]
//   …
//   viewId+N   Bloom downsample  bloomLevel[N-2] → bloomLevel[N-1]
//   viewId+N+1 Bloom upsample    bloomLevel[N-1] → bloomLevel[N-2]  (additive)
//   …
//   viewId+2N  Bloom upsample    bloomLevel[1] → bloomLevel[0]      (additive)
//   viewId+2N+1 Tonemap+bloom    hdrColor + bloomLevel[0] → ldrFb
//   viewId+2N+2 FXAA             ldrColor → backbuffer
//
// When bloom is disabled: skip threshold + downsample + upsample, tonemap reads
// only hdrColor and writes directly to ldrFb (or backbuffer when FXAA disabled).
//
// When FXAA is disabled: tonemap writes directly to the backbuffer.
//
// submit() returns the next available view ID so the caller can continue
// allocating views above the post-process range.
// ---------------------------------------------------------------------------

class PostProcessSystem
{
public:
    // Initialise: validate resources and load all shader programs.
    // Returns false if resource allocation fails.
    bool init(uint16_t w, uint16_t h);

    void shutdown();

    // Resize the post-process framebuffers.
    void resize(uint16_t w, uint16_t h);

    // Access the underlying resource container (read-only).
    const PostProcessResources& resources() const
    {
        return resources_;
    }

    // Submit all post-process passes.
    // firstViewId is typically kViewPostProcessBase.
    // SSAO (if settings.ssao.enabled) runs on firstViewId; bloom and tonemap
    // follow on subsequent view IDs.
    // finalTarget overrides the backbuffer as the destination for the last
    // pass (tonemap when FXAA is off, FXAA when enabled).  Pass an invalid
    // handle (the default) to write to the active swapchain backbuffer; pass
    // a real framebuffer to capture the output (used by screenshot tests).
    // Returns the next available view ID after all submitted passes.
    bgfx::ViewId submit(const PostProcessSettings& settings, const ShaderUniforms& uniforms,
                        bgfx::ViewId firstViewId = kViewPostProcessBase,
                        bgfx::FrameBufferHandle finalTarget = bgfx::FrameBufferHandle{
                            bgfx::kInvalidHandle});

    // Read-only access to the SSAO sub-system (e.g. to bind ssaoMap to PBR).
    const SsaoSystem& ssaoSystem() const
    {
        return ssaoSystem_;
    }

private:
    PostProcessResources resources_;
    SsaoSystem ssaoSystem_;

    // Fullscreen triangle vertex buffer (3 vertices, clip-space positions).
    bgfx::VertexBufferHandle fsTriVb_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout fsTriLayout_;

    // Shader programs — loaded once in init(), destroyed in shutdown().
    bgfx::ProgramHandle bloomThreshProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bloomDownProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bloomUpProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle tonemapProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle fxaaProgram_ = BGFX_INVALID_HANDLE;
};

}  // namespace engine::rendering
