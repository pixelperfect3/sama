#pragma once

#include <optional>
#include <string_view>

#include "engine/assets/EnvironmentAsset.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// CubemapLoader — load a 6-face cubemap from a KTX1/KTX2/DDS file via bimg,
// decode pixels to RGBA32F (converting from whatever the source format is),
// and run the same IBL integration the procedural sky uses to produce an
// EnvironmentAsset ready for IblResources::upload().
//
// Supported containers: .ktx, .ktx2, .dds (anything bimg::imageParse accepts).
// Source pixel formats: RGBA8 (sRGB or linear), BGRA8, RGB8, RGBA16F, RGBA32F,
// plus compressed formats (BC1-7, ETC2, ASTC) as long as bimg can decode
// them — bimg::imageDecodeToRgba32f normalises everything to linear float
// before the integration runs.
//
// Returns std::nullopt on any failure: missing / truncated file, unparseable
// container, non-cubemap, non-square faces, or an unsupported face format.
// On success, the returned EnvironmentAsset contains a 64² irradiance cube,
// a 128² prefiltered specular cube with 8 mips, and a 128² BRDF LUT — same
// dimensions as the procedural sky path — and may be passed directly to
// IblResources::upload().
// ---------------------------------------------------------------------------

std::optional<EnvironmentAsset> loadCubemapEnvironment(std::string_view path);

}  // namespace engine::assets
