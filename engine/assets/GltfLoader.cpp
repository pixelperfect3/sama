#include "engine/assets/GltfLoader.h"

#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "engine/assets/CpuAssetData.h"
#include "engine/assets/IFileSystem.h"
#include "engine/math/Types.h"

// cgltf — single-header glTF 2.0 parser.  Implementation compiled here only.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// stb_image for external/embedded texture decoding.
// STB_IMAGE_IMPLEMENTATION is defined in TextureLoader.cpp; include only
// the declarations here.
#include <stb_image.h>

namespace engine::assets
{

namespace
{

constexpr std::string_view kExtensions[] = {".gltf", ".glb"};

// ---------------------------------------------------------------------------
// FNV-1a hash for joint name strings (32-bit).
// ---------------------------------------------------------------------------

uint32_t fnv1aHash(const char* str)
{
    uint32_t hash = 2166136261u;
    while (*str)
    {
        hash ^= static_cast<uint32_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Decode an embedded or externally referenced image into CpuTextureData.
// ---------------------------------------------------------------------------

CpuTextureData decodeImage(const cgltf_image& img, std::string_view gltfPath, IFileSystem& fs)
{
    std::vector<uint8_t> raw;

    if (img.buffer_view)
    {
        // Embedded image: data lives inside a GLB buffer view.
        const cgltf_buffer_view* bv = img.buffer_view;
        const uint8_t* src = static_cast<const uint8_t*>(bv->buffer->data) + bv->offset;
        raw.assign(src, src + bv->size);
    }
    else if (img.uri && std::strncmp(img.uri, "data:", 5) == 0)
    {
        // Data URI (base64-encoded image embedded in the JSON).
        // cgltf does NOT decode data URIs for images automatically — that is the
        // application's responsibility. Returning empty here would silently produce
        // a failed texture; throw so the caller gets a clear error instead.
        // TODO: base64-decode img.uri and pass the raw bytes to stbi_load_from_memory.
        throw std::runtime_error(
            "GltfLoader: data URI images are not yet supported. "
            "Export the glTF with separate image files instead.");
    }
    else if (img.uri)
    {
        // External image file: resolve relative to the glTF directory.
        const std::string resolved = fs.resolve(gltfPath, img.uri);
        raw = fs.read(resolved);
        if (raw.empty())
            return {};
    }
    else
    {
        return {};
    }

    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load_from_memory(raw.data(), static_cast<int>(raw.size()), &w, &h, &ch,
                                            STBI_rgb_alpha);
    if (!pixels)
        return {};

    CpuTextureData tex;
    tex.width = static_cast<uint32_t>(w);
    tex.height = static_cast<uint32_t>(h);
    tex.pixels.assign(pixels, pixels + (w * h * 4));
    stbi_image_free(pixels);
    return tex;
}

// ---------------------------------------------------------------------------
// Build a local-space TRS matrix from a cgltf_node.
// ---------------------------------------------------------------------------

math::Mat4 nodeLocalTransform(const cgltf_node& node)
{
    if (node.has_matrix)
    {
        math::Mat4 m;
        std::memcpy(&m[0][0], node.matrix, sizeof(float) * 16);
        return m;
    }

    math::Mat4 m(1.0f);
    if (node.has_scale)
        m = glm::scale(m, {node.scale[0], node.scale[1], node.scale[2]});
    if (node.has_rotation)
    {
        glm::quat q(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
        m = glm::mat4_cast(q) * m;
    }
    if (node.has_translation)
        m = glm::translate(glm::mat4(1.0f),
                           {node.translation[0], node.translation[1], node.translation[2]}) *
            m;
    return m;
}

// ---------------------------------------------------------------------------
// Decode accessor data into a flat float vector. Returns an empty vector
// if the accessor is null or has an unexpected component type.
// ---------------------------------------------------------------------------

std::vector<float> readFloatAccessor(const cgltf_accessor* acc)
{
    if (!acc)
        return {};

    const size_t numComponents = cgltf_num_components(acc->type);
    const size_t count = acc->count;
    std::vector<float> out(count * numComponents);

    for (size_t i = 0; i < count; ++i)
        cgltf_accessor_read_float(acc, i, out.data() + i * numComponents, numComponents);
    return out;
}

std::vector<uint32_t> readIndexAccessor(const cgltf_accessor* acc)
{
    if (!acc)
        return {};

    std::vector<uint32_t> out(acc->count);
    for (size_t i = 0; i < acc->count; ++i)
        cgltf_accessor_read_uint(acc, i, &out[i], 1);
    return out;
}

// ---------------------------------------------------------------------------
// Encode a single float3 normal to oct-encoded snorm16 pair.
// Matches the existing VertexLayouts convention in the engine.
// ---------------------------------------------------------------------------

void encodeOctNormal(float nx, float ny, float nz, int16_t& outX, int16_t& outY)
{
    // Project onto L1 octahedron.
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
// Encode a single tangent (float4: xyz + sign) to 3× uint8 + sign byte.
// Matches the engine's uint8 normalized tangent encoding.
// ---------------------------------------------------------------------------

void encodeOctTangent(float tx, float ty, float tz, float sign, uint8_t& outX, uint8_t& outY,
                      uint8_t& outZ, uint8_t& outW)
{
    // Oct-encode the tangent xyz.
    // Engine encoding: [0,255] → shader reads as 2*v-1 (via bgfx Uint8 normalized).
    const float l1 = std::abs(tx) + std::abs(ty) + std::abs(tz);
    float ox = tx / l1;
    float oy = ty / l1;
    if (tz < 0.0f)
    {
        float px = ox, py = oy;
        ox = (1.0f - std::abs(py)) * (px >= 0.0f ? 1.0f : -1.0f);
        oy = (1.0f - std::abs(px)) * (py >= 0.0f ? 1.0f : -1.0f);
    }
    // Remap from [-1,1] to [0,255]: v' = (v + 1) / 2 * 255
    outX = static_cast<uint8_t>((glm::clamp(ox, -1.0f, 1.0f) + 1.0f) * 0.5f * 255.0f);
    outY = static_cast<uint8_t>((glm::clamp(oy, -1.0f, 1.0f) + 1.0f) * 0.5f * 255.0f);
    outZ = 0u;  // padding — vs_pbr.sc reads only .xy (oct) and .w (sign); .z is ignored
    outW = (sign >= 0.0f) ? 255u : 0u;
}

// ---------------------------------------------------------------------------
// Convert one cgltf_primitive to MeshData.
// ---------------------------------------------------------------------------

rendering::MeshData convertPrimitive(const cgltf_primitive& prim)
{
    rendering::MeshData md;

    // Find attribute accessors.
    const cgltf_accessor* posAcc = nullptr;
    const cgltf_accessor* normAcc = nullptr;
    const cgltf_accessor* tanAcc = nullptr;
    const cgltf_accessor* uvAcc = nullptr;
    const cgltf_accessor* jointsAcc = nullptr;
    const cgltf_accessor* weightsAcc = nullptr;

    for (size_t a = 0; a < prim.attributes_count; ++a)
    {
        const auto& attr = prim.attributes[a];
        switch (attr.type)
        {
            case cgltf_attribute_type_position:
                posAcc = attr.data;
                break;
            case cgltf_attribute_type_normal:
                normAcc = attr.data;
                break;
            case cgltf_attribute_type_tangent:
                tanAcc = attr.data;
                break;
            case cgltf_attribute_type_texcoord:
                if (attr.index == 0)
                    uvAcc = attr.data;
                break;
            case cgltf_attribute_type_joints:
                if (attr.index == 0)
                    jointsAcc = attr.data;
                break;
            case cgltf_attribute_type_weights:
                if (attr.index == 0)
                    weightsAcc = attr.data;
                break;
            default:
                break;
        }
    }

    if (!posAcc)
        return md;

    const size_t vertCount = posAcc->count;

    // Positions — raw float3 stream.
    auto posData = readFloatAccessor(posAcc);
    md.positions.resize(posData.size());
    std::copy(posData.begin(), posData.end(), md.positions.begin());

    // Bounding box.
    if (posAcc->has_min && posAcc->has_max)
    {
        md.boundsMin = {posAcc->min[0], posAcc->min[1], posAcc->min[2]};
        md.boundsMax = {posAcc->max[0], posAcc->max[1], posAcc->max[2]};
    }

    // Surface attributes — normals, tangents, UVs.
    if (normAcc)
    {
        auto normData = readFloatAccessor(normAcc);
        md.normals.resize(vertCount * 2);
        for (size_t i = 0; i < vertCount; ++i)
        {
            encodeOctNormal(normData[i * 3 + 0], normData[i * 3 + 1], normData[i * 3 + 2],
                            md.normals[i * 2 + 0], md.normals[i * 2 + 1]);
        }
    }

    if (tanAcc)
    {
        auto tanData = readFloatAccessor(tanAcc);  // float4: xyz + sign
        md.tangents.resize(vertCount * 4);
        for (size_t i = 0; i < vertCount; ++i)
        {
            encodeOctTangent(tanData[i * 4 + 0], tanData[i * 4 + 1], tanData[i * 4 + 2],
                             tanData[i * 4 + 3], md.tangents[i * 4 + 0], md.tangents[i * 4 + 1],
                             md.tangents[i * 4 + 2], md.tangents[i * 4 + 3]);
        }
    }

    std::vector<float> uvData;
    if (uvAcc)
    {
        uvData = readFloatAccessor(uvAcc);
    }
    else if (normAcc)
    {
        // No UVs in the mesh — generate zero UVs so the surface buffer
        // can be created (normals + tangents + UVs are all required).
        uvData.resize(vertCount * 2, 0.0f);
    }

    if (!uvData.empty())
    {
        md.uvs.resize(vertCount * 2);
        for (size_t i = 0; i < vertCount; ++i)
        {
            const uint32_t packed =
                glm::packHalf2x16(glm::vec2(uvData[i * 2 + 0], uvData[i * 2 + 1]));
            md.uvs[i * 2 + 0] = static_cast<uint16_t>(packed & 0xFFFFu);
            md.uvs[i * 2 + 1] = static_cast<uint16_t>(packed >> 16u);
        }
    }

    // Indices — stored as uint16. Meshes with > 65535 vertices are not supported.
    auto idx32 = readIndexAccessor(prim.indices);
    md.indices.reserve(idx32.size());
    for (uint32_t i : idx32)
    {
        if (i > 0xFFFFu)
            throw std::runtime_error("GltfLoader: mesh index " + std::to_string(i) +
                                     " exceeds uint16 max (65535). "
                                     "Split the mesh or add uint32 index support.");
        md.indices.push_back(static_cast<uint16_t>(i));
    }

    // -----------------------------------------------------------------------
    // Generate tangents from positions, normals, and UVs when the glTF mesh
    // does not provide TANGENT attributes (common for many sample models).
    // Uses a standard per-triangle tangent computation averaged at vertices.
    // -----------------------------------------------------------------------
    if (!tanAcc && normAcc && !uvData.empty() && !md.indices.empty())
    {
        const auto& posData = md.positions;  // float×3 per vertex
        // uvData was captured earlier as float×2 per vertex

        // Accumulate per-vertex tangent/bitangent in world space.
        std::vector<float> tanAccum(vertCount * 3, 0.f);
        std::vector<float> bitAccum(vertCount * 3, 0.f);

        for (size_t t = 0; t + 2 < idx32.size(); t += 3)
        {
            const uint32_t i0 = idx32[t + 0], i1 = idx32[t + 1], i2 = idx32[t + 2];

            const float* p0 = &posData[i0 * 3];
            const float* p1 = &posData[i1 * 3];
            const float* p2 = &posData[i2 * 3];

            const float* uv0 = &uvData[i0 * 2];
            const float* uv1 = &uvData[i1 * 2];
            const float* uv2 = &uvData[i2 * 2];

            const float e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
            const float e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];

            const float du1 = uv1[0] - uv0[0], dv1 = uv1[1] - uv0[1];
            const float du2 = uv2[0] - uv0[0], dv2 = uv2[1] - uv0[1];

            float det = du1 * dv2 - du2 * dv1;
            if (std::abs(det) < 1e-8f)
                det = 1e-8f;
            const float invDet = 1.f / det;

            const float tx = (dv2 * e1x - dv1 * e2x) * invDet;
            const float ty = (dv2 * e1y - dv1 * e2y) * invDet;
            const float tz = (dv2 * e1z - dv1 * e2z) * invDet;

            const float bx = (-du2 * e1x + du1 * e2x) * invDet;
            const float by = (-du2 * e1y + du1 * e2y) * invDet;
            const float bz = (-du2 * e1z + du1 * e2z) * invDet;

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

        // Orthonormalize and encode.
        auto normData = readFloatAccessor(normAcc);
        md.tangents.resize(vertCount * 4);
        for (size_t i = 0; i < vertCount; ++i)
        {
            const float nx = normData[i * 3 + 0];
            const float ny = normData[i * 3 + 1];
            const float nz = normData[i * 3 + 2];

            float tx = tanAccum[i * 3 + 0];
            float ty = tanAccum[i * 3 + 1];
            float tz = tanAccum[i * 3 + 2];

            // Gram-Schmidt orthogonalize: T = normalize(T - N * dot(N, T))
            const float ndott = nx * tx + ny * ty + nz * tz;
            tx -= nx * ndott;
            ty -= ny * ndott;
            tz -= nz * ndott;

            float len = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (len < 1e-6f)
            {
                // Degenerate — pick arbitrary tangent perpendicular to normal.
                if (std::abs(nx) < 0.9f)
                {
                    tx = 0.f;
                    ty = -nz;
                    tz = ny;
                }
                else
                {
                    tx = nz;
                    ty = 0.f;
                    tz = -nx;
                }
                len = std::sqrt(tx * tx + ty * ty + tz * tz);
            }
            tx /= len;
            ty /= len;
            tz /= len;

            // Bitangent sign: sign of dot(cross(N, T), B)
            const float cx = ny * tz - nz * ty;
            const float cy = nz * tx - nx * tz;
            const float cz = nx * ty - ny * tx;
            const float bDot =
                cx * bitAccum[i * 3 + 0] + cy * bitAccum[i * 3 + 1] + cz * bitAccum[i * 3 + 2];
            const float sign = (bDot < 0.f) ? -1.f : 1.f;

            encodeOctTangent(tx, ty, tz, sign, md.tangents[i * 4 + 0], md.tangents[i * 4 + 1],
                             md.tangents[i * 4 + 2], md.tangents[i * 4 + 3]);
        }
    }

    // -----------------------------------------------------------------------
    // Skinning attributes: JOINTS_0 and WEIGHTS_0
    // -----------------------------------------------------------------------
    if (jointsAcc && weightsAcc)
    {
        // Read joints as uint (indices into the skin's joint array).
        md.boneIndices.resize(vertCount * 4);
        for (size_t i = 0; i < vertCount; ++i)
        {
            cgltf_uint joints[4] = {};
            cgltf_accessor_read_uint(jointsAcc, i, joints, 4);
            md.boneIndices[i * 4 + 0] = static_cast<uint8_t>(joints[0]);
            md.boneIndices[i * 4 + 1] = static_cast<uint8_t>(joints[1]);
            md.boneIndices[i * 4 + 2] = static_cast<uint8_t>(joints[2]);
            md.boneIndices[i * 4 + 3] = static_cast<uint8_t>(joints[3]);
        }

        // Read weights as float, then quantize to uint8.
        auto weightData = readFloatAccessor(weightsAcc);
        md.boneWeights.resize(vertCount * 4);
        for (size_t i = 0; i < vertCount; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                float w = weightData[i * 4 + j];
                md.boneWeights[i * 4 + j] =
                    static_cast<uint8_t>(glm::clamp(w, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }

    return md;
}

// ---------------------------------------------------------------------------
// Extract skeleton data from cgltf_skin.
// ---------------------------------------------------------------------------

struct SkeletonExtraction
{
    CpuSkeletonData skeleton;
    // Remap from original skin joint index to topologically sorted index.
    // Used to fix up vertex JOINTS_0 attributes and animation channel indices.
    std::vector<int32_t> remap;
};

SkeletonExtraction extractSkeleton(const cgltf_data* data, const cgltf_skin& skin)
{
    SkeletonExtraction result;
    auto& skelData = result.skeleton;

    // Read inverse bind matrices.
    std::vector<float> ibmData;
    if (skin.inverse_bind_matrices)
        ibmData = readFloatAccessor(skin.inverse_bind_matrices);

    // Build a map from cgltf_node* to index in the skin's joint array.
    std::unordered_map<const cgltf_node*, size_t> jointNodeToIdx;
    for (size_t i = 0; i < skin.joints_count; ++i)
        jointNodeToIdx[skin.joints[i]] = i;

    // Build initial joint data (not yet topologically sorted).
    struct RawJoint
    {
        size_t originalIndex;
        math::Mat4 ibm{1.0f};
        int32_t parentOriginal = -1;  // index into skin.joints[]
        std::string name;
    };
    std::vector<RawJoint> rawJoints(skin.joints_count);

    for (size_t i = 0; i < skin.joints_count; ++i)
    {
        rawJoints[i].originalIndex = i;
        rawJoints[i].name = skin.joints[i]->name ? skin.joints[i]->name : "";

        if (!ibmData.empty() && (i + 1) * 16 <= ibmData.size())
            std::memcpy(&rawJoints[i].ibm[0][0], &ibmData[i * 16], sizeof(float) * 16);

        // Find parent: walk up the glTF node tree until we hit another joint
        // in this skin.
        const cgltf_node* parent = skin.joints[i]->parent;
        while (parent)
        {
            auto it = jointNodeToIdx.find(parent);
            if (it != jointNodeToIdx.end())
            {
                rawJoints[i].parentOriginal = static_cast<int32_t>(it->second);
                break;
            }
            parent = parent->parent;
        }
    }

    // Topological sort: parent always comes before child.
    // Simple Kahn's algorithm.
    std::vector<size_t> sortedOrder;
    sortedOrder.reserve(skin.joints_count);
    std::vector<bool> emitted(skin.joints_count, false);

    // Repeatedly scan for joints whose parent is already emitted (or is -1).
    while (sortedOrder.size() < skin.joints_count)
    {
        bool progress = false;
        for (size_t i = 0; i < skin.joints_count; ++i)
        {
            if (emitted[i])
                continue;
            int32_t p = rawJoints[i].parentOriginal;
            if (p < 0 || emitted[static_cast<size_t>(p)])
            {
                sortedOrder.push_back(i);
                emitted[i] = true;
                progress = true;
            }
        }
        if (!progress)
        {
            // Cyclic reference -- emit remaining joints as roots.
            for (size_t i = 0; i < skin.joints_count; ++i)
                if (!emitted[i])
                    sortedOrder.push_back(i);
            break;
        }
    }

    // Build remapping: originalIndex -> sortedIndex.
    result.remap.resize(skin.joints_count, -1);
    for (size_t i = 0; i < sortedOrder.size(); ++i)
        result.remap[sortedOrder[i]] = static_cast<int32_t>(i);

    // Build final sorted joint data.
    skelData.joints.resize(skin.joints_count);
    for (size_t i = 0; i < sortedOrder.size(); ++i)
    {
        const auto& raw = rawJoints[sortedOrder[i]];
        auto& dst = skelData.joints[i];
        dst.inverseBindMatrix = raw.ibm;
        dst.name = raw.name;
        dst.parentIndex = (raw.parentOriginal >= 0) ? result.remap[raw.parentOriginal] : -1;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Extract animation clips from cgltf_animation.
// ---------------------------------------------------------------------------

std::vector<CpuAnimationClipData> extractAnimations(const cgltf_data* data, const cgltf_skin& skin,
                                                    const std::vector<int32_t>& jointRemap)
{
    // Build a map from cgltf_node* to the *remapped* (sorted) joint index.
    std::unordered_map<const cgltf_node*, uint32_t> nodeToJoint;
    for (size_t i = 0; i < skin.joints_count; ++i)
    {
        int32_t sorted = (i < jointRemap.size()) ? jointRemap[i] : static_cast<int32_t>(i);
        nodeToJoint[skin.joints[i]] = static_cast<uint32_t>(sorted);
    }

    std::vector<CpuAnimationClipData> clips;
    clips.reserve(data->animations_count);

    for (size_t ai = 0; ai < data->animations_count; ++ai)
    {
        const cgltf_animation& anim = data->animations[ai];
        CpuAnimationClipData clip;
        clip.name = anim.name ? anim.name : "animation_" + std::to_string(ai);
        clip.duration = 0.0f;

        // Group channels by joint index.
        std::unordered_map<uint32_t, CpuAnimationClipData::Channel> channelMap;

        for (size_t ci = 0; ci < anim.channels_count; ++ci)
        {
            const cgltf_animation_channel& ch = anim.channels[ci];
            if (!ch.target_node)
                continue;

            auto it = nodeToJoint.find(ch.target_node);
            if (it == nodeToJoint.end())
                continue;  // not a joint in this skin

            uint32_t jointIdx = it->second;
            auto& dst = channelMap[jointIdx];
            dst.jointIndex = jointIdx;

            const cgltf_animation_sampler* sampler = ch.sampler;
            if (!sampler || !sampler->input || !sampler->output)
                continue;

            auto times = readFloatAccessor(sampler->input);
            auto values = readFloatAccessor(sampler->output);

            // Update clip duration.
            for (float t : times)
                if (t > clip.duration)
                    clip.duration = t;

            switch (ch.target_path)
            {
                case cgltf_animation_path_type_translation:
                {
                    dst.positionTimes = std::move(times);
                    dst.positionValues.resize(dst.positionTimes.size());
                    for (size_t k = 0; k < dst.positionValues.size(); ++k)
                    {
                        dst.positionValues[k] = {values[k * 3 + 0], values[k * 3 + 1],
                                                 values[k * 3 + 2]};
                    }
                    break;
                }
                case cgltf_animation_path_type_rotation:
                {
                    dst.rotationTimes = std::move(times);
                    dst.rotationValues.resize(dst.rotationTimes.size());
                    for (size_t k = 0; k < dst.rotationValues.size(); ++k)
                    {
                        // glTF stores quaternions as (x, y, z, w).
                        // GLM constructor is (w, x, y, z).
                        dst.rotationValues[k] = math::Quat(values[k * 4 + 3], values[k * 4 + 0],
                                                           values[k * 4 + 1], values[k * 4 + 2]);
                    }
                    break;
                }
                case cgltf_animation_path_type_scale:
                {
                    dst.scaleTimes = std::move(times);
                    dst.scaleValues.resize(dst.scaleTimes.size());
                    for (size_t k = 0; k < dst.scaleValues.size(); ++k)
                    {
                        dst.scaleValues[k] = {values[k * 3 + 0], values[k * 3 + 1],
                                              values[k * 3 + 2]};
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // Flatten channel map into the clip's channel vector.
        clip.channels.reserve(channelMap.size());
        for (auto& [idx, ch] : channelMap)
            clip.channels.push_back(std::move(ch));

        clips.push_back(std::move(clip));
    }

    return clips;
}

// ---------------------------------------------------------------------------
// Recurse over cgltf_node tree, building CpuSceneData nodes.
// ---------------------------------------------------------------------------

void buildNodeTree(const cgltf_data* data, const cgltf_node* node, CpuSceneData& scene,
                   uint32_t parentIndex, bool isRoot,
                   const std::unordered_set<const cgltf_node*>& jointNodes)
{
    const uint32_t nodeIndex = static_cast<uint32_t>(scene.nodes.size());
    scene.nodes.emplace_back();
    CpuSceneData::Node& dst = scene.nodes.back();

    dst.name = node->name ? node->name : "";
    dst.localTransform = nodeLocalTransform(*node);
    dst.isJoint = jointNodes.count(node) > 0;

    if (node->mesh)
    {
        // Map cgltf mesh pointer to our local mesh index.
        // All primitives are merged into one MeshData per glTF mesh.
        const ptrdiff_t meshIdx = node->mesh - data->meshes;
        dst.meshIndex = static_cast<int32_t>(meshIdx);
    }

    if (node->mesh && node->mesh->primitives_count > 0)
    {
        const cgltf_primitive& prim = node->mesh->primitives[0];
        if (prim.material)
        {
            const ptrdiff_t matIdx = prim.material - data->materials;
            dst.materialIndex = static_cast<int32_t>(matIdx);
        }
    }

    if (isRoot)
        scene.rootNodeIndices.push_back(nodeIndex);
    else
        scene.nodes[parentIndex].childIndices.push_back(nodeIndex);

    for (size_t c = 0; c < node->children_count; ++c)
        buildNodeTree(data, node->children[c], scene, nodeIndex, false, jointNodes);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// GltfLoader public interface
// ---------------------------------------------------------------------------

std::span<const std::string_view> GltfLoader::extensions() const
{
    return kExtensions;
}

CpuAssetData GltfLoader::decode(std::span<const uint8_t> bytes, std::string_view path,
                                IFileSystem& fs)
{
    cgltf_options opts{};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse(&opts, bytes.data(), bytes.size(), &data);
    if (result != cgltf_result_success)
        throw std::runtime_error("GltfLoader: cgltf_parse failed for '" + std::string(path) + "'");

    // Load external buffers (.bin files for .gltf; embedded for .glb).
    // We implement this manually via IFileSystem rather than via cgltf_load_buffers
    // (which uses libc fopen and bypasses our abstraction).
    //
    // cgltf_parse does NOT populate buf.data for any buffer type — that is
    // cgltf_load_buffers' responsibility.  We handle two cases:
    //
    //   • GLB embedded buffer (uri == null): data lives in data->bin.
    //     Mirror what cgltf_load_buffers does: point buf.data at data->bin and
    //     set data_free_method = none so cgltf_free() does not double-free it.
    //
    //   • External binary file (uri != null): read via IFileSystem, malloc a
    //     copy, and let cgltf_free() release it via memory_free.
    for (size_t i = 0; i < data->buffers_count; ++i)
    {
        cgltf_buffer& buf = data->buffers[i];
        if (buf.data)
            continue;  // already populated (shouldn't happen after cgltf_parse, but be safe)

        if (!buf.uri)
        {
            // GLB embedded binary chunk.
            if (data->bin && data->bin_size >= buf.size)
            {
                buf.data = const_cast<void*>(data->bin);
                buf.data_free_method =
                    cgltf_data_free_method_none;  // cgltf_free must not free this
            }
            continue;
        }

        const std::string binPath = fs.resolve(path, buf.uri);
        auto binBytes = fs.read(binPath);
        if (binBytes.empty())
        {
            cgltf_free(data);
            throw std::runtime_error("GltfLoader: missing external buffer '" + binPath + "'");
        }
        // cgltf_free() calls free() on buf.data when data_free_method == memory_free
        // (the default for opts = {}).  malloc here is the matching counterpart.
        buf.data = std::malloc(binBytes.size());
        if (!buf.data)
        {
            cgltf_free(data);
            throw std::runtime_error("GltfLoader: out of memory loading buffer");
        }
        std::memcpy(buf.data, binBytes.data(), binBytes.size());
        buf.size = binBytes.size();
    }

    if (cgltf_validate(data) != cgltf_result_success)
    {
        cgltf_free(data);
        throw std::runtime_error("GltfLoader: cgltf_validate failed for '" + std::string(path) +
                                 "'");
    }

    CpuSceneData scene;

    // ----- Textures -----
    scene.textures.reserve(data->images_count);
    for (size_t i = 0; i < data->images_count; ++i)
        scene.textures.push_back(decodeImage(data->images[i], path, fs));

    // ----- Skeletons (skins) — extract first so we have joint remaps -----
    std::vector<SkeletonExtraction> skelExtractions;
    skelExtractions.reserve(data->skins_count);
    scene.skeletons.reserve(data->skins_count);
    for (size_t i = 0; i < data->skins_count; ++i)
    {
        skelExtractions.push_back(extractSkeleton(data, data->skins[i]));
        scene.skeletons.push_back(std::move(skelExtractions.back().skeleton));
    }

    // ----- Per-mesh skin indices -----
    // Build a mapping from cgltf mesh to skin index. Walk the node tree and
    // record which skin each mesh-bearing node uses.
    scene.meshSkinIndices.resize(data->meshes_count, -1);
    for (size_t i = 0; i < data->nodes_count; ++i)
    {
        const cgltf_node& node = data->nodes[i];
        if (node.mesh && node.skin)
        {
            const ptrdiff_t meshIdx = node.mesh - data->meshes;
            const ptrdiff_t skinIdx = node.skin - data->skins;
            if (meshIdx >= 0 && static_cast<size_t>(meshIdx) < data->meshes_count)
                scene.meshSkinIndices[meshIdx] = static_cast<int32_t>(skinIdx);
        }
    }

    // ----- Meshes -----
    // Merge all primitives of each mesh into a single MeshData by concatenating
    // vertex/index arrays with proper index offsets. This handles models like
    // BrainStem which have many primitives in one mesh.
    scene.meshes.reserve(data->meshes_count);
    for (size_t i = 0; i < data->meshes_count; ++i)
    {
        const cgltf_mesh& gltfMesh = data->meshes[i];
        if (gltfMesh.primitives_count == 0)
        {
            scene.meshes.emplace_back();  // empty placeholder
            continue;
        }

        // Convert first primitive as the base.
        rendering::MeshData merged = convertPrimitive(gltfMesh.primitives[0]);

        // Merge remaining primitives.
        for (size_t pi = 1; pi < gltfMesh.primitives_count; ++pi)
        {
            rendering::MeshData prim = convertPrimitive(gltfMesh.primitives[pi]);
            if (prim.positions.empty())
                continue;

            // Current vertex count before merging (positions are float3).
            const uint32_t baseVertex = static_cast<uint32_t>(merged.positions.size() / 3);

            // Append positions.
            merged.positions.insert(merged.positions.end(), prim.positions.begin(),
                                    prim.positions.end());

            // Append normals (if both have them).
            if (!merged.normals.empty() && !prim.normals.empty())
            {
                merged.normals.insert(merged.normals.end(), prim.normals.begin(),
                                      prim.normals.end());
            }

            // Append tangents.
            if (!merged.tangents.empty() && !prim.tangents.empty())
            {
                merged.tangents.insert(merged.tangents.end(), prim.tangents.begin(),
                                       prim.tangents.end());
            }

            // Append UVs.
            if (!merged.uvs.empty() && !prim.uvs.empty())
            {
                merged.uvs.insert(merged.uvs.end(), prim.uvs.begin(), prim.uvs.end());
            }

            // Append bone indices.
            if (!merged.boneIndices.empty() && !prim.boneIndices.empty())
            {
                merged.boneIndices.insert(merged.boneIndices.end(), prim.boneIndices.begin(),
                                          prim.boneIndices.end());
            }

            // Append bone weights.
            if (!merged.boneWeights.empty() && !prim.boneWeights.empty())
            {
                merged.boneWeights.insert(merged.boneWeights.end(), prim.boneWeights.begin(),
                                          prim.boneWeights.end());
            }

            // Append indices with offset.
            for (uint16_t idx : prim.indices)
            {
                uint32_t newIdx = static_cast<uint32_t>(idx) + baseVertex;
                if (newIdx > 0xFFFFu)
                {
                    throw std::runtime_error(
                        "GltfLoader: merged primitive index exceeds uint16 max");
                }
                merged.indices.push_back(static_cast<uint16_t>(newIdx));
            }

            // Expand bounding box.
            merged.boundsMin = glm::min(merged.boundsMin, prim.boundsMin);
            merged.boundsMax = glm::max(merged.boundsMax, prim.boundsMax);
        }

        // If tangents are empty after merge but normals and UVs exist,
        // generate tangents for the merged mesh (same algorithm as convertPrimitive).
        {
            const uint32_t mergedVertCount = static_cast<uint32_t>(merged.positions.size() / 3);
            if (merged.tangents.empty() && !merged.normals.empty() && !merged.uvs.empty() &&
                !merged.indices.empty())
            {
                // Decode UVs from half-float back to float for tangent generation.
                std::vector<float> uvFloats(mergedVertCount * 2);
                for (uint32_t vi = 0; vi < mergedVertCount; ++vi)
                {
                    const uint32_t packed = static_cast<uint32_t>(merged.uvs[vi * 2 + 0]) |
                                            (static_cast<uint32_t>(merged.uvs[vi * 2 + 1]) << 16u);
                    const glm::vec2 uv = glm::unpackHalf2x16(packed);
                    uvFloats[vi * 2 + 0] = uv.x;
                    uvFloats[vi * 2 + 1] = uv.y;
                }

                // Decode normals from oct-encoded snorm16 back to float for tangent gen.
                auto normData = std::vector<float>(mergedVertCount * 3);
                for (uint32_t vi = 0; vi < mergedVertCount; ++vi)
                {
                    float ox = static_cast<float>(merged.normals[vi * 2 + 0]) / 32767.0f;
                    float oy = static_cast<float>(merged.normals[vi * 2 + 1]) / 32767.0f;
                    float oz = 1.0f - std::abs(ox) - std::abs(oy);
                    if (oz < 0.0f)
                    {
                        float px = ox, py = oy;
                        ox = (1.0f - std::abs(py)) * (px >= 0.0f ? 1.0f : -1.0f);
                        oy = (1.0f - std::abs(px)) * (py >= 0.0f ? 1.0f : -1.0f);
                    }
                    float len = std::sqrt(ox * ox + oy * oy + oz * oz);
                    if (len > 1e-6f)
                    {
                        ox /= len;
                        oy /= len;
                        oz /= len;
                    }
                    normData[vi * 3 + 0] = ox;
                    normData[vi * 3 + 1] = oy;
                    normData[vi * 3 + 2] = oz;
                }

                // Same tangent generation as convertPrimitive.
                std::vector<uint32_t> idx32(merged.indices.begin(), merged.indices.end());
                std::vector<float> tanAccum(mergedVertCount * 3, 0.f);
                std::vector<float> bitAccum(mergedVertCount * 3, 0.f);

                for (size_t t = 0; t + 2 < idx32.size(); t += 3)
                {
                    const uint32_t i0 = idx32[t], i1 = idx32[t + 1], i2 = idx32[t + 2];
                    const float* p0 = &merged.positions[i0 * 3];
                    const float* p1 = &merged.positions[i1 * 3];
                    const float* p2 = &merged.positions[i2 * 3];
                    const float* uv0 = &uvFloats[i0 * 2];
                    const float* uv1 = &uvFloats[i1 * 2];
                    const float* uv2 = &uvFloats[i2 * 2];

                    const float e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
                    const float e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];
                    const float du1 = uv1[0] - uv0[0], dv1 = uv1[1] - uv0[1];
                    const float du2 = uv2[0] - uv0[0], dv2 = uv2[1] - uv0[1];

                    float det = du1 * dv2 - du2 * dv1;
                    if (std::abs(det) < 1e-8f)
                        det = 1e-8f;
                    const float inv = 1.f / det;

                    const float tx = (dv2 * e1x - dv1 * e2x) * inv;
                    const float ty = (dv2 * e1y - dv1 * e2y) * inv;
                    const float tz = (dv2 * e1z - dv1 * e2z) * inv;
                    const float bx = (-du2 * e1x + du1 * e2x) * inv;
                    const float by = (-du2 * e1y + du1 * e2y) * inv;
                    const float bz = (-du2 * e1z + du1 * e2z) * inv;

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

                merged.tangents.resize(mergedVertCount * 4);
                for (size_t vi = 0; vi < mergedVertCount; ++vi)
                {
                    float nx = normData[vi * 3], ny = normData[vi * 3 + 1],
                          nz = normData[vi * 3 + 2];
                    float ttx = tanAccum[vi * 3], tty = tanAccum[vi * 3 + 1],
                          ttz = tanAccum[vi * 3 + 2];
                    float nd = nx * ttx + ny * tty + nz * ttz;
                    ttx -= nx * nd;
                    tty -= ny * nd;
                    ttz -= nz * nd;
                    float tlen = std::sqrt(ttx * ttx + tty * tty + ttz * ttz);
                    if (tlen < 1e-6f)
                    {
                        if (std::abs(nx) < 0.9f)
                        {
                            ttx = 0;
                            tty = -nz;
                            ttz = ny;
                        }
                        else
                        {
                            ttx = nz;
                            tty = 0;
                            ttz = -nx;
                        }
                        tlen = std::sqrt(ttx * ttx + tty * tty + ttz * ttz);
                    }
                    ttx /= tlen;
                    tty /= tlen;
                    ttz /= tlen;
                    float cx = ny * ttz - nz * tty, cy = nz * ttx - nx * ttz,
                          cz = nx * tty - ny * ttx;
                    float bd = cx * bitAccum[vi * 3] + cy * bitAccum[vi * 3 + 1] +
                               cz * bitAccum[vi * 3 + 2];
                    float sign = (bd < 0.f) ? -1.f : 1.f;
                    encodeOctTangent(ttx, tty, ttz, sign, merged.tangents[vi * 4],
                                     merged.tangents[vi * 4 + 1], merged.tangents[vi * 4 + 2],
                                     merged.tangents[vi * 4 + 3]);
                }
            }
        }

        // Remap vertex bone indices from original skin joint order to the
        // topologically sorted order used by the skeleton.
        if (!merged.boneIndices.empty() && scene.meshSkinIndices[i] >= 0)
        {
            const size_t skinIdx = static_cast<size_t>(scene.meshSkinIndices[i]);
            if (skinIdx < skelExtractions.size())
            {
                const auto& remap = skelExtractions[skinIdx].remap;
                for (size_t bi = 0; bi < merged.boneIndices.size(); ++bi)
                {
                    uint8_t orig = merged.boneIndices[bi];
                    if (orig < remap.size() && remap[orig] >= 0)
                    {
                        merged.boneIndices[bi] = static_cast<uint8_t>(remap[orig]);
                    }
                }
            }
        }

        scene.meshes.push_back(std::move(merged));
    }

    // ----- Materials -----
    scene.materials.reserve(data->materials_count);
    for (size_t i = 0; i < data->materials_count; ++i)
    {
        const cgltf_material& m = data->materials[i];
        CpuMaterialData mat;

        if (m.has_pbr_metallic_roughness)
        {
            const auto& pbr = m.pbr_metallic_roughness;
            mat.albedo = {pbr.base_color_factor[0], pbr.base_color_factor[1],
                          pbr.base_color_factor[2], pbr.base_color_factor[3]};
            mat.roughness = pbr.roughness_factor;
            mat.metallic = pbr.metallic_factor;

            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
            {
                const ptrdiff_t idx = pbr.base_color_texture.texture->image - data->images;
                mat.albedoTexIndex = static_cast<int32_t>(idx);
            }
            if (pbr.metallic_roughness_texture.texture &&
                pbr.metallic_roughness_texture.texture->image)
            {
                const ptrdiff_t idx = pbr.metallic_roughness_texture.texture->image - data->images;
                mat.ormTexIndex = static_cast<int32_t>(idx);
            }
        }

        if (m.normal_texture.texture && m.normal_texture.texture->image)
        {
            const ptrdiff_t idx = m.normal_texture.texture->image - data->images;
            mat.normalTexIndex = static_cast<int32_t>(idx);
        }

        if (m.emissive_texture.texture && m.emissive_texture.texture->image)
        {
            const ptrdiff_t idx = m.emissive_texture.texture->image - data->images;
            mat.emissiveTexIndex = static_cast<int32_t>(idx);
            mat.emissiveScale =
                std::max({m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]});
        }

        if (m.occlusion_texture.texture && m.occlusion_texture.texture->image)
        {
            const ptrdiff_t idx = m.occlusion_texture.texture->image - data->images;
            mat.occlusionTexIndex = static_cast<int32_t>(idx);
        }

        scene.materials.push_back(mat);
    }

    // ----- Animations -----
    // Extract animations relative to the first skin, passing the joint remap
    // so animation channel indices match the topologically sorted skeleton.
    if (data->skins_count > 0)
    {
        scene.animations = extractAnimations(data, data->skins[0], skelExtractions[0].remap);
    }

    // ----- Scene node tree -----
    // Build a set of all joint nodes across all skins so buildNodeTree can
    // mark them. Joint-only nodes (no mesh) are skipped during entity spawn.
    std::unordered_set<const cgltf_node*> jointNodes;
    for (size_t s = 0; s < data->skins_count; ++s)
    {
        const cgltf_skin& skin = data->skins[s];
        for (size_t j = 0; j < skin.joints_count; ++j)
            jointNodes.insert(skin.joints[j]);
    }

    const cgltf_scene* gltfScene =
        data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);

    if (gltfScene)
    {
        for (size_t i = 0; i < gltfScene->nodes_count; ++i)
            buildNodeTree(data, gltfScene->nodes[i], scene, 0, true, jointNodes);
    }

    cgltf_free(data);
    return scene;
}

}  // namespace engine::assets
