#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "engine/assets/EnvironmentAssetSerializer.h"
#include "engine/assets/HdrLoader.h"

// ---------------------------------------------------------------------------
// TestHdrLoader — exercises the Radiance `.hdr` equirectangular import path.
// Uses a small synthetic test HDR bundled at `assets/env/test_sky.hdr` so
// the tests are deterministic and do not require network access.
// ---------------------------------------------------------------------------

using engine::assets::EnvironmentAsset;
using engine::assets::loadEnvironmentAsset;
using engine::assets::loadHdrEnvironment;
using engine::assets::saveEnvironmentAsset;

namespace
{
std::string bundledHdrPath()
{
    return std::string(ENGINE_SOURCE_DIR) + "/assets/env/test_sky.hdr";
}
}  // namespace

TEST_CASE("HdrLoader returns nullopt for missing file", "[assets][hdr]")
{
    auto result = loadHdrEnvironment("this_file_really_does_not_exist_12345.hdr");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("HdrLoader returns nullopt for corrupted file", "[assets][hdr]")
{
    // Write a handful of junk bytes that do not form a valid Radiance header.
    const std::string tmp = std::string(ENGINE_SOURCE_DIR) + "/tests/assets/_tmp_bad.hdr";
    {
        std::FILE* f = std::fopen(tmp.c_str(), "wb");
        REQUIRE(f != nullptr);
        const char junk[] = "not a hdr file at all";
        std::fwrite(junk, 1, sizeof(junk) - 1, f);
        std::fclose(f);
    }
    auto result = loadHdrEnvironment(tmp);
    std::filesystem::remove(tmp);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("HdrLoader bakes a bundled test sky into an EnvironmentAsset", "[assets][hdr][slow]")
{
    const std::string path = bundledHdrPath();
    REQUIRE(std::filesystem::exists(path));

    auto loaded = loadHdrEnvironment(path);
    REQUIRE(loaded.has_value());

    const EnvironmentAsset& env = *loaded;

    REQUIRE(env.irradianceFaces.size() == 6);
    REQUIRE(env.irradianceSize > 0);
    REQUIRE(env.prefilteredFaces.size() == 6);
    REQUIRE(env.prefilteredMips > 0);
    REQUIRE(env.brdfLutData.size() == env.brdfLutSize * env.brdfLutSize * 2);

    // Irradiance must be non-zero — the synthetic HDR has strong gradient
    // color so at least one face should integrate to non-black.
    bool anyNonZero = false;
    for (const auto& face : env.irradianceFaces)
    {
        for (float v : face)
        {
            if (v > 1e-4f)
            {
                anyNonZero = true;
                break;
            }
        }
        if (anyNonZero)
            break;
    }
    REQUIRE(anyNonZero);
}

TEST_CASE("HdrLoader output survives a .env round-trip", "[assets][hdr][slow]")
{
    const std::string path = bundledHdrPath();
    auto loaded = loadHdrEnvironment(path);
    REQUIRE(loaded.has_value());

    const std::string tmp = std::string(ENGINE_SOURCE_DIR) + "/tests/assets/_tmp_roundtrip.env";
    REQUIRE(saveEnvironmentAsset(tmp, *loaded));

    auto reloaded = loadEnvironmentAsset(tmp);
    std::filesystem::remove(tmp);
    REQUIRE(reloaded.has_value());

    // Byte-for-byte equivalence across every float buffer.
    REQUIRE(reloaded->irradianceSize == loaded->irradianceSize);
    REQUIRE(reloaded->prefilteredSize == loaded->prefilteredSize);
    REQUIRE(reloaded->prefilteredMips == loaded->prefilteredMips);
    REQUIRE(reloaded->brdfLutSize == loaded->brdfLutSize);

    REQUIRE(reloaded->irradianceFaces.size() == loaded->irradianceFaces.size());
    for (size_t f = 0; f < loaded->irradianceFaces.size(); ++f)
    {
        const auto& a = loaded->irradianceFaces[f];
        const auto& b = reloaded->irradianceFaces[f];
        REQUIRE(a.size() == b.size());
        REQUIRE(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
    }

    REQUIRE(reloaded->prefilteredFaces.size() == loaded->prefilteredFaces.size());
    for (size_t f = 0; f < loaded->prefilteredFaces.size(); ++f)
    {
        REQUIRE(reloaded->prefilteredFaces[f].size() == loaded->prefilteredFaces[f].size());
        for (size_t m = 0; m < loaded->prefilteredFaces[f].size(); ++m)
        {
            const auto& a = loaded->prefilteredFaces[f][m];
            const auto& b = reloaded->prefilteredFaces[f][m];
            REQUIRE(a.size() == b.size());
            REQUIRE(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
        }
    }

    REQUIRE(loaded->brdfLutData.size() == reloaded->brdfLutData.size());
    REQUIRE(std::memcmp(loaded->brdfLutData.data(), reloaded->brdfLutData.data(),
                        loaded->brdfLutData.size() * sizeof(float)) == 0);
}
