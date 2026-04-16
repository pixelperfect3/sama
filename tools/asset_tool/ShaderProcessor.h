#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "tools/asset_tool/AssetProcessor.h"

namespace engine::tools
{

// ---------------------------------------------------------------------------
// ShaderProcessor — discovers and compiles shader assets.
//
// Finds .sc shader files and compiles them via bgfx's shaderc tool for the
// target platform. For Android Vulkan, shaders are compiled to SPIRV.
// ---------------------------------------------------------------------------

class ShaderProcessor
{
public:
    ShaderProcessor(const CliArgs& args, const TierConfig& tier);

    /// Discover shader files in the input directory.
    std::vector<AssetEntry> discover();

    /// Process (compile) all shader entries.
    void processAll(const std::vector<AssetEntry>& entries);

private:
    /// Determine shader type (vertex/fragment) from filename prefix.
    std::string shaderType(const std::string& filename) const;

    /// Get the output format string for the target platform.
    std::string outputFormat() const;

    CliArgs args_;
    TierConfig tier_;
};

}  // namespace engine::tools
