#include "tools/asset_tool/ShaderProcessor.h"

#include <array>
#include <cstdio>
#include <cstdlib>
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
    if (filename.length() < 3)
        return "unknown";
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

std::string ShaderProcessor::platformForTarget() const
{
    if (args_.target == "android")
        return "android";
    if (args_.target == "ios")
        return "ios";
    return "linux";  // desktop default
}

std::string ShaderProcessor::profileForTarget() const
{
    if (args_.target == "android")
        return "spirv";  // Vulkan
    if (args_.target == "ios")
        return "metal";
    return "440";  // OpenGL 4.4 for desktop
}

std::string ShaderProcessor::findShaderc() const
{
    // Check common locations relative to the project root.
    // The bgfx.cmake build places shaderc in the bgfx_cmake-build directory.
    std::vector<std::string> candidates = {
        "build/_deps/bgfx_cmake-build/shaderc",
        "build/shaderc",
        "build/Release/shaderc",
        "build/Debug/shaderc",
    };

    for (const auto& path : candidates)
    {
        if (fs::exists(path))
            return fs::canonical(path).string();
    }

    // Check PATH via "which shaderc"
    std::array<char, 256> buffer{};
    FILE* pipe = popen("which shaderc 2>/dev/null", "r");
    if (pipe)
    {
        if (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        {
            pclose(pipe);
            std::string result(buffer.data());
            // Trim trailing newline
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            if (!result.empty() && fs::exists(result))
                return result;
        }
        else
        {
            pclose(pipe);
        }
    }

    return "";
}

std::string ShaderProcessor::findVaryingDef() const
{
    std::vector<std::string> candidates = {
        (fs::path(args_.inputDir) / "shaders" / "varying.def.sc").string(),
        (fs::path(args_.inputDir) / "varying.def.sc").string(),
        "engine/shaders/varying.def.sc",
    };

    for (const auto& path : candidates)
    {
        if (fs::exists(path))
            return fs::canonical(path).string();
    }
    return "";
}

std::vector<std::string> ShaderProcessor::findIncludePaths() const
{
    std::vector<std::string> paths;

    // bgfx shader include directory (contains bgfx_shader.sh)
    std::vector<std::string> bgfxSrcCandidates = {
        "build/_deps/bgfx_cmake-src/bgfx/src",
    };
    for (const auto& p : bgfxSrcCandidates)
    {
        if (fs::exists(p))
        {
            paths.push_back(fs::canonical(p).string());
            break;
        }
    }

    // Engine shader directory (for local includes)
    if (fs::exists("engine/shaders"))
    {
        paths.push_back(fs::canonical("engine/shaders").string());
    }

    return paths;
}

bool ShaderProcessor::compileShader(const std::string& inputPath, const std::string& outputPath,
                                    const std::string& type, const std::string& platform,
                                    const std::string& profile)
{
    std::string shaderc = findShaderc();
    if (shaderc.empty())
    {
        std::cerr << "shaderc not found. Build the project first.\n";
        return false;
    }

    std::string varyingDef = findVaryingDef();
    if (varyingDef.empty())
    {
        std::cerr << "varying.def.sc not found.\n";
        return false;
    }

    // Build the shaderc command
    std::string cmd = shaderc + " -f " + inputPath + " -o " + outputPath + " --type " + type +
                      " --platform " + platform + " --varyingdef " + varyingDef + " -p " + profile;

    // Add include paths for bgfx_shader.sh and engine shaders
    auto includePaths = findIncludePaths();
    for (const auto& incPath : includePaths)
    {
        cmd += " -i " + incPath;
    }

    if (args_.verbose)
    {
        std::cout << "  $ " << cmd << "\n";
    }

    int result = std::system(cmd.c_str());
    return (result == 0);
}

void ShaderProcessor::copyFallback(const std::vector<AssetEntry>& entries)
{
    for (const auto& entry : entries)
    {
        if (entry.type != "shader")
            continue;

        fs::path srcPath = fs::path(args_.inputDir) / entry.source;
        fs::path dstPath = fs::path(args_.outputDir) / entry.output;

        fs::create_directories(dstPath.parent_path());

        std::error_code ec;
        fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            std::cerr << "Warning: failed to copy shader " << entry.source << ": " << ec.message()
                      << "\n";
        }
        else if (args_.verbose)
        {
            std::cout << "  Copied shader (shaderc not available): " << entry.source << "\n";
        }
    }
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

    std::error_code ec;
    for (auto& p : fs::recursive_directory_iterator(shaderDir, ec))
    {
        if (ec)
            break;
        if (!p.is_regular_file())
            continue;

        std::string ext = p.path().extension().string();
        if (ext != ".sc")
            continue;

        std::string filename = p.path().filename().string();

        // Skip varying definition files
        if (filename == "varying.def.sc")
            continue;
        if (filename == "varying_pp.def.sc")
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
    std::string shadercPath = findShaderc();

    if (shadercPath.empty())
    {
        std::cerr << "WARNING: shaderc not found, copying shader source files as-is.\n";
        std::cerr << "Build the project first to get shaderc, or install it in PATH.\n";
        copyFallback(entries);
        return;
    }

    std::string platform = platformForTarget();
    std::string profile = profileForTarget();

    for (const auto& entry : entries)
    {
        if (entry.type != "shader")
            continue;

        fs::path srcPath = fs::path(args_.inputDir) / entry.source;
        fs::path dstPath = fs::path(args_.outputDir) / entry.output;

        fs::create_directories(dstPath.parent_path());

        if (args_.dryRun)
        {
            std::cout << "  Would compile: " << entry.source << " -> " << entry.output << "\n";
            continue;
        }

        std::string type = shaderType(srcPath.filename().string());
        if (type == "unknown")
        {
            std::cerr << "  Skipping unknown shader type: " << entry.source << "\n";
            continue;
        }

        if (!compileShader(srcPath.string(), dstPath.string(), type, platform, profile))
        {
            std::cerr << "  FAILED: " << entry.source << "\n";
        }
        else if (args_.verbose)
        {
            std::cout << "  Compiled: " << entry.source << " -> " << entry.output << "\n";
        }
    }
}

}  // namespace engine::tools
