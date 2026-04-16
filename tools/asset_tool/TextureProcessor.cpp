#include "tools/asset_tool/TextureProcessor.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace engine::tools
{

TextureProcessor::TextureProcessor(const CliArgs& args, const TierConfig& tier)
    : args_(args), tier_(tier)
{
}

bool TextureProcessor::isTextureFile(const std::string& extension) const
{
    static const std::vector<std::string> textureExtensions = {".png", ".jpg",  ".jpeg",
                                                               ".ktx", ".ktx2", ".dds"};

    std::string ext = extension;
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const auto& te : textureExtensions)
    {
        if (ext == te)
            return true;
    }
    return false;
}

std::string TextureProcessor::outputExtension() const
{
    // For Android target, output as .ktx (ASTC compressed).
    // For now, we just copy with original extension since ASTC encoding
    // is not yet implemented.
    return ".ktx";
}

std::vector<AssetEntry> TextureProcessor::discover()
{
    std::vector<AssetEntry> entries;

    if (!fs::exists(args_.inputDir))
        return entries;

    for (auto& p : fs::recursive_directory_iterator(args_.inputDir))
    {
        if (!p.is_regular_file())
            continue;

        std::string ext = p.path().extension().string();
        if (!isTextureFile(ext))
            continue;

        std::string relPath = fs::relative(p.path(), args_.inputDir).string();

        // Build output path: replace extension with .ktx for ASTC
        fs::path outPath = fs::path(relPath);
        outPath.replace_extension(outputExtension());

        AssetEntry entry;
        entry.type = "texture";
        entry.source = relPath;
        entry.output = outPath.string();
        entry.format = "astc_" + tier_.astcBlockSize;

        // TODO: read actual image dimensions using stb_image or similar.
        // For now, set dimensions to the tier's max as a placeholder.
        // Actual dimensions would be read, then downscaled if exceeding
        // the tier limit.
        entry.width = tier_.maxTextureSize;
        entry.height = tier_.maxTextureSize;
        entry.originalWidth = 0;
        entry.originalHeight = 0;

        if (args_.verbose)
        {
            std::cout << "  Found texture: " << relPath << "\n";
        }

        entries.push_back(std::move(entry));
    }

    return entries;
}

void TextureProcessor::processAll(const std::vector<AssetEntry>& entries)
{
    for (const auto& entry : entries)
    {
        if (entry.type != "texture")
            continue;

        fs::path srcPath = fs::path(args_.inputDir) / entry.source;
        fs::path dstPath = fs::path(args_.outputDir) / entry.output;

        fs::create_directories(dstPath.parent_path());

        // TODO: ASTC compression via astcenc CLI.
        // The third_party/astc-codec library is decode-only, so actual
        // encoding requires the astcenc command-line tool. For now, we
        // copy the source texture as-is.
        //
        // Future implementation would:
        // 1. Read image with stb_image
        // 2. Downscale if dimensions exceed tier_.maxTextureSize
        // 3. Invoke: astcenc -cl input.png output.astc <blockSize> -medium
        // 4. Wrap the .astc data in a KTX container

        // Copy source file to output (preserving data for now)
        fs::path actualDst = fs::path(args_.outputDir) / entry.source;
        fs::create_directories(actualDst.parent_path());

        std::error_code ec;
        fs::copy_file(srcPath, actualDst, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            std::cerr << "Warning: failed to copy texture " << entry.source << ": " << ec.message()
                      << "\n";
        }
        else if (args_.verbose)
        {
            std::cout << "  Copied texture (ASTC encoding TODO): " << entry.source << "\n";
        }
    }
}

}  // namespace engine::tools
