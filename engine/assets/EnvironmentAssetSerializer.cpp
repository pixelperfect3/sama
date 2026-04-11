#include "engine/assets/EnvironmentAssetSerializer.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace engine::assets
{

namespace
{

constexpr char kMagic[4] = {'S', 'A', 'E', 'V'};

bool readExact(std::FILE* f, void* dst, size_t bytes)
{
    return std::fread(dst, 1, bytes, f) == bytes;
}

bool writeExact(std::FILE* f, const void* src, size_t bytes)
{
    return std::fwrite(src, 1, bytes, f) == bytes;
}

}  // namespace

bool saveEnvironmentAsset(std::string_view path, const EnvironmentAsset& env)
{
    const std::string p(path);
    std::FILE* f = std::fopen(p.c_str(), "wb");
    if (!f)
        return false;

    // Header
    if (!writeExact(f, kMagic, sizeof(kMagic)))
        goto fail;
    {
        const uint32_t version = kEnvironmentAssetVersion;
        if (!writeExact(f, &version, sizeof(version)))
            goto fail;
    }
    if (!writeExact(f, &env.irradianceSize, sizeof(env.irradianceSize)))
        goto fail;
    if (!writeExact(f, &env.prefilteredSize, sizeof(env.prefilteredSize)))
        goto fail;
    {
        const uint32_t mips = env.prefilteredMips;
        if (!writeExact(f, &mips, sizeof(mips)))
            goto fail;
    }
    if (!writeExact(f, &env.brdfLutSize, sizeof(env.brdfLutSize)))
        goto fail;

    // Irradiance: 6 × sz² × 4 floats
    if (env.irradianceFaces.size() != 6)
        goto fail;
    for (uint32_t f_idx = 0; f_idx < 6; ++f_idx)
    {
        const auto& face = env.irradianceFaces[f_idx];
        const size_t expected = env.irradianceSize * env.irradianceSize * 4;
        if (face.size() != expected)
            goto fail;
        if (!writeExact(f, face.data(), face.size() * sizeof(float)))
            goto fail;
    }

    // Prefiltered: 6 × mips × mipSz² × 4 floats, face-major then mip-major.
    if (env.prefilteredFaces.size() != 6)
        goto fail;
    for (uint32_t f_idx = 0; f_idx < 6; ++f_idx)
    {
        if (env.prefilteredFaces[f_idx].size() != env.prefilteredMips)
            goto fail;
        for (uint8_t m = 0; m < env.prefilteredMips; ++m)
        {
            const auto& mip = env.prefilteredFaces[f_idx][m];
            uint32_t mipSz = env.prefilteredSize >> m;
            if (mipSz < 1)
                mipSz = 1;
            const size_t expected = mipSz * mipSz * 4;
            if (mip.size() != expected)
                goto fail;
            if (!writeExact(f, mip.data(), mip.size() * sizeof(float)))
                goto fail;
        }
    }

    // BRDF LUT: sz² × 2 floats
    {
        const size_t expected = env.brdfLutSize * env.brdfLutSize * 2;
        if (env.brdfLutData.size() != expected)
            goto fail;
        if (!writeExact(f, env.brdfLutData.data(), env.brdfLutData.size() * sizeof(float)))
            goto fail;
    }

    std::fclose(f);
    return true;

fail:
    std::fclose(f);
    return false;
}

std::optional<EnvironmentAsset> loadEnvironmentAsset(std::string_view path)
{
    const std::string p(path);
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f)
        return std::nullopt;

    EnvironmentAsset env;

    // Header
    char magic[4];
    if (!readExact(f, magic, sizeof(magic)) || std::memcmp(magic, kMagic, 4) != 0)
    {
        std::fclose(f);
        return std::nullopt;
    }
    uint32_t version = 0;
    if (!readExact(f, &version, sizeof(version)) || version != kEnvironmentAssetVersion)
    {
        std::fclose(f);
        return std::nullopt;
    }
    if (!readExact(f, &env.irradianceSize, sizeof(env.irradianceSize)))
    {
        std::fclose(f);
        return std::nullopt;
    }
    if (!readExact(f, &env.prefilteredSize, sizeof(env.prefilteredSize)))
    {
        std::fclose(f);
        return std::nullopt;
    }
    uint32_t mips = 0;
    if (!readExact(f, &mips, sizeof(mips)) || mips == 0 || mips > 16)
    {
        std::fclose(f);
        return std::nullopt;
    }
    env.prefilteredMips = static_cast<uint8_t>(mips);
    if (!readExact(f, &env.brdfLutSize, sizeof(env.brdfLutSize)))
    {
        std::fclose(f);
        return std::nullopt;
    }

    // Irradiance
    env.irradianceFaces.resize(6);
    for (uint32_t f_idx = 0; f_idx < 6; ++f_idx)
    {
        const size_t pixelCount = env.irradianceSize * env.irradianceSize * 4;
        env.irradianceFaces[f_idx].resize(pixelCount);
        if (!readExact(f, env.irradianceFaces[f_idx].data(), pixelCount * sizeof(float)))
        {
            std::fclose(f);
            return std::nullopt;
        }
    }

    // Prefiltered
    env.prefilteredFaces.resize(6);
    for (uint32_t f_idx = 0; f_idx < 6; ++f_idx)
    {
        env.prefilteredFaces[f_idx].resize(env.prefilteredMips);
        for (uint8_t m = 0; m < env.prefilteredMips; ++m)
        {
            uint32_t mipSz = env.prefilteredSize >> m;
            if (mipSz < 1)
                mipSz = 1;
            const size_t pixelCount = mipSz * mipSz * 4;
            env.prefilteredFaces[f_idx][m].resize(pixelCount);
            if (!readExact(f, env.prefilteredFaces[f_idx][m].data(), pixelCount * sizeof(float)))
            {
                std::fclose(f);
                return std::nullopt;
            }
        }
    }

    // BRDF LUT
    {
        const size_t pixelCount = env.brdfLutSize * env.brdfLutSize * 2;
        env.brdfLutData.resize(pixelCount);
        if (!readExact(f, env.brdfLutData.data(), pixelCount * sizeof(float)))
        {
            std::fclose(f);
            return std::nullopt;
        }
    }

    std::fclose(f);
    return env;
}

}  // namespace engine::assets
