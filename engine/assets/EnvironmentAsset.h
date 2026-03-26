#pragma once

#include <cstdint>
#include <vector>

namespace engine::assets
{

// ---------------------------------------------------------------------------
// EnvironmentAsset — CPU-side environment data before GPU upload.
//
// All three textures are precomputed offline (not at runtime).
// Upload via IblResources::upload().
// ---------------------------------------------------------------------------

struct EnvironmentAsset
{
    // Irradiance cubemap (32x32 each face, 1 mip)
    uint32_t irradianceSize = 32;
    std::vector<std::vector<float>> irradianceFaces;  // [6][size*size*4] RGBA32F

    // Prefiltered specular cubemap (512x512, full mip chain)
    uint32_t prefilteredSize = 512;
    uint8_t prefilteredMips = 0;  // computed: log2(prefilteredSize)+1
    std::vector<std::vector<std::vector<float>>> prefilteredFaces;
    // [6][mip][mipSize*mipSize*4] RGBA16F (store as float, upload as RGBA16F)

    // BRDF LUT (512x512 RG32F)
    uint32_t brdfLutSize = 512;
    std::vector<float> brdfLutData;  // [size*size*2] RG32F
};

}  // namespace engine::assets
