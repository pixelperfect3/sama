#include "engine/rendering/IblResources.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "engine/math/Types.h"

using engine::math::Vec2;
using engine::math::Vec3;

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// CPU math helpers — Hammersley / importance sampling / geometry term.
// These run only during generateDefault() to fill the BRDF LUT.
// ---------------------------------------------------------------------------

static Vec2 hammersley(uint32_t i, uint32_t N)
{
    // Van der Corput radical inverse in base 2.
    uint32_t bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float ri = float(bits) * 2.3283064365386963e-10f;
    return Vec2(float(i) / float(N), ri);
}

static Vec3 importanceSampleGGX(Vec2 xi, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0f * glm::pi<float>() * xi.x;
    float cosTheta = glm::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float sinTheta = glm::sqrt(1.0f - cosTheta * cosTheta);
    return Vec3(glm::cos(phi) * sinTheta, glm::sin(phi) * sinTheta, cosTheta);
}

static float geometrySmithIBL(float NdotV, float NdotL, float roughness)
{
    // IBL geometry term uses k = r²/2 (not the direct-lighting (r+1)²/8).
    float k = (roughness * roughness) / 2.0f;
    auto G1 = [k](float NdotX)
    { return NdotX / (NdotX * (1.0f - k) + k); };
    return G1(NdotV) * G1(NdotL);
}

// ---------------------------------------------------------------------------
// Cubemap UV → direction helpers
// ---------------------------------------------------------------------------

// faceIndex: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
// u, v in [0, 1]
static Vec3 cubeUvToDir(uint32_t faceIndex, float u, float v)
{
    // Remap [0,1] → [-1,1]
    float s = u * 2.0f - 1.0f;
    float t = v * 2.0f - 1.0f;

    switch (faceIndex)
    {
        case 0:
            return glm::normalize(Vec3(1.0f, -t, -s));  // +X
        case 1:
            return glm::normalize(Vec3(-1.0f, -t, s));  // -X
        case 2:
            return glm::normalize(Vec3(s, 1.0f, t));    // +Y
        case 3:
            return glm::normalize(Vec3(s, -1.0f, -t));  // -Y
        case 4:
            return glm::normalize(Vec3(s, -t, 1.0f));   // +Z
        default:
            return glm::normalize(Vec3(-s, -t, -1.0f));  // -Z
    }
}

// ---------------------------------------------------------------------------
// IblResources::upload
// ---------------------------------------------------------------------------

bool IblResources::upload(const assets::EnvironmentAsset& env)
{
    shutdown();

    // --- Irradiance cubemap ---
    {
        uint32_t sz = env.irradianceSize;
        uint32_t faceBytes = sz * sz * 4 * sizeof(float);
        uint32_t totalBytes = faceBytes * 6;
        const bgfx::Memory* mem = bgfx::alloc(totalBytes);

        auto* dst = reinterpret_cast<float*>(mem->data);
        for (uint32_t f = 0; f < 6; ++f)
        {
            const auto& face = env.irradianceFaces[f];
            std::memcpy(dst + f * sz * sz * 4, face.data(), faceBytes);
        }

        irradiance_ = bgfx::createTextureCube(
            static_cast<uint16_t>(sz), false, 1, bgfx::TextureFormat::RGBA32F, 0, mem);
    }

    // --- Prefiltered specular cubemap ---
    {
        uint32_t sz = env.prefilteredSize;
        uint8_t mips = env.prefilteredMips;

        // Compute total byte count across all mips and faces.
        uint32_t totalBytes = 0;
        for (uint8_t m = 0; m < mips; ++m)
        {
            uint32_t mipSz = sz >> m;
            if (mipSz < 1)
                mipSz = 1;
            totalBytes += mipSz * mipSz * 4 * sizeof(uint16_t) * 6;
        }

        const bgfx::Memory* mem = bgfx::alloc(totalBytes);
        auto* dst = reinterpret_cast<uint16_t*>(mem->data);
        uint32_t offset = 0;

        for (uint8_t m = 0; m < mips; ++m)
        {
            uint32_t mipSz = sz >> m;
            if (mipSz < 1)
                mipSz = 1;
            uint32_t pixelCount = mipSz * mipSz;

            for (uint32_t f = 0; f < 6; ++f)
            {
                const auto& face = env.prefilteredFaces[f][m];
                // Convert float → half (bgfx::packHalf — use a simple bit cast
                // approximation via bx half conversion isn't available without
                // bx headers, so we store float-encoded halfs.
                // For upload purposes: convert float to f16 manually.
                for (uint32_t p = 0; p < pixelCount * 4; ++p)
                {
                    // Simple float-to-half via standard bit manipulation.
                    float val = face[p];
                    uint32_t f32 = 0;
                    std::memcpy(&f32, &val, 4);
                    uint16_t sign = static_cast<uint16_t>((f32 >> 16u) & 0x8000u);
                    uint16_t exponent = static_cast<uint16_t>(((f32 >> 23u) & 0xFFu) - 112u);
                    uint16_t mantissa = static_cast<uint16_t>((f32 >> 13u) & 0x3FFu);
                    if (exponent <= 0)
                        exponent = 0, mantissa = 0;
                    else if (exponent > 30)
                        exponent = 30, mantissa = 0x3FFu;
                    dst[offset++] = sign | static_cast<uint16_t>(exponent << 10u) | mantissa;
                }
            }
        }

        prefiltered_ = bgfx::createTextureCube(
            static_cast<uint16_t>(sz), true, 1, bgfx::TextureFormat::RGBA16F, 0, mem);
    }

    // --- BRDF LUT ---
    {
        uint32_t sz = env.brdfLutSize;
        uint32_t bytes = sz * sz * 2 * sizeof(float);
        const bgfx::Memory* mem = bgfx::alloc(bytes);
        std::memcpy(mem->data, env.brdfLutData.data(), bytes);

        brdfLut_ = bgfx::createTexture2D(
            static_cast<uint16_t>(sz),
            static_cast<uint16_t>(sz),
            false,
            1,
            bgfx::TextureFormat::RG32F,
            0,
            mem);
    }

    return bgfx::isValid(brdfLut_);
}

// ---------------------------------------------------------------------------
// IblResources::generateDefault
// ---------------------------------------------------------------------------

bool IblResources::generateDefault()
{
    shutdown();

    // --- Irradiance cubemap (32×32×6, RGBA32F) ---
    // Sky/ground hemisphere gradient per-face.
    {
        constexpr uint32_t sz = 32;
        constexpr Vec3 skyColor{0.5f, 0.7f, 1.0f};
        constexpr Vec3 groundColor{0.1f, 0.08f, 0.05f};

        constexpr uint32_t facePixels = sz * sz * 4;
        constexpr uint32_t totalBytes = facePixels * 6 * sizeof(float);

        const bgfx::Memory* mem = bgfx::alloc(totalBytes);
        auto* dst = reinterpret_cast<float*>(mem->data);

        for (uint32_t f = 0; f < 6; ++f)
        {
            for (uint32_t y = 0; y < sz; ++y)
            {
                for (uint32_t x = 0; x < sz; ++x)
                {
                    float u = (float(x) + 0.5f) / float(sz);
                    float v = (float(y) + 0.5f) / float(sz);
                    Vec3 dir = cubeUvToDir(f, u, v);
                    float t = glm::max(dir.y, 0.0f);
                    Vec3 color = glm::mix(groundColor, skyColor, t);

                    uint32_t base = (f * sz * sz + y * sz + x) * 4;
                    dst[base + 0] = color.x;
                    dst[base + 1] = color.y;
                    dst[base + 2] = color.z;
                    dst[base + 3] = 1.0f;
                }
            }
        }

        irradiance_ = bgfx::createTextureCube(
            static_cast<uint16_t>(sz), false, 1, bgfx::TextureFormat::RGBA32F, 0, mem);
    }

    // --- Prefiltered specular cubemap (64×64×6, RGBA16F, 7 mips) ---
    // All mips: solid grey (0.5, 0.5, 0.5, 1.0).
    {
        constexpr uint32_t sz = 64;
        constexpr uint8_t mips = 7;  // log2(64)+1

        // Calculate total size: sum of mip sizes × 6 faces × 4 channels × sizeof(half).
        uint32_t totalBytes = 0;
        for (uint8_t m = 0; m < mips; ++m)
        {
            uint32_t mipSz = sz >> m;
            if (mipSz < 1)
                mipSz = 1;
            totalBytes += mipSz * mipSz * 4 * sizeof(uint16_t) * 6;
        }

        const bgfx::Memory* mem = bgfx::alloc(totalBytes);
        auto* dst = reinterpret_cast<uint16_t*>(mem->data);
        uint32_t offset = 0;

        // Half-float representation of 0.5f = 0x3800
        // Half-float representation of 1.0f = 0x3C00
        constexpr uint16_t half05 = 0x3800u;
        constexpr uint16_t half10 = 0x3C00u;

        for (uint8_t m = 0; m < mips; ++m)
        {
            uint32_t mipSz = sz >> m;
            if (mipSz < 1)
                mipSz = 1;

            for (uint32_t f = 0; f < 6; ++f)
            {
                for (uint32_t p = 0; p < mipSz * mipSz; ++p)
                {
                    dst[offset++] = half05;  // R
                    dst[offset++] = half05;  // G
                    dst[offset++] = half05;  // B
                    dst[offset++] = half10;  // A
                }
            }
        }

        prefiltered_ = bgfx::createTextureCube(
            static_cast<uint16_t>(sz), true, 1, bgfx::TextureFormat::RGBA16F, 0, mem);
    }

    // --- BRDF LUT (128×128, RG32F) ---
    // 1024-sample Hammersley GGX split-sum integration.
    {
        constexpr uint32_t size = 128;
        constexpr uint32_t numSamples = 1024;

        std::vector<float> lut(size * size * 2, 0.0f);

        for (uint32_t y = 0; y < size; ++y)
        {
            float NdotV = (float(y) + 0.5f) / float(size);
            Vec3 V{0.0f, 0.0f, NdotV};

            for (uint32_t x = 0; x < size; ++x)
            {
                float roughness = (float(x) + 0.5f) / float(size);
                float scale = 0.0f;
                float bias = 0.0f;

                for (uint32_t s = 0; s < numSamples; ++s)
                {
                    Vec2 xi = hammersley(s, numSamples);
                    Vec3 H = importanceSampleGGX(xi, roughness);
                    Vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

                    float NdotL = glm::max(L.z, 0.0f);
                    float NdotH = glm::max(H.z, 0.0f);
                    float VdotH = glm::max(glm::dot(V, H), 0.0f);

                    if (NdotL > 0.0f)
                    {
                        float G = geometrySmithIBL(NdotV, NdotL, roughness);
                        float G_Vis = (G * VdotH) / (NdotH * NdotV + 1e-6f);
                        float Fc = glm::pow(1.0f - VdotH, 5.0f);
                        scale += (1.0f - Fc) * G_Vis;
                        bias += Fc * G_Vis;
                    }
                }

                lut[y * size * 2 + x * 2 + 0] = scale / float(numSamples);
                lut[y * size * 2 + x * 2 + 1] = bias / float(numSamples);
            }
        }

        uint32_t bytes = size * size * 2 * sizeof(float);
        const bgfx::Memory* mem = bgfx::alloc(bytes);
        std::memcpy(mem->data, lut.data(), bytes);

        brdfLut_ = bgfx::createTexture2D(
            static_cast<uint16_t>(size),
            static_cast<uint16_t>(size),
            false,
            1,
            bgfx::TextureFormat::RG32F,
            0,
            mem);
    }

    return bgfx::isValid(brdfLut_);
}

// ---------------------------------------------------------------------------
// IblResources::shutdown
// ---------------------------------------------------------------------------

void IblResources::shutdown()
{
    if (bgfx::isValid(irradiance_))
    {
        bgfx::destroy(irradiance_);
        irradiance_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(prefiltered_))
    {
        bgfx::destroy(prefiltered_);
        prefiltered_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(brdfLut_))
    {
        bgfx::destroy(brdfLut_);
        brdfLut_ = BGFX_INVALID_HANDLE;
    }
}

}  // namespace engine::rendering
