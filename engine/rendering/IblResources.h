#pragma once

#include <bgfx/bgfx.h>

#include "engine/assets/EnvironmentAsset.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// IblResources — GPU-side image-based lighting resources.
//
// Manages three textures required for the IBL split-sum ambient term:
//   - Irradiance cubemap       (diffuse, pre-integrated over hemisphere)
//   - Prefiltered specular cubemap (specular, mip-encoded roughness levels)
//   - BRDF LUT                 (split-sum scale/bias for F0 parametrisation)
//
// Usage:
//   IblResources ibl;
//   ibl.generateDefault();   // procedural sky/ground — no external asset needed
//   // or:
//   ibl.upload(envAsset);    // upload precomputed offline data
//
//   // Bind before PBR draw calls:
//   bgfx::setTexture(6, uniforms.s_irradiance,  ibl.irradiance());
//   bgfx::setTexture(7, uniforms.s_prefiltered, ibl.prefiltered());
//   bgfx::setTexture(8, uniforms.s_brdfLut,     ibl.brdfLut());
//
//   ibl.shutdown();
// ---------------------------------------------------------------------------

class IblResources
{
public:
    // Upload precomputed IBL data to GPU. Call after bgfx::init().
    bool upload(const assets::EnvironmentAsset& env);

    // Generate a procedural IBL with a realistic sunset-like sky model:
    //   - 64×64 irradiance cubemap (cosine-weighted hemisphere integration)
    //   - 128×128 prefiltered cubemap with 8 mip levels (GGX importance sampling)
    //   - 128×128 BRDF LUT (1024-sample Hammersley GGX split-sum integration)
    bool generateDefault();

    // Destroy all GPU handles.
    void shutdown();

    bgfx::TextureHandle irradiance() const
    {
        return irradiance_;
    }
    bgfx::TextureHandle prefiltered() const
    {
        return prefiltered_;
    }
    bgfx::TextureHandle brdfLut() const
    {
        return brdfLut_;
    }

    // Returns true only when the BRDF LUT handle is valid (all textures created).
    bool isValid() const
    {
        return bgfx::isValid(brdfLut_);
    }

private:
    bgfx::TextureHandle irradiance_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle prefiltered_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle brdfLut_ = BGFX_INVALID_HANDLE;
};

}  // namespace engine::rendering
