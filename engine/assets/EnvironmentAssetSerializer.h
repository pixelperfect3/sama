#pragma once

#include <optional>
#include <string_view>

#include "engine/assets/EnvironmentAsset.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// EnvironmentAsset binary serializer
//
// Stores an EnvironmentAsset to a single self-contained binary blob.
// Layout (little-endian throughout):
//
//   char     magic[4]            "SAEV"  (Sama EnVironment)
//   uint32_t version             current = 1
//   uint32_t irradianceSize      e.g. 64
//   uint32_t prefilteredSize     e.g. 128
//   uint32_t prefilteredMips     e.g. 8
//   uint32_t brdfLutSize         e.g. 128
//   // 6 faces × irradianceSize² × 4 floats (RGBA32F)
//   float    irradiance[6 * sz * sz * 4]
//   // 6 faces × all mips, mip-major within face. Each mip is mipSz² × 4 floats.
//   float    prefiltered[...]
//   // brdfLutSize² × 2 floats (RG32F)
//   float    brdfLut[brdfLutSize² * 2]
//
// The serializer is stable as long as the version constant doesn't change.
// Bump kCurrentVersion when the procedural sky model or any cubemap layout
// is altered, and re-bake the cached file.
// ---------------------------------------------------------------------------

inline constexpr uint32_t kEnvironmentAssetVersion = 1;

// Write `env` to the file at `path`. Overwrites if it exists. Returns true
// on success, false on any I/O error.
bool saveEnvironmentAsset(std::string_view path, const EnvironmentAsset& env);

// Load an EnvironmentAsset from `path`. Returns std::nullopt if the file is
// missing, has the wrong magic, has the wrong version, or fails any size
// check. The caller can pass the result to IblResources::upload().
std::optional<EnvironmentAsset> loadEnvironmentAsset(std::string_view path);

}  // namespace engine::assets
