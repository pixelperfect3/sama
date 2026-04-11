#pragma once

#include <optional>
#include <string_view>

#include "engine/assets/EnvironmentAsset.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// HdrLoader — load a Radiance `.hdr` equirectangular environment map and run
// the same CPU IBL integration the procedural sky uses (irradiance cubemap,
// GGX prefiltered specular cubemap, BRDF LUT), producing an `EnvironmentAsset`
// ready for `IblResources::upload()` or serialization to a `.env` file via
// `engine::assets::saveEnvironmentAsset`.
//
// The loader uses stb_image's `stbi_loadf` path and is linked with
// STB_IMAGE_STATIC so its symbols do not collide with the main
// `TextureLoader.cpp` translation unit that owns STB_IMAGE_IMPLEMENTATION.
// ---------------------------------------------------------------------------

// Load an equirectangular Radiance HDR file and bake an `EnvironmentAsset`.
// Returns `std::nullopt` if the file cannot be read or parsed by stb_image.
// Integration takes several seconds on a single thread — same cost as
// `IblResources::generateDefaultAsset()`.
std::optional<EnvironmentAsset> loadHdrEnvironment(std::string_view path);

}  // namespace engine::assets
