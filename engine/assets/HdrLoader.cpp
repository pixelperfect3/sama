#include "engine/assets/HdrLoader.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <string>

#include "engine/math/Types.h"
#include "engine/rendering/IblResources.h"

// stb_image — use the static-symbol variant so we don't collide with
// TextureLoader.cpp which owns STB_IMAGE_IMPLEMENTATION. We only need the
// `stbi_loadf` float-path for Radiance HDR files.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_HDR
#include <stb_image.h>

using engine::math::Vec3;

namespace engine::assets
{

std::optional<EnvironmentAsset> loadHdrEnvironment(std::string_view path)
{
    // stb_image needs a null-terminated C string.
    std::string p(path);

    int width = 0;
    int height = 0;
    int channels = 0;
    float* pixels = stbi_loadf(p.c_str(), &width, &height, &channels, 3);
    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        if (pixels != nullptr)
        {
            stbi_image_free(pixels);
        }
        return std::nullopt;
    }

    // Bilinearly sample the equirectangular projection at a unit direction.
    // Mapping: u = 0.5 + atan2(z, x) / (2π), v = 0.5 - asin(y) / π
    const int w = width;
    const int h = height;
    auto sampleEquirect = [pixels, w, h](const Vec3& dir) -> Vec3
    {
        Vec3 d = glm::normalize(dir);
        float u = 0.5f + std::atan2(d.z, d.x) / (2.0f * glm::pi<float>());
        float v = 0.5f - std::asin(glm::clamp(d.y, -1.0f, 1.0f)) / glm::pi<float>();

        // Wrap u (horizontal), clamp v (vertical).
        u = u - std::floor(u);
        v = glm::clamp(v, 0.0f, 1.0f);

        float fx = u * static_cast<float>(w) - 0.5f;
        float fy = v * static_cast<float>(h) - 0.5f;

        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        float tx = fx - static_cast<float>(x0);
        float ty = fy - static_cast<float>(y0);

        auto fetch = [pixels, w, h](int x, int y) -> Vec3
        {
            // Wrap x horizontally, clamp y vertically.
            int xi = ((x % w) + w) % w;
            int yi = std::clamp(y, 0, h - 1);
            const float* px = pixels + (yi * w + xi) * 3;
            return Vec3(px[0], px[1], px[2]);
        };

        Vec3 c00 = fetch(x0, y0);
        Vec3 c10 = fetch(x0 + 1, y0);
        Vec3 c01 = fetch(x0, y0 + 1);
        Vec3 c11 = fetch(x0 + 1, y0 + 1);

        Vec3 a = glm::mix(c00, c10, tx);
        Vec3 b = glm::mix(c01, c11, tx);
        return glm::mix(a, b, ty);
    };

    EnvironmentAsset env = rendering::IblResources::generateAssetFromSky(sampleEquirect);

    stbi_image_free(pixels);
    return env;
}

}  // namespace engine::assets
