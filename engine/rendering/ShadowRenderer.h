#pragma once

#include <bgfx/bgfx.h>

#include "engine/math/Types.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// ShadowDesc — configuration for the shadow atlas.
// ---------------------------------------------------------------------------

struct ShadowDesc
{
    uint32_t resolution = 2048;  // texture dimension (square atlas)
    uint32_t cascadeCount = 1;   // Phase 4 = 1, Phase 9 = up to 4
};

// ---------------------------------------------------------------------------
// ShadowRenderer
//
// Manages the shadow atlas texture and per-cascade framebuffers.
// All cascades are packed into one texture (shadow atlas):
//   1 cascade:  full atlas (resolution x resolution)
//   2 cascades: 2 tiles side-by-side (each resolution/2 wide)
//   4 cascades: 2x2 grid (each resolution/2 x resolution/2)
//
// Using an atlas means one sampler (s_shadowMap) for all cascades.
//
// Usage per frame:
//   1. Call beginCascade(i, lightView, lightProj) before submitting shadow draws.
//   2. Bind atlasTexture() to s_shadowMap in the PBR pass.
//   3. Upload shadowMatrix(i) as u_shadowMatrix[i].
// ---------------------------------------------------------------------------

class ShadowRenderer
{
public:
    // Initialise the atlas texture and per-cascade framebuffers.
    // Must be called after bgfx::init(). Returns false on failure.
    bool init(const ShadowDesc& desc);

    // Destroy all bgfx handles.  Safe to call multiple times.
    void shutdown();

    // Set up the bgfx view for cascade i (call before submitting shadow draws):
    //   - Binds the per-cascade framebuffer.
    //   - Sets the viewport to the cascade tile within the atlas.
    //   - Clears depth to 1.0 (far plane).
    //   - Uploads the light view/projection matrices.
    // Stores lightView and lightProj for shadowMatrix() computation.
    void beginCascade(uint32_t cascadeIdx, const math::Mat4& lightView,
                      const math::Mat4& lightProj);

    // Shadow atlas texture handle (bind as s_shadowMap in the PBR pass).
    [[nodiscard]] bgfx::TextureHandle atlasTexture() const
    {
        return atlas_;
    }

    // Shadow matrix for cascade i: worldPos -> shadow UV [0,1]^2 x depth.
    // = atlasTranslate * atlasScale * biasMatrix * lightProj * lightView
    // Bias: XY shifted from [-1,1] to [0,1]; Z unchanged (GLM_FORCE_DEPTH_ZERO_TO_ONE).
    [[nodiscard]] math::Mat4 shadowMatrix(uint32_t cascadeIdx) const;

    // UV rect for cascade i in the atlas: vec4(x0, y0, x1, y1) in [0,1].
    [[nodiscard]] math::Vec4 cascadeUvRect(uint32_t cascadeIdx) const;

    [[nodiscard]] const ShadowDesc& desc() const
    {
        return desc_;
    }

private:
    ShadowDesc desc_;
    bgfx::TextureHandle atlas_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle fb_[4] = {
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
    };
    math::Mat4 lightView_[4];
    math::Mat4 lightProj_[4];
};

}  // namespace engine::rendering
