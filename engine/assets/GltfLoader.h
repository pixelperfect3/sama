#pragma once

#include <string_view>

#include "engine/assets/IAssetLoader.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// GltfLoader — decodes .gltf and .glb files into CpuSceneData.
//
// Uses cgltf (single C header) for JSON/binary parsing.
// External texture images referenced by the glTF are loaded via the
// IFileSystem passed to decode(), decoded with stb_image.
//
// Thread-safe: all state is local to each decode() call.
// ---------------------------------------------------------------------------

class GltfLoader : public IAssetLoader
{
public:
    [[nodiscard]] std::span<const std::string_view> extensions() const override;

    [[nodiscard]] CpuAssetData decode(std::span<const uint8_t> bytes, std::string_view path,
                                      IFileSystem& fs) override;
};

}  // namespace engine::assets
