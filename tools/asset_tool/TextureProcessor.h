#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "tools/asset_tool/AssetProcessor.h"

namespace engine::tools
{

// ---------------------------------------------------------------------------
// TextureProcessor — discovers and processes texture assets.
//
// Reads source images with stb_image, optionally downscales to the tier's
// maxTextureSize, compresses to ASTC via astcenc, and writes a KTX1 file.
// Falls back to copying the source file if astcenc is unavailable.
// ---------------------------------------------------------------------------

class TextureProcessor
{
public:
    TextureProcessor(const CliArgs& args, const TierConfig& tier);

    /// Discover texture files in the input directory.
    std::vector<AssetEntry> discover();

    /// Process (compress) all texture entries.
    void processAll(const std::vector<AssetEntry>& entries);

    /// Parse "NxM" block size string into width/height.
    static bool parseBlockSize(const std::string& blockSize, int& blockX, int& blockY);

    /// Compute the number of ASTC blocks for a given dimension and block size.
    static int astcBlockCount(int pixels, int blockDim);

    /// Return the GL internal format constant for an ASTC block size.
    static uint32_t astcGlInternalFormat(int blockX, int blockY);

private:
    bool isTextureFile(const std::string& extension) const;
    std::string outputExtension() const;

    /// Process a single texture: load, downscale, compress, write KTX.
    bool processOne(const AssetEntry& entry);

    /// Write ASTC-compressed data as a KTX1 file.
    bool writeKtx(const std::filesystem::path& path, int width, int height, int blockX, int blockY,
                  const uint8_t* data, size_t dataSize);

    CliArgs args_;
    TierConfig tier_;
};

}  // namespace engine::tools
