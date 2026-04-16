#include "tools/asset_tool/ShaderProcessor.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace engine::tools
{

ShaderProcessor::ShaderProcessor(const CliArgs& args, const TierConfig& tier)
    : args_(args), tier_(tier)
{
}

std::string ShaderProcessor::shaderType(const std::string& filename) const
{
    if (filename.substr(0, 3) == "vs_")
        return "vertex";
    if (filename.substr(0, 3) == "fs_")
        return "fragment";
    if (filename.substr(0, 3) == "cs_")
        return "compute";
    return "unknown";
}

std::string ShaderProcessor::outputFormat() const
{
    if (args_.target == "android")
        return "spirv";
    if (args_.target == "ios")
        return "metal";
    return "spirv";  // desktop default
}

std::vector<AssetEntry> ShaderProcessor::discover()
{
    std::vector<AssetEntry> entries;

    if (!fs::exists(args_.inputDir))
        return entries;

    // Look for .sc shader files in a shaders/ subdirectory
    fs::path shaderDir = fs::path(args_.inputDir) / "shaders";
    if (!fs::exists(shaderDir))
    {
        // Also try the input directory itself
        shaderDir = args_.inputDir;
    }

    for (auto& p : fs::recursive_directory_iterator(shaderDir))
    {
        if (!p.is_regular_file())
            continue;

        std::string ext = p.path().extension().string();
        if (ext != ".sc")
            continue;

        std::string filename = p.path().filename().string();

        // Skip varying definition files
        if (filename.find("varying") != std::string::npos)
            continue;

        std::string type = shaderType(filename);
        if (type == "unknown")
            continue;

        std::string relPath = fs::relative(p.path(), args_.inputDir).string();

        // Output path: replace .sc with .bin
        fs::path outPath = fs::path(relPath);
        outPath.replace_extension(".bin");

        AssetEntry entry;
        entry.type = "shader";
        entry.source = relPath;
        entry.output = outPath.string();
        entry.format = outputFormat();

        if (args_.verbose)
        {
            std::cout << "  Found shader (" << type << "): " << relPath << "\n";
        }

        entries.push_back(std::move(entry));
    }

    return entries;
}

void ShaderProcessor::processAll(const std::vector<AssetEntry>& entries)
{
    for (const auto& entry : entries)
    {
        if (entry.type != "shader")
            continue;

        fs::path srcPath = fs::path(args_.inputDir) / entry.source;
        fs::path dstPath = fs::path(args_.outputDir) / entry.output;

        fs::create_directories(dstPath.parent_path());

        // TODO: invoke shaderc to compile the shader.
        // The shaderc binary is built as part of the engine build.
        //
        // Future implementation:
        //   std::string cmd = "shaderc"
        //       " -f " + srcPath.string() +
        //       " -o " + dstPath.string() +
        //       " --type " + shaderType(srcPath.filename().string()) +
        //       " --platform android"
        //       " -p spirv";
        //   system(cmd.c_str());
        //
        // For now, copy the source shader file as a placeholder.

        std::error_code ec;
        fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            std::cerr << "Warning: failed to copy shader " << entry.source << ": " << ec.message()
                      << "\n";
        }
        else if (args_.verbose)
        {
            std::cout << "  Copied shader (compilation TODO): " << entry.source << "\n";
        }
    }
}

}  // namespace engine::tools
