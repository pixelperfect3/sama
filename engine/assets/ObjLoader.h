#pragma once

#include <string_view>

#include "engine/assets/IAssetLoader.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// ObjLoader — loads Wavefront OBJ files into CpuSceneData.
//
// Supports positions, normals, texture coordinates, and MTL materials.
// Generates tangents when normals and UVs are available.
// Uses tinyobjloader (vendored single-header library).
// Thread-safe: tinyobjloader is stateless per call.
// ---------------------------------------------------------------------------

class ObjLoader : public IAssetLoader
{
public:
    [[nodiscard]] std::span<const std::string_view> extensions() const override;

    [[nodiscard]] CpuAssetData decode(std::span<const uint8_t> bytes, std::string_view path,
                                      IFileSystem& fs) override;
};

}  // namespace engine::assets
