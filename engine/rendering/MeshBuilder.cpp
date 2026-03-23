#include "engine/rendering/MeshBuilder.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/packing.hpp>

#include "engine/rendering/GpuFeatures.h"
#include "engine/rendering/VertexLayouts.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Oct-encoding helpers
//
// Maps a unit vector onto an octahedron, projects to [-1,1]², then folds
// negative-Z octants so the whole sphere fits in the positive square.
// Reference: "A Survey of Efficient Representations for Independent Unit Vectors"
//            Cigolle et al., 2014.
// ---------------------------------------------------------------------------

// Returns the XY position on the octahedron for the unit vector v.
// Result is in [-1,1]².
static glm::vec2 octWrap(glm::vec2 v)
{
    return (glm::vec2(1.0f) - glm::abs(glm::vec2(v.y, v.x))) *
           glm::vec2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
}

static glm::vec2 encodeOctF(glm::vec3 n)
{
    // Project onto octahedron.
    n /= (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
    // Fold the lower hemisphere.
    glm::vec2 oct = (n.z >= 0.0f) ? glm::vec2(n.x, n.y) : octWrap(glm::vec2(n.x, n.y));
    return oct;  // in [-1,1]²
}

// Encode a unit normal to two snorm16 values.
static glm::i16vec2 encodeOct(glm::vec3 n)
{
    const glm::vec2 oct = encodeOctF(n);
    // Map [-1,1] -> [-32767,32767] (snorm16 range, avoiding -32768 for symmetry).
    return glm::i16vec2(static_cast<int16_t>(std::round(oct.x * 32767.0f)),
                        static_cast<int16_t>(std::round(oct.y * 32767.0f)));
}

// Encode a tangent to four unorm8 values (bgfx Uint8 normalized).
// bgfx maps [0,255]->[0,1] in the shader; the shader remaps with 2*v-1 to get [-1,1].
// So: float_val in [-1,1] -> uint8_t = round((float_val * 0.5 + 0.5) * 255)
// sign should be +1.0f or -1.0f (bitangent handedness).
static glm::u8vec4 encodeTangent(glm::vec3 t, float sign)
{
    const glm::vec2 oct = encodeOctF(t);
    // Map [-1,1] -> [0,255]
    const auto toUnorm8 = [](float v) -> uint8_t
    { return static_cast<uint8_t>(std::round((v * 0.5f + 0.5f) * 255.0f)); };
    const uint8_t x = toUnorm8(oct.x);
    const uint8_t y = toUnorm8(oct.y);
    const uint8_t s = toUnorm8(sign);   // +1 -> 255, -1 -> 0
    return glm::u8vec4(x, y, 128u, s);  // Z=128 (zero in snorm)
}

// ---------------------------------------------------------------------------
// buildMesh
// ---------------------------------------------------------------------------

Mesh buildMesh(const MeshData& data)
{
    if (data.positions.empty() || data.indices.empty())
        return {};

    // Vertex count inferred from positions (x,y,z triples).
    if (data.positions.size() % 3 != 0)
        return {};

    const uint32_t vertexCount = static_cast<uint32_t>(data.positions.size() / 3);
    const uint32_t indexCount = static_cast<uint32_t>(data.indices.size());

    Mesh mesh;
    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;
    mesh.boundsMin = data.boundsMin;
    mesh.boundsMax = data.boundsMax;

    // -----------------------------------------------------------------------
    // Stream 0 — position buffer
    // -----------------------------------------------------------------------
    {
        const bgfx::VertexLayout layout = positionLayout();

        const uint32_t byteSize = vertexCount * layout.getStride();
        const bgfx::Memory* mem = bgfx::copy(data.positions.data(), byteSize);
        if (!mem)
            return {};

        mesh.positionVbh = bgfx::createVertexBuffer(mem, layout);
        if (!bgfx::isValid(mesh.positionVbh))
            return {};
    }

    // -----------------------------------------------------------------------
    // Stream 1 — surface buffer (optional)
    // -----------------------------------------------------------------------
    const bool hasSurface = !data.normals.empty() && !data.tangents.empty() && !data.uvs.empty();

    if (hasSurface)
    {
        // Validate sizes.
        if (data.normals.size() != static_cast<size_t>(vertexCount) * 2 ||
            data.tangents.size() != static_cast<size_t>(vertexCount) * 4 ||
            data.uvs.size() != static_cast<size_t>(vertexCount) * 2)
        {
            bgfx::destroy(mesh.positionVbh);
            return {};
        }

        // Pack interleaved: [snorm16 nx, snorm16 ny, snorm8 tx, snorm8 ty, snorm8 tz, snorm8
        // ts, uint16 u, uint16 v]
        // Stride = 4 (normal) + 4 (tangent) + 4 (uv) = 12 bytes
        const uint32_t stride = 12u;
        const uint32_t byteSize = vertexCount * stride;
        std::vector<uint8_t> buf(byteSize);

        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            uint8_t* dst = buf.data() + i * stride;

            // snorm16×2 normal
            const int16_t nx = data.normals[i * 2 + 0];
            const int16_t ny = data.normals[i * 2 + 1];
            std::memcpy(dst + 0, &nx, sizeof(int16_t));
            std::memcpy(dst + 2, &ny, sizeof(int16_t));

            // unorm8×4 tangent (oct-encoded, bias+scale: 0=−1, 128≈0, 255=+1)
            dst[4] = data.tangents[i * 4 + 0];
            dst[5] = data.tangents[i * 4 + 1];
            dst[6] = data.tangents[i * 4 + 2];
            dst[7] = data.tangents[i * 4 + 3];

            // float16×2 UV (already encoded as uint16_t by the caller)
            const uint16_t u = data.uvs[i * 2 + 0];
            const uint16_t v = data.uvs[i * 2 + 1];
            std::memcpy(dst + 8, &u, sizeof(uint16_t));
            std::memcpy(dst + 10, &v, sizeof(uint16_t));
        }

        // Build layout matching the packed interleaved data.
        // halfPrecisionAttribs=true so TexCoord0 uses Half.
        GpuFeatures halfGpu{};
        halfGpu.halfPrecisionAttribs = true;
        const bgfx::VertexLayout layout = surfaceLayout(halfGpu);

        const bgfx::Memory* mem = bgfx::copy(buf.data(), byteSize);
        if (!mem)
        {
            bgfx::destroy(mesh.positionVbh);
            return {};
        }

        mesh.surfaceVbh = bgfx::createVertexBuffer(mem, layout);
        if (!bgfx::isValid(mesh.surfaceVbh))
        {
            bgfx::destroy(mesh.positionVbh);
            return {};
        }
    }

    // -----------------------------------------------------------------------
    // Index buffer (16-bit)
    // -----------------------------------------------------------------------
    {
        const uint32_t byteSize = indexCount * sizeof(uint16_t);
        const bgfx::Memory* mem = bgfx::copy(data.indices.data(), byteSize);
        if (!mem)
        {
            mesh.positionVbh = BGFX_INVALID_HANDLE;
            mesh.surfaceVbh = BGFX_INVALID_HANDLE;
            return {};
        }

        mesh.ibh = bgfx::createIndexBuffer(mem);
        if (!bgfx::isValid(mesh.ibh))
        {
            bgfx::destroy(mesh.positionVbh);
            if (bgfx::isValid(mesh.surfaceVbh))
                bgfx::destroy(mesh.surfaceVbh);
            return {};
        }
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// makeCubeMeshData — unit cube centred at origin, 24 unique vertices
// (4 per face so each corner has correct per-face normal/tangent).
// ---------------------------------------------------------------------------

MeshData makeCubeMeshData()
{
    // Face definitions: normal, tangent (right vector), up vector, and the
    // 4 corner offsets (all in ±0.5 local space).
    struct Face
    {
        glm::vec3 normal;
        glm::vec3 tangent;
        // Corner vertices: base + (s * right + t * up) where s,t in {-1,+1}
        glm::vec3 corners[4];
        glm::vec2 uvs[4];
    };

    // clang-format off
    const Face faces[6] = {
        // +X
        { { 1, 0, 0}, { 0, 0,-1},
          {{ 0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f}},
          {{0,0},{1,0},{1,1},{0,1}} },
        // -X
        { {-1, 0, 0}, { 0, 0, 1},
          {{-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f}},
          {{0,0},{1,0},{1,1},{0,1}} },
        // +Y
        { { 0, 1, 0}, { 1, 0, 0},
          {{-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f}},
          {{0,0},{1,0},{1,1},{0,1}} },
        // -Y
        { { 0,-1, 0}, { 1, 0, 0},
          {{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}},
          {{0,0},{1,0},{1,1},{0,1}} },
        // +Z
        { { 0, 0, 1}, { 1, 0, 0},
          {{-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f}},
          {{0,0},{1,0},{1,1},{0,1}} },
        // -Z
        { { 0, 0,-1}, {-1, 0, 0},
          {{ 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f}},
          {{0,0},{1,0},{1,1},{0,1}} },
    };
    // clang-format on

    MeshData data;
    data.boundsMin = math::Vec3(-0.5f);
    data.boundsMax = math::Vec3(0.5f);

    // 6 faces × 4 vertices = 24 vertices
    data.positions.reserve(24 * 3);
    data.normals.reserve(24 * 2);
    data.tangents.reserve(24 * 4);
    data.uvs.reserve(24 * 2);
    // 6 faces × 2 triangles × 3 indices = 36 indices
    data.indices.reserve(36);

    for (int fi = 0; fi < 6; ++fi)
    {
        const Face& f = faces[fi];
        const uint16_t baseVtx = static_cast<uint16_t>(fi * 4);

        const glm::i16vec2 encN = encodeOct(f.normal);
        const glm::u8vec4 encT = encodeTangent(f.tangent, 1.0f);

        for (int ci = 0; ci < 4; ++ci)
        {
            const glm::vec3& c = f.corners[ci];
            data.positions.push_back(c.x);
            data.positions.push_back(c.y);
            data.positions.push_back(c.z);

            data.normals.push_back(encN.x);
            data.normals.push_back(encN.y);

            data.tangents.push_back(encT.x);
            data.tangents.push_back(encT.y);
            data.tangents.push_back(encT.z);
            data.tangents.push_back(encT.w);

            // Pack UV as float16 using glm::packHalf2x16 which packs two float16
            // values into one uint32; we extract the two uint16 halves.
            const uint32_t packed = glm::packHalf2x16(f.uvs[ci]);
            data.uvs.push_back(static_cast<uint16_t>(packed & 0xFFFFu));
            data.uvs.push_back(static_cast<uint16_t>((packed >> 16) & 0xFFFFu));
        }

        // Two CCW triangles per face: (0,1,2) and (0,2,3)
        data.indices.push_back(baseVtx + 0);
        data.indices.push_back(baseVtx + 1);
        data.indices.push_back(baseVtx + 2);
        data.indices.push_back(baseVtx + 0);
        data.indices.push_back(baseVtx + 2);
        data.indices.push_back(baseVtx + 3);
    }

    return data;
}

}  // namespace engine::rendering
