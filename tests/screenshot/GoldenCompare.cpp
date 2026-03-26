// STB_IMAGE_IMPLEMENTATION is provided by engine_assets (TextureLoader.cpp).
// STB_IMAGE_WRITE_IMPLEMENTATION is defined here — only needed in this file.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "GoldenCompare.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::screenshot
{

bool gUpdateGoldens = false;

std::string goldenPath(const std::string& name)
{
    return std::string(ENGINE_SOURCE_DIR) + "/tests/golden/" + name + ".png";
}

bool compareOrUpdateGolden(const std::string& name, const std::vector<uint8_t>& pixels,
                           uint16_t width, uint16_t height, uint8_t tolerance)
{
    const std::string path = goldenPath(name);

    // Save or overwrite when requested / golden missing
    const bool goldenExists = std::filesystem::exists(path);

    if (!goldenExists || gUpdateGoldens)
    {
        // Ensure the directory exists
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());

        const int result =
            stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                           pixels.data(), static_cast<int>(width) * 4);
        if (result == 0)
        {
            fprintf(stderr, "[golden] Failed to write golden PNG: %s\n", path.c_str());
            return false;
        }
        if (!goldenExists)
            fprintf(stderr, "[golden] Created new golden: %s\n", path.c_str());
        else
            fprintf(stderr, "[golden] Updated golden: %s\n", path.c_str());

        return true;
    }

    // Load stored golden
    int gw = 0;
    int gh = 0;
    int gc = 0;
    uint8_t* golden = stbi_load(path.c_str(), &gw, &gh, &gc, 4);
    if (!golden)
    {
        fprintf(stderr, "[golden] Failed to load golden PNG: %s\n", path.c_str());
        return false;
    }

    if (gw != static_cast<int>(width) || gh != static_cast<int>(height))
    {
        fprintf(stderr, "[golden] Resolution mismatch for '%s': golden=%dx%d current=%dx%d\n",
                name.c_str(), gw, gh, static_cast<int>(width), static_cast<int>(height));
        stbi_image_free(golden);
        return false;
    }

    // Compare pixel by pixel
    const uint32_t total = static_cast<uint32_t>(width) * height;
    uint32_t failPixels = 0;

    for (uint32_t i = 0; i < total; ++i)
    {
        const uint8_t* cur = pixels.data() + i * 4;
        const uint8_t* ref = golden + i * 4;

        const int dr = std::abs(static_cast<int>(cur[0]) - static_cast<int>(ref[0]));
        const int dg = std::abs(static_cast<int>(cur[1]) - static_cast<int>(ref[1]));
        const int db = std::abs(static_cast<int>(cur[2]) - static_cast<int>(ref[2]));

        const int maxDelta = std::max({dr, dg, db});
        if (maxDelta > static_cast<int>(tolerance))
            ++failPixels;
    }

    stbi_image_free(golden);

    // Allow up to 1% of pixels to differ
    const uint32_t maxAllowed = total / 100u;
    if (failPixels > maxAllowed)
    {
        fprintf(stderr, "[golden] FAIL '%s': %u/%u pixels differ by more than %u (%.2f%%)\n",
                name.c_str(), failPixels, total, static_cast<unsigned>(tolerance),
                100.0 * failPixels / total);
        return false;
    }

    return true;
}

}  // namespace engine::screenshot
