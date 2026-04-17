#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace engine::tools
{

// ---------------------------------------------------------------------------
// TierConfig — quality settings for a target device tier.
//
// NOTE: The engine has its own engine::game::TierConfig in
// engine/game/ProjectConfig.h which bundles both asset and render quality.
// This tool-side struct is intentionally separate to avoid the asset tool
// depending on the full engine, but the asset-quality fields (maxTextureSize,
// astcBlockSize) should stay in sync with the engine-side defaults returned
// by engine::game::defaultTiers().
// ---------------------------------------------------------------------------

struct TierConfig
{
    std::string name;
    int maxTextureSize = 1024;
    std::string astcBlockSize = "6x6";
    bool copyModelsAsIs = true;
};

TierConfig getTierConfig(const std::string& tierName);

// ---------------------------------------------------------------------------
// AssetEntry — one processed asset in the manifest.
// ---------------------------------------------------------------------------

struct AssetEntry
{
    std::string type;    // "texture", "shader", "model"
    std::string source;  // relative path from input dir
    std::string output;  // relative path in output dir
    std::string format;  // "astc_6x6", "spirv", "glb", etc.
    int width = 0;
    int height = 0;
    int originalWidth = 0;
    int originalHeight = 0;
};

// ---------------------------------------------------------------------------
// CliArgs — parsed command-line arguments.
// ---------------------------------------------------------------------------

struct CliArgs
{
    std::string inputDir;
    std::string outputDir;
    std::string target = "android";
    std::string tier = "mid";
    bool verbose = false;
    bool dryRun = false;
    bool help = false;
    bool valid = true;
    std::string errorMessage;
};

CliArgs parseArgs(int argc, char* argv[]);

// ---------------------------------------------------------------------------
// AssetProcessor — orchestrates asset processing for a target platform/tier.
// ---------------------------------------------------------------------------

class AssetProcessor
{
public:
    AssetProcessor(const CliArgs& args);

    /// Run the full pipeline: discover, process, write manifest.
    /// Returns 0 on success, non-zero on failure.
    int run();

    /// Access the collected asset entries (available after run()).
    const std::vector<AssetEntry>& entries() const
    {
        return entries_;
    }

private:
    void discoverTextures();
    void discoverShaders();
    void discoverModels();
    bool writeManifest();
    bool ensureOutputDir();

    CliArgs args_;
    TierConfig tier_;
    std::vector<AssetEntry> entries_;
};

}  // namespace engine::tools
