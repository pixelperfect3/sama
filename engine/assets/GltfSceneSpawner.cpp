#include "engine/assets/GltfSceneSpawner.h"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "engine/assets/GltfAsset.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/RenderResources.h"
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/SceneGraph.h"

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
        gpuMat.emissiveMapId = remap(mat.emissiveMapId);
        gpuMat.occlusionMapId = remap(mat.occlusionMapId);
        outMaterialIds.push_back(res.addMaterial(gpuMat));
    }
}

// Decompose a Mat4 into TransformComponent TRS.
rendering::TransformComponent decomposeToTRS(const math::Mat4& m)
{
    math::Vec3 scale;
    math::Quat rotation;
    math::Vec3 translation;
    math::Vec3 skew;
    math::Vec4 perspective;
    glm::decompose(m, scale, rotation, translation, skew, perspective);

    rendering::TransformComponent tc{};
    tc.position = translation;
    tc.rotation = rotation;
    tc.scale = scale;
    return tc;
}

// Recurse over node tree, spawning entities with local TRS + hierarchy.
// Returns the EntityID of the spawned node.
ecs::EntityID spawnNode(const GltfAsset& asset, uint32_t nodeIdx, ecs::EntityID parentEntity,
                        ecs::Registry& reg, const std::vector<uint32_t>& meshIds,
                        const std::vector<uint32_t>& matIds)
{
    const GltfAsset::Node& node = asset.nodes[nodeIdx];

    ecs::EntityID e = reg.createEntity();

    // Store local TRS; TransformSystem will compute WorldTransformComponent.
    reg.emplace<rendering::TransformComponent>(e, decomposeToTRS(node.localTransform));

    // Establish parent-child link.
    if (parentEntity != ecs::INVALID_ENTITY)
        scene::setParent(reg, e, parentEntity);

    // Attach mesh and material if this node references geometry.
    if (node.meshIndex >= 0 && node.meshIndex < static_cast<int32_t>(meshIds.size()))
    {
        const uint32_t meshId = meshIds[static_cast<size_t>(node.meshIndex)];

        uint32_t matId = 0;
        if (node.materialIndex >= 0 && node.materialIndex < static_cast<int32_t>(matIds.size()))
            matId = matIds[static_cast<size_t>(node.materialIndex)];

        reg.emplace<rendering::MeshComponent>(e, rendering::MeshComponent{meshId});
        reg.emplace<rendering::MaterialComponent>(e, rendering::MaterialComponent{matId});
        reg.emplace<rendering::VisibleTag>(e);
        reg.emplace<rendering::ShadowVisibleTag>(e, rendering::ShadowVisibleTag{1});
    }

    // Recurse into children.
    for (uint32_t childIdx : node.childIndices)
        spawnNode(asset, childIdx, e, reg, meshIds, matIds);

    return e;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------

void GltfSceneSpawner::spawn(const GltfAsset& asset, ecs::Registry& reg,
                             rendering::RenderResources& res)
{
    std::vector<uint32_t> meshIds;
    std::vector<uint32_t> matIds;
    registerResources(asset, res, meshIds, matIds);

    for (uint32_t rootIdx : asset.rootNodeIndices)
        spawnNode(asset, rootIdx, ecs::INVALID_ENTITY, reg, meshIds, matIds);
}

}  // namespace engine::assets
