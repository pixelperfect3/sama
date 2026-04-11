#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "engine/assets/CubemapLoader.h"
#include "engine/assets/EnvironmentAsset.h"
#include "engine/assets/EnvironmentAssetSerializer.h"

namespace fs = std::filesystem;

namespace
{

// ---------------------------------------------------------------------------
// Minimal KTX1 writer for RGBA32F cubemaps. We only need enough of the KTX1
// spec to produce a file bimg::imageParse can round-trip:
//
//   12  byte identifier  "«1»«KTX 11»«rn»«1A»«n»"
//   u32 endianness       0x04030201
//   u32 glType           GL_FLOAT (0x1406)
//   u32 glTypeSize       4
//   u32 glFormat         GL_RGBA (0x1908)
//   u32 glInternalFormat GL_RGBA32F (0x8814)
//   u32 glBaseInternal   GL_RGBA (0x1908)
//   u32 pixelWidth
//   u32 pixelHeight
//   u32 pixelDepth       0
//   u32 numArrayElements 0
//   u32 numFaces         6
//   u32 numMipmapLevels  1
//   u32 bytesOfKeyValueData 0
//   for each mip:
//     u32 imageSize (size of one cubeface)
//     for each face: faceData + cubePadding (4-byte align)
//     mipPadding
//
// Since faceSize = width*height*16 is always a multiple of 4 for RGBA32F,
// no padding is required for uint sizes we produce here.
// ---------------------------------------------------------------------------

void writeU32LE(std::ofstream& out, uint32_t v)
{
    uint8_t bytes[4] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF),
                        static_cast<uint8_t>((v >> 16) & 0xFF),
                        static_cast<uint8_t>((v >> 24) & 0xFF)};
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

// Writes a uniformly coloured RGBA32F cubemap (32×32, 1 mip, 6 faces) to
// `path` as a KTX1 file. Each face gets a distinct linear colour:
//   +X red, -X cyan, +Y green, -Y magenta, +Z blue, -Z yellow.
bool writeTestCubemapKtx(const fs::path& path)
{
    constexpr uint32_t kWidth = 32;
    constexpr uint32_t kHeight = 32;
    constexpr uint32_t kFaceFloats = kWidth * kHeight * 4;  // RGBA
    constexpr uint32_t kFaceBytes = kFaceFloats * sizeof(float);

    // Linear-space face colours.
    const std::array<std::array<float, 4>, 6> kFaceColors = {{
        {{1.0f, 0.0f, 0.0f, 1.0f}},  // +X red
        {{0.0f, 1.0f, 1.0f, 1.0f}},  // -X cyan
        {{0.0f, 1.0f, 0.0f, 1.0f}},  // +Y green
        {{1.0f, 0.0f, 1.0f, 1.0f}},  // -Y magenta
        {{0.0f, 0.0f, 1.0f, 1.0f}},  // +Z blue
        {{1.0f, 1.0f, 0.0f, 1.0f}},  // -Z yellow
    }};

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }

    // KTX1 identifier.
    const uint8_t identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31,
                                    0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    out.write(reinterpret_cast<const char*>(identifier), 12);

    writeU32LE(out, 0x04030201);  // endianness
    writeU32LE(out, 0x1406);      // glType = GL_FLOAT
    writeU32LE(out, 4);           // glTypeSize
    writeU32LE(out, 0x1908);      // glFormat = GL_RGBA
    writeU32LE(out, 0x8814);      // glInternalFormat = GL_RGBA32F
    writeU32LE(out, 0x1908);      // glBaseInternalFormat = GL_RGBA
    writeU32LE(out, kWidth);
    writeU32LE(out, kHeight);
    writeU32LE(out, 0);  // pixelDepth
    writeU32LE(out, 0);  // numArrayElements (non-array)
    writeU32LE(out, 6);  // numFaces (cube)
    writeU32LE(out, 1);  // numMipmapLevels
    writeU32LE(out, 0);  // bytesOfKeyValueData

    // Single mip, 6 faces.
    writeU32LE(out, kFaceBytes);  // imageSize = bytes for ONE face

    for (uint32_t f = 0; f < 6; ++f)
    {
        std::vector<float> pixels(kFaceFloats);
        for (uint32_t p = 0; p < kWidth * kHeight; ++p)
        {
            pixels[p * 4 + 0] = kFaceColors[f][0];
            pixels[p * 4 + 1] = kFaceColors[f][1];
            pixels[p * 4 + 2] = kFaceColors[f][2];
            pixels[p * 4 + 3] = kFaceColors[f][3];
        }
        out.write(reinterpret_cast<const char*>(pixels.data()), kFaceBytes);
        // kFaceBytes is a multiple of 4, so no cube padding needed.
    }

    return out.good();
}

// Ensures the test cubemap exists at the canonical path. Returns path as
// a string, or "" if generation failed.
std::string ensureTestCubemap()
{
    const fs::path path = fs::path(ENGINE_SOURCE_DIR) / "assets" / "env" / "test_cubemap.ktx";
    if (!fs::exists(path))
    {
        if (!writeTestCubemapKtx(path))
        {
            return {};
        }
    }
    return path.string();
}

}  // namespace

TEST_CASE("CubemapLoader returns nullopt for missing file", "[assets][cubemap]")
{
    auto result = engine::assets::loadCubemapEnvironment("this_file_does_not_exist.ktx");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("CubemapLoader returns nullopt for truncated KTX", "[assets][cubemap]")
{
    // Write a file that's only 8 bytes — way smaller than the KTX1 header.
    const fs::path tmp = fs::temp_directory_path() / "sama_truncated.ktx";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        const uint8_t garbage[8] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB};
        out.write(reinterpret_cast<const char*>(garbage), 8);
    }

    auto result = engine::assets::loadCubemapEnvironment(tmp.string());
    REQUIRE_FALSE(result.has_value());

    std::error_code ec;
    fs::remove(tmp, ec);
}

TEST_CASE("CubemapLoader loads synthetic 6-face KTX", "[assets][cubemap]")
{
    const std::string path = ensureTestCubemap();
    REQUIRE_FALSE(path.empty());

    auto env = engine::assets::loadCubemapEnvironment(path);
    REQUIRE(env.has_value());

    // Shared integration produces the same dimensions as the procedural sky.
    REQUIRE(env->irradianceSize == 64);
    REQUIRE(env->irradianceFaces.size() == 6);
    for (const auto& face : env->irradianceFaces)
    {
        REQUIRE(face.size() == 64u * 64u * 4u);
    }
    REQUIRE(env->prefilteredSize == 128);
    REQUIRE(env->prefilteredMips == 8);
    REQUIRE(env->prefilteredFaces.size() == 6);
    REQUIRE(env->brdfLutSize == 128);
    REQUIRE(env->brdfLutData.size() == 128u * 128u * 2u);
}

TEST_CASE("CubemapLoader irradiance follows face colour shift", "[assets][cubemap]")
{
    const std::string path = ensureTestCubemap();
    REQUIRE_FALSE(path.empty());

    auto env = engine::assets::loadCubemapEnvironment(path);
    REQUIRE(env.has_value());

    // The face of the irradiance cubemap whose normal points in +X should
    // still be red-dominant: the hemisphere around +X sees mostly the red
    // (+X) face, with neighbouring faces contributing greens/blues/etc.
    // Pick the centre texel of face 0 (+X).
    const uint32_t sz = env->irradianceSize;
    const uint32_t cx = sz / 2;
    const uint32_t cy = sz / 2;
    const float* px = &env->irradianceFaces[0][(cy * sz + cx) * 4];

    const float r = px[0];
    const float g = px[1];
    const float b = px[2];
    REQUIRE(r > g);
    REQUIRE(r > b);

    // And the -X face (index 1) should be cyan-dominant → g ≈ b and both > r.
    const float* px_nx = &env->irradianceFaces[1][(cy * sz + cx) * 4];
    REQUIRE(px_nx[1] > px_nx[0]);
    REQUIRE(px_nx[2] > px_nx[0]);
}

TEST_CASE("CubemapLoader output round-trips through environment serializer", "[assets][cubemap]")
{
    const std::string path = ensureTestCubemap();
    REQUIRE_FALSE(path.empty());

    auto env = engine::assets::loadCubemapEnvironment(path);
    REQUIRE(env.has_value());

    const fs::path out = fs::temp_directory_path() / "sama_cubemap_roundtrip.env";
    REQUIRE(engine::assets::saveEnvironmentAsset(out.string(), *env));

    auto reloaded = engine::assets::loadEnvironmentAsset(out.string());
    REQUIRE(reloaded.has_value());

    REQUIRE(reloaded->irradianceSize == env->irradianceSize);
    REQUIRE(reloaded->prefilteredSize == env->prefilteredSize);
    REQUIRE(reloaded->prefilteredMips == env->prefilteredMips);
    REQUIRE(reloaded->brdfLutSize == env->brdfLutSize);

    for (uint32_t f = 0; f < 6; ++f)
    {
        REQUIRE(reloaded->irradianceFaces[f].size() == env->irradianceFaces[f].size());
        REQUIRE(std::memcmp(reloaded->irradianceFaces[f].data(), env->irradianceFaces[f].data(),
                            env->irradianceFaces[f].size() * sizeof(float)) == 0);
    }

    REQUIRE(reloaded->brdfLutData.size() == env->brdfLutData.size());
    REQUIRE(std::memcmp(reloaded->brdfLutData.data(), env->brdfLutData.data(),
                        env->brdfLutData.size() * sizeof(float)) == 0);

    std::error_code ec;
    fs::remove(out, ec);
}

TEST_CASE("CubemapLoader rejects non-cubemap KTX", "[assets][cubemap]")
{
    // Write a minimal KTX1 file with numFaces = 1 (plain 2D texture). Must
    // be rejected by the loader.
    const fs::path tmp = fs::temp_directory_path() / "sama_not_cubemap.ktx";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        const uint8_t identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31,
                                        0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        out.write(reinterpret_cast<const char*>(identifier), 12);
        writeU32LE(out, 0x04030201);
        writeU32LE(out, 0x1406);  // GL_FLOAT
        writeU32LE(out, 4);
        writeU32LE(out, 0x1908);  // GL_RGBA
        writeU32LE(out, 0x8814);  // GL_RGBA32F
        writeU32LE(out, 0x1908);
        writeU32LE(out, 4);  // width
        writeU32LE(out, 4);  // height
        writeU32LE(out, 0);
        writeU32LE(out, 0);
        writeU32LE(out, 1);  // numFaces = 1 → not a cubemap
        writeU32LE(out, 1);
        writeU32LE(out, 0);
        const uint32_t faceBytes = 4 * 4 * 4 * sizeof(float);
        writeU32LE(out, faceBytes);
        std::vector<float> pixels(4 * 4 * 4, 0.5f);
        out.write(reinterpret_cast<const char*>(pixels.data()), faceBytes);
    }

    auto result = engine::assets::loadCubemapEnvironment(tmp.string());
    REQUIRE_FALSE(result.has_value());

    std::error_code ec;
    fs::remove(tmp, ec);
}
