#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/animation/AnimationClip.h"
#include "engine/animation/Skeleton.h"
#include "engine/assets/Texture.h"
#include "engine/math/Types.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"

namespace engine::assets
{

// ---------------------------------------------------------------------------
// GltfAsset — GPU-side result of loading a glTF / GLB file.
//
// Produced by AssetManager::processUploads() after GltfLoader decodes the
// file on a worker thread and the main thread uploads all GPU resources.
//
// meshes, textures, and materials are parallel to the glTF file's arrays.
// The node tree mirrors the glTF scene graph; node indices reference into
// the meshes / materials vectors.
//
// GltfSceneSpawner::spawn() walks the node tree and creates ECS entities.
// ---------------------------------------------------------------------------

struct GltfAsset
{
    std::vector<rendering::Mesh> meshes;
    std::vector<Texture> textures;
    std::vector<rendering::Material> materials;

    struct Node
    {
        std::string name;
        math::Mat4 localTransform{1.0f};
        int32_t meshIndex = -1;      // -1 = no mesh on this node
        int32_t materialIndex = -1;  // -1 = no material override
        bool isJoint = false;        // true if this node is a skeleton joint
        std::vector<uint32_t> childIndices;
    };
    std::vector<Node> nodes;
    std::vector<uint32_t> rootNodeIndices;

    // Animation data (CPU-only, no GPU upload needed).
    std::vector<animation::Skeleton> skeletons;
    std::vector<animation::AnimationClip> animations;
    // Per-mesh: which skeleton index applies. -1 = no skin.
    std::vector<int32_t> meshSkinIndices;

    // Release all bgfx GPU handles. Called by AssetManager on slot destruction.
    void destroy()
    {
        for (auto& m : meshes)
            m.destroy();
        for (auto& t : textures)
            t.destroy();
        meshes.clear();
        textures.clear();
        materials.clear();
        nodes.clear();
        rootNodeIndices.clear();
    }
};

}  // namespace engine::assets
