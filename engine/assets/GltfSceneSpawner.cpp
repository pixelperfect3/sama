#include "engine/assets/GltfSceneSpawner.h"

#include <glm/glm.hpp>

#include "engine/assets/GltfAsset.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/RenderResources.h"

namespace engine::assets
{

namespace
{

// Register all meshes and materials from asset into res.
// Returns parallel vectors of RenderResources IDs indexed by the GltfAsset
// mesh / material arrays.
void registerResources(const GltfAsset& asset, rendering::RenderResources& res,
                       std::vector<uint32_t>& outMeshIds, std::vector<uint32_t>& outMaterialIds)
{
    outMeshIds.reserve(asset.meshes.size());
    for (const auto& mesh : asset.meshes)
    {
        // addMesh takes ownership — but GltfAsset owns the handles, so we
        // register a copy of the Mesh struct (handle values only, not ownership).
        // RenderResources::destroyAll() would double-free; the asset must outlive
        // the spawned entities. For Phase D this is acceptable.
        outMeshIds.push_back(res.addMesh(rendering::Mesh(mesh)));
    }

    // Register textures and build a mapping from asset-local index → RenderResources ID.
    // The asset's texture handles are non-owned here; GltfAsset retains ownership.
    std::vector<uint32_t> texIds;
    texIds.reserve(asset.textures.size());
    for (const auto& tex : asset.textures)
        texIds.push_back(res.addTexture(tex.handle));

    // Remap material texture IDs from 1-based asset-texture-index to
    // the RenderResources texture IDs assigned above.
    auto remap = [&](uint32_t id) -> uint32_t
    {
        if (id == 0)
            return 0;
        const uint32_t idx = id - 1;
        return (idx < texIds.size()) ? texIds[idx] : 0;
    };

    outMaterialIds.reserve(asset.materials.size());
    for (const auto& mat : asset.materials)
    {
        rendering::Material gpuMat = mat;
        gpuMat.albedoMapId = remap(mat.albedoMapId);
        gpuMat.normalMapId = remap(mat.normalMapId);
        gpuMat.ormMapId = remap(mat.ormMapId);
        outMaterialIds.push_back(res.addMaterial(gpuMat));
    }
}

// Recurse over node tree, spawning entities.
void spawnNode(const GltfAsset& asset, uint32_t nodeIdx, const math::Mat4& parentWorld,
               ecs::Registry& reg, const std::vector<uint32_t>& meshIds,
               const std::vector<uint32_t>& matIds)
{
    const GltfAsset::Node& node = asset.nodes[nodeIdx];
    const math::Mat4 worldTransform = parentWorld * node.localTransform;

    // Spawn an entity for this node if it has a mesh.
    if (node.meshIndex >= 0 && node.meshIndex < static_cast<int32_t>(meshIds.size()))
    {
        const uint32_t meshId = meshIds[static_cast<size_t>(node.meshIndex)];

        uint32_t matId = 0;
        if (node.materialIndex >= 0 && node.materialIndex < static_cast<int32_t>(matIds.size()))
        {
            matId = matIds[static_cast<size_t>(node.materialIndex)];
        }

        ecs::EntityID e = reg.createEntity();
        reg.emplace<rendering::WorldTransformComponent>(
            e, rendering::WorldTransformComponent{worldTransform});
        reg.emplace<rendering::MeshComponent>(e, rendering::MeshComponent{meshId});
        reg.emplace<rendering::MaterialComponent>(e, rendering::MaterialComponent{matId});
        reg.emplace<rendering::VisibleTag>(e);
        reg.emplace<rendering::ShadowVisibleTag>(e, rendering::ShadowVisibleTag{1});
    }

    // Recurse into children.
    for (uint32_t childIdx : node.childIndices)
        spawnNode(asset, childIdx, worldTransform, reg, meshIds, matIds);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------

void GltfSceneSpawner::spawn(const GltfAsset& asset, ecs::Registry& reg,
                             rendering::RenderResources& res)
{
    std::vector<uint32_t> meshIds;
    std::vector<uint32_t> matIds;
    registerResources(asset, res, meshIds, matIds);

    const math::Mat4 identity(1.0f);
    for (uint32_t rootIdx : asset.rootNodeIndices)
        spawnNode(asset, rootIdx, identity, reg, meshIds, matIds);
}

}  // namespace engine::assets
