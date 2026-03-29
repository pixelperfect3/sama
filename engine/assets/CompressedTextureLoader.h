#pragma once

#include <string_view>

#include "engine/assets/IAssetLoader.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// CompressedTextureLoader — loads KTX and DDS compressed textures.
//
// These formats are passed through as raw bytes to bgfx::createTexture(),
// which handles format detection and GPU upload internally.
// Thread-safe: decode() is stateless.
// ---------------------------------------------------------------------------

class CompressedTextureLoader : public IAssetLoader
{
public:
    [[nodiscard]] std::span<const std::string_view> extensions() const override;

    [[nodiscard]] CpuAssetData decode(std::span<const uint8_t> bytes, std::string_view path,
                                      IFileSystem& fs) override;
};

}  // namespace engine::assets
