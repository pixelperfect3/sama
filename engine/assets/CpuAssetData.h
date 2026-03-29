#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "engine/math/Types.h"
#include "engine/rendering/MeshBuilder.h"  // reuses MeshData

namespace engine::assets
{

// ---------------------------------------------------------------------------
// CpuTextureData — decoded pixel data, ready for bgfx upload.
//
// Pixels are always RGBA8 (4 bytes/pixel) after decoding.
// width × height × 4 == pixels.size().
// ---------------------------------------------------------------------------

struct CpuTextureData
{
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

// ---------------------------------------------------------------------------
// CpuMaterialData — PBR material parameters decoded from a glTF material.
//
// textureIndex fields reference entries in CpuSceneData::textures (not bgfx
// handles yet). -1 = no texture for that slot.
// ---------------------------------------------------------------------------

struct CpuMaterialData
{
    math::Vec4 albedo = math::Vec4(1.0f);  // base color factor (RGBA)
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveScale = 0.0f;
    int32_t albedoTexIndex = -1;  // index into CpuSceneData::textures
    int32_t normalTexIndex = -1;
    int32_t ormTexIndex = -1;        // G=roughness, B=metallic (R ignored per glTF spec)
    int32_t emissiveTexIndex = -1;   // emissive color texture
    int32_t occlusionTexIndex = -1;  // separate occlusion texture (R=AO)
};

// ---------------------------------------------------------------------------
// CpuSceneData — compound result from a glTF file.
//
// Contains all meshes, textures, and materials decoded from a single file,
// plus the scene node tree needed to spawn ECS entities. This is the entire
// CPU-side state needed to upload and instantiate a loaded scene.
//
// Node::meshIndex and Node::materialIndex index into the local meshes /
// materials vectors (-1 = no mesh / no material on this node).
// Node::textureIndices are resolved through materials; game code does not
// need to look up textures by index directly.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// CPU-side animation data — decoded from glTF skins and animations.
// ---------------------------------------------------------------------------

struct CpuJointData
{
    math::Mat4 inverseBindMatrix{1.0f};
    int32_t parentIndex = -1;
    std::string name;  // kept as string during loading; hashed to uint32_t at upload
};

struct CpuSkeletonData
{
    std::vector<CpuJointData> joints;
};

struct CpuAnimationClipData
{
    std::string name;
    float duration = 0.0f;

    struct Channel
    {
        uint32_t jointIndex;
        std::vector<float> positionTimes;
        std::vector<math::Vec3> positionValues;
        std::vector<float> rotationTimes;
        std::vector<math::Quat> rotationValues;
        std::vector<float> scaleTimes;
        std::vector<math::Vec3> scaleValues;
    };
    std::vector<Channel> channels;
};

struct CpuSceneData
{
    // Geometry — uses the existing MeshData type so buildMesh() can be called
    // directly on upload without any conversion.
    std::vector<rendering::MeshData> meshes;

    // Texture pixel data — one entry per image referenced in the file.
    std::vector<CpuTextureData> textures;

    // Materials — texture indices reference entries in the textures vector.
    std::vector<CpuMaterialData> materials;

    // Scene node tree.
    struct Node
    {
        std::string name;
        math::Mat4 localTransform{1.0f};  // local TRS matrix
        int32_t meshIndex = -1;
        int32_t materialIndex = -1;
        std::vector<uint32_t> childIndices;
    };
    std::vector<Node> nodes;
    std::vector<uint32_t> rootNodeIndices;

    // Animation data (optional, empty for non-animated assets).
    std::vector<CpuSkeletonData> skeletons;
    std::vector<CpuAnimationClipData> animations;
    // Per-mesh: which skin (skeleton) index applies. -1 = no skin.
    // Parallel to meshes vector.
    std::vector<int32_t> meshSkinIndices;
};

// ---------------------------------------------------------------------------
// CpuCompressedTextureData — raw KTX/DDS file bytes for bgfx upload.
//
// bgfx::createTexture() accepts raw KTX/DDS file data and handles format
// detection and decompression internally. We just pass the bytes through.
// ---------------------------------------------------------------------------

struct CpuCompressedTextureData
{
    std::vector<uint8_t> rawBytes;
};

// ---------------------------------------------------------------------------
// CpuAssetData — discriminated union over all CPU-side decoded types.
//
// AssetManager::uploadOne() visits this variant to dispatch to the correct
// bgfx upload path.
// ---------------------------------------------------------------------------

using CpuAssetData = std::variant<CpuTextureData, CpuSceneData, CpuCompressedTextureData>;

}  // namespace engine::assets
