#include "engine/assets/TextureLoader.h"

#include <stdexcept>

#include "engine/assets/CpuAssetData.h"

// stb_image — implementation compiled here, nowhere else in engine_assets.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

namespace engine::assets
{

namespace
{
constexpr std::string_view kExtensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
}

std::span<const std::string_view> TextureLoader::extensions() const
{
    return kExtensions;
}

CpuAssetData TextureLoader::decode(std::span<const uint8_t> bytes, std::string_view path,
                                   IFileSystem& /*fs*/)
{
    int w = 0, h = 0, channels = 0;
    // Always decode to RGBA8 (4 channels) so the upload path is uniform.
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h,
                                            &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        throw std::runtime_error("TextureLoader: failed to decode '" + std::string(path) +
                                 "': " + stbi_failure_reason());
    }

    CpuTextureData result;
    result.width = static_cast<uint32_t>(w);
    result.height = static_cast<uint32_t>(h);
    result.pixels.assign(pixels, pixels + (w * h * 4));

    stbi_image_free(pixels);
    return result;
}

}  // namespace engine::assets
