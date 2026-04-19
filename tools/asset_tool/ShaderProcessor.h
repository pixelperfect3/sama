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
// target platform. Falls back to copying source files if shaderc is not found.
// ---------------------------------------------------------------------------

class ShaderProcessor
{
public:
    ShaderProcessor(const CliArgs& args, const TierConfig& tier);

    /// Discover shader files in the input directory.
    std::vector<AssetEntry> discover();

    /// Process (compile) all shader entries.
    void processAll(const std::vector<AssetEntry>& entries);

    /// Determine shader type (vertex/fragment/compute) from filename prefix.
    std::string shaderType(const std::string& filename) const;

    /// Map target name to shaderc --platform value.
    std::string platformForTarget() const;

    /// Map target name to shaderc -p (profile) value.
    std::string profileForTarget() const;

    /// Locate the shaderc binary (checks build tree, then PATH).
    std::string findShaderc() const;

    /// Locate varying.def.sc (checks input dir, then engine source).
    std::string findVaryingDef() const;

    /// Locate bgfx shader include directories.
    std::vector<std::string> findIncludePaths() const;

private:
    /// Compile a single shader via shaderc.
    bool compileShader(const std::string& inputPath, const std::string& outputPath,
                       const std::string& type, const std::string& platform,
                       const std::string& profile);

    /// Fall back to copying shader source files as-is.
    void copyFallback(const std::vector<AssetEntry>& entries);

    /// Get the output format string for the target platform.
    std::string outputFormat() const;

    CliArgs args_;
    TierConfig tier_;
};

}  // namespace engine::tools
