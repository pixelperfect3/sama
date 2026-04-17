#include "tools/asset_tool/AssetProcessor.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "engine/io/Json.h"
#include "tools/asset_tool/ShaderProcessor.h"
#include "tools/asset_tool/TextureProcessor.h"

namespace fs = std::filesystem;

namespace engine::tools
{

// ---------------------------------------------------------------------------
// TierConfig lookup
// ---------------------------------------------------------------------------

TierConfig getTierConfig(const std::string& tierName)
{
    if (tierName == "low")
    {
        return {"low", 512, "8x8", true};
    }
    else if (tierName == "high")
    {
        return {"high", 2048, "4x4", true};
    }
    // Default to mid
    return {"mid", 1024, "6x6", true};
}

// ---------------------------------------------------------------------------
// CLI argument parsing
// ---------------------------------------------------------------------------

CliArgs parseArgs(int argc, char* argv[])
{
    CliArgs args;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            args.help = true;
            return args;
        }
        else if (arg == "--verbose" || arg == "-v")
        {
            args.verbose = true;
        }
        else if (arg == "--dry-run")
        {
            args.dryRun = true;
        }
        else if (arg == "--input" && i + 1 < argc)
        {
            args.inputDir = argv[++i];
        }
        else if (arg == "--output" && i + 1 < argc)
        {
            args.outputDir = argv[++i];
        }
        else if (arg == "--target" && i + 1 < argc)
        {
            args.target = argv[++i];
        }
        else if (arg == "--tier" && i + 1 < argc)
        {
            args.tier = argv[++i];
        }
        else
        {
            args.valid = false;
            args.errorMessage = "Unknown argument: " + arg;
            return args;
        }
    }

    if (args.inputDir.empty())
    {
        args.valid = false;
        args.errorMessage = "Missing required argument: --input <dir>";
        return args;
    }
    if (args.outputDir.empty())
    {
        args.valid = false;
        args.errorMessage = "Missing required argument: --output <dir>";
        return args;
    }

    // Validate tier
    if (args.tier != "low" && args.tier != "mid" && args.tier != "high")
    {
        args.valid = false;
        args.errorMessage = "Invalid tier: " + args.tier + " (expected low, mid, or high)";
        return args;
    }

    // Validate target
    if (args.target != "android" && args.target != "ios" && args.target != "desktop")
    {
        args.valid = false;
        args.errorMessage =
            "Invalid target: " + args.target + " (expected android, ios, or desktop)";
        return args;
    }

    return args;
}

// ---------------------------------------------------------------------------
// AssetProcessor
// ---------------------------------------------------------------------------

AssetProcessor::AssetProcessor(const CliArgs& args) : args_(args), tier_(getTierConfig(args.tier))
{
}

int AssetProcessor::run()
{
    if (!fs::exists(args_.inputDir))
    {
        std::cerr << "Error: input directory does not exist: " << args_.inputDir << "\n";
        return 1;
    }

    if (!ensureOutputDir())
    {
        return 1;
    }

    if (args_.verbose)
    {
        std::cout << "Asset processing:\n"
                  << "  input:  " << args_.inputDir << "\n"
                  << "  output: " << args_.outputDir << "\n"
                  << "  target: " << args_.target << "\n"
                  << "  tier:   " << tier_.name << " (maxTex=" << tier_.maxTextureSize
                  << ", astc=" << tier_.astcBlockSize << ")\n";
    }

    discoverTextures();
    discoverShaders();
    discoverModels();

    if (args_.verbose)
    {
        std::cout << "Found " << entries_.size() << " asset(s)\n";
    }

    if (!args_.dryRun)
    {
        // Copy/process assets
        TextureProcessor texProc(args_, tier_);
        texProc.processAll(entries_);

        ShaderProcessor shaderProc(args_, tier_);
        shaderProc.processAll(entries_);

        // Copy models as-is
        for (const auto& entry : entries_)
        {
            if (entry.type == "model")
            {
                fs::path src = fs::path(args_.inputDir) / entry.source;
                fs::path dst = fs::path(args_.outputDir) / entry.output;

                // Verify destination is inside outputDir (prevent path traversal)
                fs::path canonicalDst = fs::weakly_canonical(dst);
                fs::path canonicalOut = fs::weakly_canonical(args_.outputDir);
                auto mismatch =
                    std::mismatch(canonicalOut.begin(), canonicalOut.end(), canonicalDst.begin());
                if (mismatch.first != canonicalOut.end())
                {
                    std::cerr << "Error: output path escapes output directory: " << dst << "\n";
                    continue;
                }

                fs::create_directories(dst.parent_path());

                std::error_code ec;
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    std::cerr << "Warning: failed to copy model " << entry.source << ": "
                              << ec.message() << "\n";
                }
                else if (args_.verbose)
                {
                    std::cout << "  Copied model: " << entry.source << "\n";
                }
            }
        }

        if (!writeManifest())
        {
            return 1;
        }
    }
    else
    {
        std::cout << "Dry run — would process " << entries_.size() << " asset(s):\n";
        for (const auto& entry : entries_)
        {
            std::cout << "  [" << entry.type << "] " << entry.source << " -> " << entry.output
                      << " (" << entry.format << ")\n";
        }
    }

    return 0;
}

void AssetProcessor::discoverTextures()
{
    TextureProcessor texProc(args_, tier_);
    auto texEntries = texProc.discover();
    entries_.insert(entries_.end(), texEntries.begin(), texEntries.end());
}

void AssetProcessor::discoverShaders()
{
    ShaderProcessor shaderProc(args_, tier_);
    auto shaderEntries = shaderProc.discover();
    entries_.insert(entries_.end(), shaderEntries.begin(), shaderEntries.end());
}

void AssetProcessor::discoverModels()
{
    static const std::vector<std::string> modelExtensions = {".glb", ".gltf"};

    std::error_code ec;
    for (auto& p : fs::recursive_directory_iterator(args_.inputDir, ec))
    {
        if (ec)
            break;
        if (!p.is_regular_file())
            continue;

        std::string ext = p.path().extension().string();
        // Convert extension to lowercase for comparison
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        bool isModel = false;
        for (const auto& me : modelExtensions)
        {
            if (ext == me)
            {
                isModel = true;
                break;
            }
        }
        if (!isModel)
            continue;

        std::string relPath = fs::relative(p.path(), args_.inputDir).string();

        AssetEntry entry;
        entry.type = "model";
        entry.source = relPath;
        entry.output = relPath;  // models copied as-is
        entry.format = (ext == ".glb") ? "glb" : "gltf";

        if (args_.verbose)
        {
            std::cout << "  Found model: " << relPath << "\n";
        }

        entries_.push_back(std::move(entry));
    }
}

bool AssetProcessor::ensureOutputDir()
{
    if (args_.dryRun)
        return true;

    std::error_code ec;
    fs::create_directories(args_.outputDir, ec);
    if (ec)
    {
        std::cerr << "Error: cannot create output directory: " << args_.outputDir << " ("
                  << ec.message() << ")\n";
        return false;
    }
    return true;
}

bool AssetProcessor::writeManifest()
{
    io::JsonWriter writer(true);

    writer.startObject();

    writer.key("platform");
    writer.writeString(args_.target.c_str());

    writer.key("tier");
    writer.writeString(tier_.name.c_str());

    // Generate ISO 8601 timestamp
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &timeT);
#else
    gmtime_r(&timeT, &tm);
#endif
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &tm);

    writer.key("timestamp");
    writer.writeString(timeBuf);

    writer.key("assets");
    writer.startArray();

    for (const auto& entry : entries_)
    {
        writer.startObject();

        writer.key("type");
        writer.writeString(entry.type.c_str());

        writer.key("source");
        writer.writeString(entry.source.c_str());

        writer.key("output");
        writer.writeString(entry.output.c_str());

        writer.key("format");
        writer.writeString(entry.format.c_str());

        if (entry.type == "texture")
        {
            writer.key("width");
            writer.writeInt(entry.width);

            writer.key("height");
            writer.writeInt(entry.height);

            if (entry.originalWidth > 0)
            {
                writer.key("originalWidth");
                writer.writeInt(entry.originalWidth);
            }
            if (entry.originalHeight > 0)
            {
                writer.key("originalHeight");
                writer.writeInt(entry.originalHeight);
            }
        }

        writer.endObject();
    }

    writer.endArray();
    writer.endObject();

    fs::path manifestPath = fs::path(args_.outputDir) / "manifest.json";
    if (!writer.writeToFile(manifestPath.string().c_str()))
    {
        std::cerr << "Error: failed to write manifest: " << manifestPath << "\n";
        return false;
    }

    if (args_.verbose)
    {
        std::cout << "Wrote manifest: " << manifestPath << "\n";
    }

    return true;
}

}  // namespace engine::tools
