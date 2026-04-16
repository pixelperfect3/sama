#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "tools/asset_tool/AssetProcessor.h"

namespace engine::tools
{

// ---------------------------------------------------------------------------
// TextureProcessor — discovers and processes texture assets.
//
// For now, textures are copied to the output directory. Actual ASTC
// compression requires the `astcenc` CLI tool (the third_party/astc-codec
// library is decode-only). A TODO is logged for each texture that would
// be compressed.
// ---------------------------------------------------------------------------

class TextureProcessor
{
public:
    TextureProcessor(const CliArgs& args, const TierConfig& tier);

    /// Discover texture files in the input directory.
    std::vector<AssetEntry> discover();

    /// Process (copy/compress) all texture entries.
    void processAll(const std::vector<AssetEntry>& entries);

private:
    bool isTextureFile(const std::string& extension) const;
    std::string outputExtension() const;

    CliArgs args_;
    TierConfig tier_;
};

}  // namespace engine::tools
