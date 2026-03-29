#include "engine/assets/CompressedTextureLoader.h"

#include <stdexcept>

#include "engine/assets/CpuAssetData.h"

namespace engine::assets
{

namespace
{
constexpr std::string_view kExtensions[] = {".ktx", ".dds"};
}

std::span<const std::string_view> CompressedTextureLoader::extensions() const
{
    return kExtensions;
}

CpuAssetData CompressedTextureLoader::decode(std::span<const uint8_t> bytes, std::string_view path,
                                             IFileSystem& /*fs*/)
{
    if (bytes.empty())
    {
        throw std::runtime_error("CompressedTextureLoader: empty data for '" + std::string(path) +
                                 "'");
    }

    CpuCompressedTextureData result;
    result.rawBytes.assign(bytes.begin(), bytes.end());
    return result;
}

}  // namespace engine::assets
