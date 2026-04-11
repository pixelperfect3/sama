#include "engine/rendering/IblResources.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

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
    auto G1 = [k](float NdotX) { return NdotX / (NdotX * (1.0f - k) + k); };
    return G1(NdotV) * G1(NdotL);
}

// ---------------------------------------------------------------------------
// Float → half-float conversion
// ---------------------------------------------------------------------------

static uint16_t floatToHalf(float val)
{
    uint32_t f32 = 0;
    std::memcpy(&f32, &val, 4);
    uint16_t sign = static_cast<uint16_t>((f32 >> 16u) & 0x8000u);
    int32_t exponent = static_cast<int32_t>(((f32 >> 23u) & 0xFFu)) - 112;
    uint16_t mantissa = static_cast<uint16_t>((f32 >> 13u) & 0x3FFu);
    if (exponent <= 0)
    {
        exponent = 0;
        mantissa = 0;
    }
    else if (exponent > 30)
    {
        exponent = 30;
        mantissa = 0x3FFu;
    }
    return sign | static_cast<uint16_t>(exponent << 10u) | mantissa;
}

// ---------------------------------------------------------------------------
// Procedural sky model — sunset-like atmosphere
// ---------------------------------------------------------------------------

static Vec3 proceduralSky(const Vec3& dir)
{
    float y = dir.y;

    // Sky colors
    Vec3 zenith{0.08f, 0.15f, 0.45f};      // deep blue at top
    Vec3 horizon{0.55f, 0.65f, 0.85f};     // lighter blue at horizon
    Vec3 sunsetWarm{0.85f, 0.45f, 0.20f};  // warm orange near horizon
    Vec3 sunsetPink{0.75f, 0.35f, 0.40f};  // pinkish tint

    // Ground colors
    Vec3 groundDark{0.04f, 0.03f, 0.02f};  // dark ground directly below
    Vec3 groundMid{0.08f, 0.06f, 0.04f};   // brown earth

    if (y >= 0.0f)
    {
        // Sky hemisphere
        float skyT = glm::clamp(y, 0.0f, 1.0f);

        // Base gradient: horizon → zenith
        Vec3 base = glm::mix(horizon, zenith, glm::pow(skyT, 0.6f));

        // Warm sunset band near horizon (exponential falloff)
        float sunsetBand = glm::exp(-skyT * 8.0f);
        base = glm::mix(base, sunsetWarm, sunsetBand * 0.5f);

        // Pink tint slightly above horizon
        float pinkBand = glm::exp(-glm::pow((skyT - 0.05f) * 10.0f, 2.0f));
        base = glm::mix(base, sunsetPink, pinkBand * 0.25f);

        return base;
    }
    else
    {
        // Ground hemisphere
        float groundT = glm::clamp(-y, 0.0f, 1.0f);
        Vec3 base = glm::mix(groundMid, groundDark, glm::pow(groundT, 0.5f));

        // Warm glow near horizon on ground side
        float horizonGlow = glm::exp(-groundT * 6.0f);
        Vec3 warmGround{0.18f, 0.12f, 0.06f};
        base = glm::mix(base, warmGround, horizonGlow * 0.6f);

        return base;
    }
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
            return glm::normalize(Vec3(s, 1.0f, t));  // +Y
        case 3:
            return glm::normalize(Vec3(s, -1.0f, -t));  // -Y
        case 4:
            return glm::normalize(Vec3(s, -t, 1.0f));  // +Z
        default:
            return glm::normalize(Vec3(-s, -t, -1.0f));  // -Z
    }
}

// ---------------------------------------------------------------------------
// Build an orthonormal basis (TBN) around N for importance sampling
// ---------------------------------------------------------------------------

static void buildTBN(const Vec3& N, Vec3& T, Vec3& B)
{
    Vec3 up = (glm::abs(N.y) < 0.999f) ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    T = glm::normalize(glm::cross(up, N));
    B = glm::cross(N, T);
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

        irradiance_ = bgfx::createTextureCube(static_cast<uint16_t>(sz), false, 1,
                                              bgfx::TextureFormat::RGBA32F, 0, mem);
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

        prefiltered_ = bgfx::createTextureCube(static_cast<uint16_t>(sz), true, 1,
                                               bgfx::TextureFormat::RGBA16F, 0, mem);
    }

    // --- BRDF LUT ---
    {
        uint32_t sz = env.brdfLutSize;
        uint32_t bytes = sz * sz * 2 * sizeof(float);
        const bgfx::Memory* mem = bgfx::alloc(bytes);
        std::memcpy(mem->data, env.brdfLutData.data(), bytes);

        brdfLut_ = bgfx::createTexture2D(static_cast<uint16_t>(sz), static_cast<uint16_t>(sz),
                                         false, 1, bgfx::TextureFormat::RG32F, 0, mem);
    }

    return bgfx::isValid(brdfLut_);
}

// ---------------------------------------------------------------------------
// IblResources::generateDefaultAsset — CPU-only computation
// ---------------------------------------------------------------------------

assets::EnvironmentAsset IblResources::generateDefaultAsset()
{
    assets::EnvironmentAsset env;

    // --- Irradiance cubemap (64×64×6, RGBA32F) ---
    // Cosine-weighted hemisphere integration of the procedural sky model.
    {
        constexpr uint32_t sz = 64;
        constexpr uint32_t irradianceSamples = 256;
        env.irradianceSize = sz;
        env.irradianceFaces.resize(6);

        for (uint32_t f = 0; f < 6; ++f)
        {
            env.irradianceFaces[f].resize(sz * sz * 4);
            float* dst = env.irradianceFaces[f].data();

            for (uint32_t y = 0; y < sz; ++y)
            {
                for (uint32_t x = 0; x < sz; ++x)
                {
                    float u = (float(x) + 0.5f) / float(sz);
                    float v = (float(y) + 0.5f) / float(sz);
                    Vec3 N = cubeUvToDir(f, u, v);

                    Vec3 T, B;
                    buildTBN(N, T, B);

                    Vec3 irradiance{0.0f};
                    for (uint32_t s = 0; s < irradianceSamples; ++s)
                    {
                        Vec2 xi = hammersley(s, irradianceSamples);
                        float phi = 2.0f * glm::pi<float>() * xi.x;
                        float cosTheta = glm::sqrt(1.0f - xi.y);
                        float sinTheta = glm::sqrt(xi.y);

                        Vec3 sampleDir = T * (glm::cos(phi) * sinTheta) +
                                         B * (glm::sin(phi) * sinTheta) + N * cosTheta;
                        sampleDir = glm::normalize(sampleDir);
                        irradiance += proceduralSky(sampleDir);
                    }
                    irradiance *= glm::pi<float>() / float(irradianceSamples);

                    uint32_t base = (y * sz + x) * 4;
                    dst[base + 0] = irradiance.x;
                    dst[base + 1] = irradiance.y;
                    dst[base + 2] = irradiance.z;
                    dst[base + 3] = 1.0f;
                }
            }
        }
    }

    // --- Prefiltered specular cubemap (128×128, 8 mips, stored as float32) ---
    {
        constexpr uint32_t sz = 128;
        constexpr uint8_t mips = 8;  // log2(128)+1
        constexpr uint32_t prefilteredSamples = 128;
        env.prefilteredSize = sz;
        env.prefilteredMips = mips;
        env.prefilteredFaces.resize(6);
        for (uint32_t f = 0; f < 6; ++f)
            env.prefilteredFaces[f].resize(mips);

        for (uint8_t m = 0; m < mips; ++m)
        {
            uint32_t mipSz = std::max(sz >> m, 1u);
            float roughness = float(m) / float(mips - 1);

            for (uint32_t f = 0; f < 6; ++f)
            {
                env.prefilteredFaces[f][m].resize(mipSz * mipSz * 4);
                float* dst = env.prefilteredFaces[f][m].data();

                for (uint32_t y = 0; y < mipSz; ++y)
                {
                    for (uint32_t x = 0; x < mipSz; ++x)
                    {
                        float u = (float(x) + 0.5f) / float(mipSz);
                        float v = (float(y) + 0.5f) / float(mipSz);
                        Vec3 N = cubeUvToDir(f, u, v);
                        Vec3 R = N;

                        Vec3 color{0.0f};
                        float totalWeight = 0.0f;

                        if (roughness < 0.01f)
                        {
                            color = proceduralSky(N);
                            totalWeight = 1.0f;
                        }
                        else
                        {
                            Vec3 T, B;
                            buildTBN(N, T, B);

                            for (uint32_t s = 0; s < prefilteredSamples; ++s)
                            {
                                Vec2 xi = hammersley(s, prefilteredSamples);
                                Vec3 H = importanceSampleGGX(xi, roughness);

                                Vec3 Hw = T * H.x + B * H.y + N * H.z;
                                Hw = glm::normalize(Hw);

                                Vec3 L = glm::normalize(2.0f * glm::dot(R, Hw) * Hw - R);
                                float NdotL = glm::dot(N, L);

                                if (NdotL > 0.0f)
                                {
                                    color += proceduralSky(L) * NdotL;
                                    totalWeight += NdotL;
                                }
                            }
                        }

                        if (totalWeight > 0.0f)
                            color /= totalWeight;

                        uint32_t base = (y * mipSz + x) * 4;
                        dst[base + 0] = color.x;
                        dst[base + 1] = color.y;
                        dst[base + 2] = color.z;
                        dst[base + 3] = 1.0f;
                    }
                }
            }
        }
    }

    // --- BRDF LUT (128×128, RG32F) ---
    {
        constexpr uint32_t size = 128;
        constexpr uint32_t numSamples = 1024;
        env.brdfLutSize = size;
        env.brdfLutData.resize(size * size * 2, 0.0f);

        for (uint32_t y = 0; y < size; ++y)
        {
            float NdotV = glm::max((float(y) + 0.5f) / float(size), 0.001f);
            Vec3 V{glm::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV};

            for (uint32_t x = 0; x < size; ++x)
            {
                float roughness = glm::max((float(x) + 0.5f) / float(size), 0.01f);
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

                env.brdfLutData[y * size * 2 + x * 2 + 0] = scale / float(numSamples);
                env.brdfLutData[y * size * 2 + x * 2 + 1] = bias / float(numSamples);
            }
        }
    }

    return env;
}

// ---------------------------------------------------------------------------
// IblResources::generateDefault — runs CPU computation, then uploads
// ---------------------------------------------------------------------------

bool IblResources::generateDefault()
{
    return upload(generateDefaultAsset());
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
