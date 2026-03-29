#include "engine/assets/ObjLoader.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/assets/CpuAssetData.h"
#include "engine/assets/IFileSystem.h"
#include "engine/math/Types.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace engine::assets
{

namespace
{

constexpr std::string_view kExtensions[] = {".obj"};

// ---------------------------------------------------------------------------
// Oct-encode a float3 normal to snorm16 pair (same encoding as GltfLoader).
// ---------------------------------------------------------------------------

void encodeOctNormal(float nx, float ny, float nz, int16_t& outX, int16_t& outY)
{
    const float l1 = std::abs(nx) + std::abs(ny) + std::abs(nz);
    float ox = nx / l1;
    float oy = ny / l1;
    if (nz < 0.0f)
    {
        float px = ox, py = oy;
        ox = (1.0f - std::abs(py)) * (px >= 0.0f ? 1.0f : -1.0f);
        oy = (1.0f - std::abs(px)) * (py >= 0.0f ? 1.0f : -1.0f);
    }
    outX = static_cast<int16_t>(glm::clamp(ox, -1.0f, 1.0f) * 32767.0f);
    outY = static_cast<int16_t>(glm::clamp(oy, -1.0f, 1.0f) * 32767.0f);
}

// ---------------------------------------------------------------------------
// Oct-encode tangent xyz + sign to 4x uint8 (same encoding as GltfLoader).
// ---------------------------------------------------------------------------

void encodeOctTangent(float tx, float ty, float tz, float sign, uint8_t& outX, uint8_t& outY,
                      uint8_t& outZ, uint8_t& outW)
{
    const float l1 = std::abs(tx) + std::abs(ty) + std::abs(tz);
    float ox = tx / l1;
    float oy = ty / l1;
    if (tz < 0.0f)
    {
        float px = ox, py = oy;
        ox = (1.0f - std::abs(py)) * (px >= 0.0f ? 1.0f : -1.0f);
        oy = (1.0f - std::abs(px)) * (py >= 0.0f ? 1.0f : -1.0f);
    }
    outX = static_cast<uint8_t>((glm::clamp(ox, -1.0f, 1.0f) + 1.0f) * 0.5f * 255.0f);
    outY = static_cast<uint8_t>((glm::clamp(oy, -1.0f, 1.0f) + 1.0f) * 0.5f * 255.0f);
    outZ = 0u;
    outW = (sign >= 0.0f) ? 255u : 0u;
}

// ---------------------------------------------------------------------------
// Generate tangents from positions, normals, UVs, and indices.
// Same algorithm as GltfLoader's tangent generation.
// ---------------------------------------------------------------------------

void generateTangents(rendering::MeshData& md, const std::vector<float>& uvFloats)
{
    const size_t vertCount = md.positions.size() / 3;
    std::vector<float> tanAccum(vertCount * 3, 0.f);
    std::vector<float> bitAccum(vertCount * 3, 0.f);

    for (size_t t = 0; t + 2 < md.indices.size(); t += 3)
    {
        const uint32_t i0 = md.indices[t + 0];
        const uint32_t i1 = md.indices[t + 1];
        const uint32_t i2 = md.indices[t + 2];

        const float* p0 = &md.positions[i0 * 3];
        const float* p1 = &md.positions[i1 * 3];
        const float* p2 = &md.positions[i2 * 3];

        const float* uv0 = &uvFloats[i0 * 2];
        const float* uv1 = &uvFloats[i1 * 2];
        const float* uv2 = &uvFloats[i2 * 2];

        const float dx1 = p1[0] - p0[0], dy1 = p1[1] - p0[1], dz1 = p1[2] - p0[2];
        const float dx2 = p2[0] - p0[0], dy2 = p2[1] - p0[1], dz2 = p2[2] - p0[2];
        const float du1 = uv1[0] - uv0[0], dv1 = uv1[1] - uv0[1];
        const float du2 = uv2[0] - uv0[0], dv2 = uv2[1] - uv0[1];

        float r = du1 * dv2 - du2 * dv1;
        if (std::abs(r) < 1e-12f)
            r = 1.0f;
        r = 1.0f / r;

        const float tx = (dx1 * dv2 - dx2 * dv1) * r;
        const float ty = (dy1 * dv2 - dy2 * dv1) * r;
        const float tz = (dz1 * dv2 - dz2 * dv1) * r;
        const float bx = (dx2 * du1 - dx1 * du2) * r;
        const float by = (dy2 * du1 - dy1 * du2) * r;
        const float bz = (dz2 * du1 - dz1 * du2) * r;

        for (uint32_t idx : {i0, i1, i2})
        {
            tanAccum[idx * 3 + 0] += tx;
            tanAccum[idx * 3 + 1] += ty;
            tanAccum[idx * 3 + 2] += tz;
            bitAccum[idx * 3 + 0] += bx;
            bitAccum[idx * 3 + 1] += by;
            bitAccum[idx * 3 + 2] += bz;
        }
    }

    // Decode normals from oct-encoded snorm16 back to float for orthonormalization.
    std::vector<float> normData(vertCount * 3);
    for (size_t i = 0; i < vertCount; ++i)
    {
        float ox = static_cast<float>(md.normals[i * 2 + 0]) / 32767.0f;
        float oy = static_cast<float>(md.normals[i * 2 + 1]) / 32767.0f;
        float oz = 1.0f - std::abs(ox) - std::abs(oy);
        if (oz < 0.0f)
        {
            float px = ox, py = oy;
            ox = (1.0f - std::abs(py)) * (px >= 0.0f ? 1.0f : -1.0f);
            oy = (1.0f - std::abs(px)) * (py >= 0.0f ? 1.0f : -1.0f);
        }
        float len = std::sqrt(ox * ox + oy * oy + oz * oz);
        if (len > 0.0f)
        {
            ox /= len;
            oy /= len;
            oz /= len;
        }
        normData[i * 3 + 0] = ox;
        normData[i * 3 + 1] = oy;
        normData[i * 3 + 2] = oz;
    }

    // Orthonormalize and encode.
    md.tangents.resize(vertCount * 4);
    for (size_t i = 0; i < vertCount; ++i)
    {
        const float nx = normData[i * 3 + 0];
        const float ny = normData[i * 3 + 1];
        const float nz = normData[i * 3 + 2];

        float ttx = tanAccum[i * 3 + 0];
        float tty = tanAccum[i * 3 + 1];
        float ttz = tanAccum[i * 3 + 2];

        // Gram-Schmidt orthogonalize.
        float ndott = nx * ttx + ny * tty + nz * ttz;
        ttx -= nx * ndott;
        tty -= ny * ndott;
        ttz -= nz * ndott;

        float tlen = std::sqrt(ttx * ttx + tty * tty + ttz * ttz);
        if (tlen < 1e-6f)
        {
            // Degenerate — pick arbitrary tangent perpendicular to normal.
            if (std::abs(nx) < 0.9f)
            {
                ttx = 0.f;
                tty = -nz;
                ttz = ny;
            }
            else
            {
                ttx = -ny;
                tty = nx;
                ttz = 0.f;
            }
            tlen = std::sqrt(ttx * ttx + tty * tty + ttz * ttz);
        }
        ttx /= tlen;
        tty /= tlen;
        ttz /= tlen;

        // Bitangent sign.
        const float cx = ny * ttz - nz * tty;
        const float cy = nz * ttx - nx * ttz;
        const float cz = nx * tty - ny * ttx;
        const float bDot =
            cx * bitAccum[i * 3 + 0] + cy * bitAccum[i * 3 + 1] + cz * bitAccum[i * 3 + 2];
        const float sign = (bDot < 0.f) ? -1.f : 1.f;

        encodeOctTangent(ttx, tty, ttz, sign, md.tangents[i * 4 + 0], md.tangents[i * 4 + 1],
                         md.tangents[i * 4 + 2], md.tangents[i * 4 + 3]);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// IAssetLoader interface
// ---------------------------------------------------------------------------

std::span<const std::string_view> ObjLoader::extensions() const
{
    return kExtensions;
}

CpuAssetData ObjLoader::decode(std::span<const uint8_t> bytes, std::string_view path,
                               IFileSystem& fs)
{
    // Parse the OBJ from memory.
    std::string objText(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream objStream(objText);

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    // Resolve MTL path relative to the OBJ file.
    // Extract directory from path.
    std::string pathStr(path);
    std::string baseDir;
    auto lastSlash = pathStr.find_last_of('/');
    if (lastSlash != std::string::npos)
        baseDir = pathStr.substr(0, lastSlash + 1);

    // Custom material reader that loads MTL files from IFileSystem.
    class FsMaterialReader : public tinyobj::MaterialReader
    {
    public:
        FsMaterialReader(IFileSystem& fs, const std::string& baseDir) : fs_(fs), baseDir_(baseDir)
        {
        }

        bool operator()(const std::string& matId, std::vector<tinyobj::material_t>* mats,
                        std::map<std::string, int>* matMap, std::string* warning,
                        std::string* error) override
        {
            std::string mtlPath = baseDir_ + matId;
            auto mtlBytes = fs_.read(mtlPath);
            if (mtlBytes.empty())
            {
                if (warning)
                    *warning += "MTL file not found: " + mtlPath + "\n";
                return false;
            }
            std::string mtlText(reinterpret_cast<const char*>(mtlBytes.data()), mtlBytes.size());
            std::istringstream mtlStream(mtlText);
            tinyobj::LoadMtl(matMap, mats, &mtlStream, warning, error);
            return true;
        }

    private:
        IFileSystem& fs_;
        std::string baseDir_;
    };

    FsMaterialReader materialReader(fs, baseDir);

    bool ok =
        tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &objStream, &materialReader);
    if (!ok)
    {
        throw std::runtime_error("ObjLoader: failed to parse '" + std::string(path) + "': " + err);
    }

    const bool hasNormals = !attrib.normals.empty();
    const bool hasUVs = !attrib.texcoords.empty();

    CpuSceneData scene;

    // -----------------------------------------------------------------------
    // Convert materials.
    // -----------------------------------------------------------------------

    for (const auto& mtl : materials)
    {
        CpuMaterialData mat;
        mat.albedo = {mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2], 1.0f};

        // Ns (specular exponent) → roughness: higher Ns = shinier = lower roughness.
        // Common mapping: roughness = 1 - sqrt(Ns / 1000).
        if (mtl.shininess > 0.0f)
        {
            mat.roughness = 1.0f - glm::clamp(std::sqrt(mtl.shininess / 1000.0f), 0.0f, 1.0f);
        }

        // OBJ doesn't have a direct metallic parameter; use a heuristic:
        // if specular color is close to diffuse, treat as metallic.
        // For simplicity, default to 0 (non-metallic).
        mat.metallic = 0.0f;

        scene.materials.push_back(std::move(mat));
    }

    // -----------------------------------------------------------------------
    // Convert shapes to meshes.
    // -----------------------------------------------------------------------

    for (size_t s = 0; s < shapes.size(); ++s)
    {
        const auto& shape = shapes[s];
        rendering::MeshData md;

        // De-index OBJ data: OBJ uses separate indices for pos/norm/uv,
        // but the engine needs unified per-vertex attributes.
        struct VertexKey
        {
            int v, vn, vt;
            bool operator==(const VertexKey& o) const
            {
                return v == o.v && vn == o.vn && vt == o.vt;
            }
        };
        struct VertexKeyHash
        {
            size_t operator()(const VertexKey& k) const
            {
                return std::hash<int>()(k.v) ^ (std::hash<int>()(k.vn) << 11) ^
                       (std::hash<int>()(k.vt) << 22);
            }
        };

        std::unordered_map<VertexKey, uint16_t, VertexKeyHash> vertexMap;
        std::vector<float> rawNormals;  // float3 per unique vertex
        std::vector<float> rawUVs;      // float2 per unique vertex

        math::Vec3 boundsMin(std::numeric_limits<float>::max());
        math::Vec3 boundsMax(std::numeric_limits<float>::lowest());

        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            const int fv = shape.mesh.num_face_vertices[f];

            // Triangulate: fan from first vertex of the face.
            for (int v = 1; v < fv - 1; ++v)
            {
                const int triIndices[3] = {0, v, v + 1};
                for (int ti = 0; ti < 3; ++ti)
                {
                    const tinyobj::index_t& idx = shape.mesh.indices[indexOffset + triIndices[ti]];

                    VertexKey key{idx.vertex_index, idx.normal_index, idx.texcoord_index};
                    auto it = vertexMap.find(key);
                    if (it != vertexMap.end())
                    {
                        md.indices.push_back(it->second);
                    }
                    else
                    {
                        if (vertexMap.size() >= 65535)
                        {
                            throw std::runtime_error(
                                "ObjLoader: mesh '" + shape.name +
                                "' exceeds 16-bit index limit (65535 vertices).");
                        }
                        auto newIdx = static_cast<uint16_t>(vertexMap.size());
                        vertexMap[key] = newIdx;

                        // Position.
                        float px = attrib.vertices[3 * idx.vertex_index + 0];
                        float py = attrib.vertices[3 * idx.vertex_index + 1];
                        float pz = attrib.vertices[3 * idx.vertex_index + 2];
                        md.positions.push_back(px);
                        md.positions.push_back(py);
                        md.positions.push_back(pz);

                        boundsMin = glm::min(boundsMin, math::Vec3(px, py, pz));
                        boundsMax = glm::max(boundsMax, math::Vec3(px, py, pz));

                        // Normal.
                        if (hasNormals && idx.normal_index >= 0)
                        {
                            rawNormals.push_back(attrib.normals[3 * idx.normal_index + 0]);
                            rawNormals.push_back(attrib.normals[3 * idx.normal_index + 1]);
                            rawNormals.push_back(attrib.normals[3 * idx.normal_index + 2]);
                        }

                        // UV.
                        if (hasUVs && idx.texcoord_index >= 0)
                        {
                            rawUVs.push_back(attrib.texcoords[2 * idx.texcoord_index + 0]);
                            rawUVs.push_back(attrib.texcoords[2 * idx.texcoord_index + 1]);
                        }

                        md.indices.push_back(newIdx);
                    }
                }
            }
            indexOffset += fv;
        }

        md.boundsMin = boundsMin;
        md.boundsMax = boundsMax;

        const size_t vertCount = md.positions.size() / 3;

        // Encode normals to oct-encoded snorm16.
        if (rawNormals.size() == vertCount * 3)
        {
            md.normals.resize(vertCount * 2);
            for (size_t i = 0; i < vertCount; ++i)
            {
                encodeOctNormal(rawNormals[i * 3 + 0], rawNormals[i * 3 + 1], rawNormals[i * 3 + 2],
                                md.normals[i * 2 + 0], md.normals[i * 2 + 1]);
            }
        }

        // Encode UVs to float16.
        std::vector<float> uvFloats;
        if (rawUVs.size() == vertCount * 2)
        {
            uvFloats = rawUVs;
        }
        else if (!md.normals.empty())
        {
            // No UVs but normals exist — generate zero UVs so the surface
            // buffer can be created.
            uvFloats.resize(vertCount * 2, 0.0f);
        }

        if (!uvFloats.empty())
        {
            md.uvs.resize(vertCount * 2);
            for (size_t i = 0; i < vertCount; ++i)
            {
                const uint32_t packed =
                    glm::packHalf2x16(glm::vec2(uvFloats[i * 2 + 0], uvFloats[i * 2 + 1]));
                md.uvs[i * 2 + 0] = static_cast<uint16_t>(packed & 0xFFFFu);
                md.uvs[i * 2 + 1] = static_cast<uint16_t>(packed >> 16u);
            }
        }

        // Generate tangents when normals + UVs are available.
        if (!md.normals.empty() && !uvFloats.empty() && !md.indices.empty())
        {
            generateTangents(md, uvFloats);
        }

        scene.meshes.push_back(std::move(md));

        // Create a scene node for this shape.
        CpuSceneData::Node node;
        node.name = shape.name;
        node.localTransform = math::Mat4(1.0f);
        node.meshIndex = static_cast<int32_t>(s);

        // Assign material from the first face of the shape.
        if (!shape.mesh.material_ids.empty() && shape.mesh.material_ids[0] >= 0)
        {
            node.materialIndex = shape.mesh.material_ids[0];
        }

        scene.nodes.push_back(std::move(node));
        scene.rootNodeIndices.push_back(static_cast<uint32_t>(scene.nodes.size() - 1));
    }

    return scene;
}

}  // namespace engine::assets
