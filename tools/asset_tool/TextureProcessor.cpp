#include "tools/asset_tool/TextureProcessor.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "tools/asset_tool/AstcEncoder.h"

// stb_image — compiled here with STATIC to avoid symbol collisions with
// engine_assets/TextureLoader.cpp which also defines STB_IMAGE_IMPLEMENTATION.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

namespace fs = std::filesystem;

namespace engine::tools
{

// ---------------------------------------------------------------------------
// KTX1 constants
// ---------------------------------------------------------------------------

static const uint8_t kKtxIdentifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31,
                                           0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
static constexpr uint32_t kKtxEndianness = 0x04030201;
static constexpr uint32_t kGlRgba = 0x1908;

// GL_COMPRESSED_RGBA_ASTC_*_KHR constants
static constexpr uint32_t kAstcFormat4x4 = 0x93B0;
static constexpr uint32_t kAstcFormat5x4 = 0x93B1;
static constexpr uint32_t kAstcFormat5x5 = 0x93B2;
static constexpr uint32_t kAstcFormat6x5 = 0x93B3;
static constexpr uint32_t kAstcFormat6x6 = 0x93B4;
static constexpr uint32_t kAstcFormat8x5 = 0x93B5;
static constexpr uint32_t kAstcFormat8x6 = 0x93B6;
static constexpr uint32_t kAstcFormat8x8 = 0x93B7;
static constexpr uint32_t kAstcFormat10x5 = 0x93B8;
static constexpr uint32_t kAstcFormat10x6 = 0x93B9;
static constexpr uint32_t kAstcFormat10x8 = 0x93BA;
static constexpr uint32_t kAstcFormat10x10 = 0x93BB;
static constexpr uint32_t kAstcFormat12x10 = 0x93BC;
static constexpr uint32_t kAstcFormat12x12 = 0x93BD;

// ---------------------------------------------------------------------------
// TextureProcessor
// ---------------------------------------------------------------------------

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
    return ".ktx";
}

bool TextureProcessor::parseBlockSize(const std::string& blockSize, int& blockX, int& blockY)
{
    auto pos = blockSize.find('x');
    if (pos == std::string::npos || pos == 0 || pos == blockSize.size() - 1)
        return false;

    try
    {
        blockX = std::stoi(blockSize.substr(0, pos));
        blockY = std::stoi(blockSize.substr(pos + 1));
    }
    catch (...)
    {
        return false;
    }

    return blockX >= 4 && blockX <= 12 && blockY >= 4 && blockY <= 12;
}

int TextureProcessor::astcBlockCount(int pixels, int blockDim)
{
    return (pixels + blockDim - 1) / blockDim;
}

uint32_t TextureProcessor::astcGlInternalFormat(int blockX, int blockY)
{
    if (blockX == 4 && blockY == 4)
        return kAstcFormat4x4;
    if (blockX == 5 && blockY == 4)
        return kAstcFormat5x4;
    if (blockX == 5 && blockY == 5)
        return kAstcFormat5x5;
    if (blockX == 6 && blockY == 5)
        return kAstcFormat6x5;
    if (blockX == 6 && blockY == 6)
        return kAstcFormat6x6;
    if (blockX == 8 && blockY == 5)
        return kAstcFormat8x5;
    if (blockX == 8 && blockY == 6)
        return kAstcFormat8x6;
    if (blockX == 8 && blockY == 8)
        return kAstcFormat8x8;
    if (blockX == 10 && blockY == 5)
        return kAstcFormat10x5;
    if (blockX == 10 && blockY == 6)
        return kAstcFormat10x6;
    if (blockX == 10 && blockY == 8)
        return kAstcFormat10x8;
    if (blockX == 10 && blockY == 10)
        return kAstcFormat10x10;
    if (blockX == 12 && blockY == 10)
        return kAstcFormat12x10;
    if (blockX == 12 && blockY == 12)
        return kAstcFormat12x12;

    return kAstcFormat6x6;
}

std::vector<AssetEntry> TextureProcessor::discover()
{
    std::vector<AssetEntry> entries;

    if (!fs::exists(args_.inputDir))
        return entries;

    std::error_code ec;
    for (auto& p : fs::recursive_directory_iterator(args_.inputDir, ec))
    {
        if (ec)
            break;
        if (!p.is_regular_file())
            continue;

        std::string ext = p.path().extension().string();
        if (!isTextureFile(ext))
            continue;

        std::string relPath = fs::relative(p.path(), args_.inputDir).string();

        fs::path outPath = fs::path(relPath);
        outPath.replace_extension(outputExtension());

        AssetEntry entry;
        entry.type = "texture";
        entry.source = relPath;
        entry.output = outPath.string();
        entry.format = "astc_" + tier_.astcBlockSize;

        // Read actual image dimensions using stbi_info
        std::string srcPath = (fs::path(args_.inputDir) / relPath).string();
        int w = 0, h = 0, channels = 0;
        if (stbi_info(srcPath.c_str(), &w, &h, &channels))
        {
            entry.originalWidth = w;
            entry.originalHeight = h;

            if (w > tier_.maxTextureSize || h > tier_.maxTextureSize)
            {
                float scale =
                    static_cast<float>(tier_.maxTextureSize) / static_cast<float>(std::max(w, h));
                entry.width = static_cast<int>(static_cast<float>(w) * scale);
                entry.height = static_cast<int>(static_cast<float>(h) * scale);
            }
            else
            {
                entry.width = w;
                entry.height = h;
            }
        }
        else
        {
            entry.width = tier_.maxTextureSize;
            entry.height = tier_.maxTextureSize;
            entry.originalWidth = 0;
            entry.originalHeight = 0;
        }

        if (args_.verbose)
        {
            std::cout << "  Found texture: " << relPath << " (" << entry.originalWidth << "x"
                      << entry.originalHeight << " -> " << entry.width << "x" << entry.height
                      << ")\n";
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

        if (!processOne(entry))
        {
            std::cerr << "Warning: failed to process texture " << entry.source << "\n";
        }
    }
}

bool TextureProcessor::processOne(const AssetEntry& entry)
{
    fs::path srcPath = fs::path(args_.inputDir) / entry.source;
    fs::path dstPath = fs::path(args_.outputDir) / entry.output;

    fs::create_directories(dstPath.parent_path());

    // 1. Load source image as RGBA
    int srcWidth = 0, srcHeight = 0, srcChannels = 0;
    uint8_t* pixels = stbi_load(srcPath.string().c_str(), &srcWidth, &srcHeight, &srcChannels, 4);
    if (!pixels)
    {
        std::cerr << "  Failed to load image: " << srcPath << " (" << stbi_failure_reason()
                  << ")\n";
        // Fallback: copy the file as-is
        std::error_code ec;
        fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
        return !static_cast<bool>(ec);
    }

    // 2. Downscale if needed
    int outWidth = srcWidth;
    int outHeight = srcHeight;
    uint8_t* resizedPixels = nullptr;

    if (srcWidth > tier_.maxTextureSize || srcHeight > tier_.maxTextureSize)
    {
        float scale = static_cast<float>(tier_.maxTextureSize) /
                      static_cast<float>(std::max(srcWidth, srcHeight));
        outWidth = std::max(1, static_cast<int>(static_cast<float>(srcWidth) * scale));
        outHeight = std::max(1, static_cast<int>(static_cast<float>(srcHeight) * scale));

        resizedPixels = new uint8_t[outWidth * outHeight * 4];
        stbir_resize_uint8_linear(pixels, srcWidth, srcHeight, srcWidth * 4, resizedPixels,
                                  outWidth, outHeight, outWidth * 4, STBIR_RGBA);

        if (args_.verbose)
        {
            std::cout << "  Downscaled " << entry.source << " from " << srcWidth << "x" << srcHeight
                      << " to " << outWidth << "x" << outHeight << "\n";
        }
    }

    const uint8_t* finalPixels = resizedPixels ? resizedPixels : pixels;

    // 3. Parse block size
    int blockX = 6, blockY = 6;
    if (!parseBlockSize(tier_.astcBlockSize, blockX, blockY))
    {
        std::cerr << "  Invalid ASTC block size: " << tier_.astcBlockSize << ", using 6x6\n";
        blockX = 6;
        blockY = 6;
    }

    // 4. Try ASTC compression
    std::vector<uint8_t> compressedData;
    bool compressed =
        compressAstc(finalPixels, outWidth, outHeight, blockX, blockY, compressedData);

    bool ok = false;
    if (compressed)
    {
        // 5. Write KTX1 file
        ok = writeKtx(dstPath, outWidth, outHeight, blockX, blockY, compressedData.data(),
                      compressedData.size());

        if (ok && args_.verbose)
        {
            std::cout << "  Compressed texture: " << entry.source << " -> " << entry.output
                      << " (ASTC " << tier_.astcBlockSize << ", " << compressedData.size()
                      << " bytes)\n";
        }
    }
    else
    {
        // Fallback: copy source file as-is
        std::error_code ec;
        fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
        ok = !static_cast<bool>(ec);

        if (ok && args_.verbose)
        {
            std::cout << "  Copied texture (ASTC encoder unavailable): " << entry.source << "\n";
        }
    }

    stbi_image_free(pixels);
    delete[] resizedPixels;
    return ok;
}

bool TextureProcessor::writeKtx(const fs::path& path, int width, int height, int blockX, int blockY,
                                const uint8_t* data, size_t dataSize)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        std::cerr << "  Failed to open for writing: " << path << "\n";
        return false;
    }

    uint32_t glInternalFormat = astcGlInternalFormat(blockX, blockY);

    struct KtxHeader
    {
        uint8_t identifier[12];
        uint32_t endianness;
        uint32_t glType;
        uint32_t glTypeSize;
        uint32_t glFormat;
        uint32_t glInternalFormat;
        uint32_t glBaseInternalFormat;
        uint32_t pixelWidth;
        uint32_t pixelHeight;
        uint32_t pixelDepth;
        uint32_t numberOfArrayElements;
        uint32_t numberOfFaces;
        uint32_t numberOfMipmapLevels;
        uint32_t bytesOfKeyValueData;
    };

    KtxHeader header{};
    std::memcpy(header.identifier, kKtxIdentifier, 12);
    header.endianness = kKtxEndianness;
    header.glType = 0;
    header.glTypeSize = 1;
    header.glFormat = 0;
    header.glInternalFormat = glInternalFormat;
    header.glBaseInternalFormat = kGlRgba;
    header.pixelWidth = static_cast<uint32_t>(width);
    header.pixelHeight = static_cast<uint32_t>(height);
    header.pixelDepth = 0;
    header.numberOfArrayElements = 0;
    header.numberOfFaces = 1;
    header.numberOfMipmapLevels = 1;
    header.bytesOfKeyValueData = 0;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    uint32_t imageSize = static_cast<uint32_t>(dataSize);
    file.write(reinterpret_cast<const char*>(&imageSize), sizeof(imageSize));

    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(dataSize));

    // Pad to 4-byte alignment
    size_t padding = (4 - (dataSize % 4)) % 4;
    if (padding > 0)
    {
        uint8_t zeros[4] = {0, 0, 0, 0};
        file.write(reinterpret_cast<const char*>(zeros), static_cast<std::streamsize>(padding));
    }

    file.close();
    return !file.fail();
}

}  // namespace engine::tools
