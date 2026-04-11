#include "engine/assets/CubemapLoader.h"

#include <bimg/bimg.h>
#include <bx/allocator.h>
#include <bx/error.h>

// NOTE: we intentionally use bimg's reference-form `imageParse(ImageContainer&,
// data, size, err)` from `bimg/src/image.cpp` rather than the pointer-returning
// form from `bimg/src/image_decode.cpp`. The latter drags stb_image into the
// final link and conflicts with engine_assets/TextureLoader.cpp's
// `STB_IMAGE_IMPLEMENTATION`. The reference form only parses KTX1/KTX2/DDS
// headers (no JPEG/PNG decode path) which is exactly what we need here.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "engine/math/Types.h"
#include "engine/rendering/IblResources.h"

namespace engine::assets
{

namespace
{

using engine::math::Vec3;

// A single decoded cubemap face in RGBA32F, linear-space.
struct FacePixels
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> data;  // width * height * 4
};

// ---------------------------------------------------------------------------
// Read a file fully into memory. Returns empty vector on any I/O failure.
// ---------------------------------------------------------------------------

std::vector<uint8_t> readFileBytes(std::string_view path)
{
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return {};
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return {};
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        return {};
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// Decode a single cubemap face to linear RGBA32F via bimg::imageDecodeToRgba32f.
// Handles any source format bimg knows how to unpack, including compressed.
// ---------------------------------------------------------------------------

bool decodeFaceToRgba32f(bx::AllocatorI* allocator, const bimg::ImageContainer& container,
                         uint16_t face, const void* sourceData, uint32_t sourceSize,
                         FacePixels& out)
{
    // When the container was populated via the reference-form `imageParse`
    // (no heap copy), its m_data/m_size are null; the caller passes the raw
    // file bytes so `imageGetRawData` can offset into them.
    const void* dataPtr = container.m_data != nullptr ? container.m_data : sourceData;
    const uint32_t dataSize = container.m_data != nullptr ? container.m_size : sourceSize;

    bimg::ImageMip mip;
    if (!bimg::imageGetRawData(container, face, /*lod*/ 0, dataPtr, dataSize, mip))
    {
        return false;
    }

    out.width = mip.m_width;
    out.height = mip.m_height;
    out.data.assign(static_cast<size_t>(mip.m_width) * mip.m_height * 4, 0.0f);

    const uint32_t dstPitch = mip.m_width * 4 * sizeof(float);
    bimg::imageDecodeToRgba32f(allocator, out.data.data(), mip.m_data, mip.m_width, mip.m_height,
                               /*depth*/ 1, dstPitch, mip.m_format);
    return true;
}

// ---------------------------------------------------------------------------
// Bilinear sample of a face at floating-point UV in [0,1]². Mirrors the
// addressing convention used by `cubeUvToDir` in IblResources.cpp so a
// direction fed through face-selection and back through the sampler lands
// on the same pixel.
// ---------------------------------------------------------------------------

Vec3 sampleFaceBilinear(const FacePixels& face, float u, float v)
{
    const float fx = glm::clamp(u, 0.0f, 1.0f) * static_cast<float>(face.width) - 0.5f;
    const float fy = glm::clamp(v, 0.0f, 1.0f) * static_cast<float>(face.height) - 0.5f;

    const int x0 = std::max(0, static_cast<int>(std::floor(fx)));
    const int y0 = std::max(0, static_cast<int>(std::floor(fy)));
    const int x1 = std::min(static_cast<int>(face.width) - 1, x0 + 1);
    const int y1 = std::min(static_cast<int>(face.height) - 1, y0 + 1);

    const float tx = glm::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
    const float ty = glm::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);

    auto texel = [&](int x, int y)
    {
        const size_t base = (static_cast<size_t>(y) * face.width + static_cast<size_t>(x)) * 4;
        return Vec3(face.data[base + 0], face.data[base + 1], face.data[base + 2]);
    };

    const Vec3 c00 = texel(x0, y0);
    const Vec3 c10 = texel(x1, y0);
    const Vec3 c01 = texel(x0, y1);
    const Vec3 c11 = texel(x1, y1);

    const Vec3 cx0 = glm::mix(c00, c10, tx);
    const Vec3 cx1 = glm::mix(c01, c11, tx);
    return glm::mix(cx0, cx1, ty);
}

// ---------------------------------------------------------------------------
// Standard cubemap face selection: given a unit direction, pick the face
// whose axis has the largest magnitude and compute the 2D UV on that face.
// The face indexing + UV layout matches `cubeUvToDir` in IblResources.cpp.
// ---------------------------------------------------------------------------

Vec3 sampleCubemap(const std::vector<FacePixels>& faces, const Vec3& dir)
{
    const float ax = std::fabs(dir.x);
    const float ay = std::fabs(dir.y);
    const float az = std::fabs(dir.z);

    uint32_t faceIndex = 0;
    float sc = 0.0f;
    float tc = 0.0f;
    float ma = 0.0f;

    if (ax >= ay && ax >= az)
    {
        if (dir.x >= 0.0f)  // +X
        {
            faceIndex = 0;
            sc = -dir.z;
            tc = -dir.y;
        }
        else  // -X
        {
            faceIndex = 1;
            sc = dir.z;
            tc = -dir.y;
        }
        ma = ax;
    }
    else if (ay >= ax && ay >= az)
    {
        if (dir.y >= 0.0f)  // +Y
        {
            faceIndex = 2;
            sc = dir.x;
            tc = dir.z;
        }
        else  // -Y
        {
            faceIndex = 3;
            sc = dir.x;
            tc = -dir.z;
        }
        ma = ay;
    }
    else
    {
        if (dir.z >= 0.0f)  // +Z
        {
            faceIndex = 4;
            sc = dir.x;
            tc = -dir.y;
        }
        else  // -Z
        {
            faceIndex = 5;
            sc = -dir.x;
            tc = -dir.y;
        }
        ma = az;
    }

    const float u = 0.5f * (sc / ma + 1.0f);
    const float v = 0.5f * (tc / ma + 1.0f);
    return sampleFaceBilinear(faces[faceIndex], u, v);
}

}  // namespace

// ---------------------------------------------------------------------------
// loadCubemapEnvironment
// ---------------------------------------------------------------------------

std::optional<EnvironmentAsset> loadCubemapEnvironment(std::string_view path)
{
    const std::vector<uint8_t> bytes = readFileBytes(path);
    if (bytes.empty())
    {
        return std::nullopt;
    }

    bx::DefaultAllocator allocator;
    bx::Error err;

    // Reference-form parse: populates `container` with pointers into `bytes`.
    // The caller is responsible for keeping `bytes` alive for the duration of
    // any imageGetRawData calls. No imageFree needed: no heap-allocated
    // pixel data, just a stack-local container.
    bimg::ImageContainer container{};
    if (!bimg::imageParse(container, bytes.data(), static_cast<uint32_t>(bytes.size()), &err) ||
        !err.isOk())
    {
        return std::nullopt;
    }

    // Require a square 6-face cubemap.
    const bool validDimensions =
        container.m_cubeMap && container.m_width > 0 && container.m_height == container.m_width;
    if (!validDimensions)
    {
        return std::nullopt;
    }

    // Decode each face to linear RGBA32F so the integration callback can run
    // against a uniform format.
    std::vector<FacePixels> faces(6);
    for (uint16_t f = 0; f < 6; ++f)
    {
        if (!decodeFaceToRgba32f(&allocator, container, f, bytes.data(),
                                 static_cast<uint32_t>(bytes.size()), faces[f]))
        {
            return std::nullopt;
        }
        if (faces[f].width == 0 || faces[f].height == 0)
        {
            return std::nullopt;
        }
    }

    // Build the sky-sampling closure and run the shared IBL integration.
    auto skyCallback = [&faces](const Vec3& direction) -> Vec3
    { return sampleCubemap(faces, glm::normalize(direction)); };

    return engine::rendering::IblResources::generateAssetFromSky(skyCallback);
}

}  // namespace engine::assets
