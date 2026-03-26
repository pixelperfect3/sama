#pragma once

#include <string_view>

#include "engine/assets/IAssetLoader.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// TextureLoader — decodes PNG / JPEG / BMP / TGA to RGBA8 CpuTextureData.
//
// Uses stb_image (already a project dependency via screenshot tests).
// Thread-safe: stb_image is stateless per call.
// ---------------------------------------------------------------------------

class TextureLoader : public IAssetLoader
{
public:
    [[nodiscard]] std::span<const std::string_view> extensions() const override;

    [[nodiscard]] CpuAssetData decode(std::span<const uint8_t> bytes, std::string_view path,
                                      IFileSystem& fs) override;
};

}  // namespace engine::assets
