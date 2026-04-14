#include "engine/assets/GltfSceneSpawner.h"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/assets/GltfAsset.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/RenderResources.h"
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/NameComponent.h"
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
// Skips joint-only nodes (skeleton bones with no mesh) to avoid cluttering
// the hierarchy with internal skeleton structure.
// Returns the EntityID of the spawned node, or INVALID_ENTITY if skipped.
ecs::EntityID spawnNode(const GltfAsset& asset, uint32_t nodeIdx, ecs::EntityID parentEntity,
                        ecs::Registry& reg, const std::vector<uint32_t>& meshIds,
                        const std::vector<uint32_t>& matIds)
{
    const GltfAsset::Node& node = asset.nodes[nodeIdx];

    // Skip joint-only nodes — they are internal skeleton structure managed by
    // the animation system, not scene entities.
    if (node.isJoint && node.meshIndex < 0)
    {
        // Still recurse children — a joint's child might have a mesh.
        for (uint32_t childIdx : node.childIndices)
            spawnNode(asset, childIdx, parentEntity, reg, meshIds, matIds);
        return ecs::INVALID_ENTITY;
    }

    ecs::EntityID e = reg.createEntity();

    // Store local TRS; TransformSystem will compute WorldTransformComponent.
    reg.emplace<rendering::TransformComponent>(e, decomposeToTRS(node.localTransform));

    // Assign glTF node name if available.
    if (!node.name.empty())
        reg.emplace<scene::NameComponent>(e, scene::NameComponent{node.name});

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

void GltfSceneSpawner::spawn(const GltfAsset& asset, ecs::Registry& reg,
                             rendering::RenderResources& res,
                             animation::AnimationResources& animRes)
{
    std::vector<uint32_t> meshIds;
    std::vector<uint32_t> matIds;
    registerResources(asset, res, meshIds, matIds);

    // Register skeletons and clips into AnimationResources.
    std::vector<uint32_t> skelIds;
    skelIds.reserve(asset.skeletons.size());
    for (const auto& skel : asset.skeletons)
        skelIds.push_back(animRes.addSkeleton(skel));

    std::vector<uint32_t> clipIds;
    clipIds.reserve(asset.animations.size());
    for (const auto& clip : asset.animations)
        clipIds.push_back(animRes.addClip(clip));

    // Spawn nodes with animation components where appropriate.
    for (uint32_t rootIdx : asset.rootNodeIndices)
        spawnNode(asset, rootIdx, ecs::INVALID_ENTITY, reg, meshIds, matIds);

    // Walk all spawned entities and attach animation components to those with
    // skinned meshes. We find entities by checking meshIndex against meshSkinIndices.
    auto meshView = reg.view<rendering::MeshComponent>();

    meshView.each(
        [&](ecs::EntityID entity, const rendering::MeshComponent& mc)
        {
            // Find the original glTF mesh index by looking up the mesh ID
            // in our local meshIds mapping.
            int32_t gltfMeshIdx = -1;
            for (size_t i = 0; i < meshIds.size(); ++i)
            {
                if (meshIds[i] == mc.mesh)
                {
                    gltfMeshIdx = static_cast<int32_t>(i);
                    break;
                }
            }

            if (gltfMeshIdx < 0 || static_cast<size_t>(gltfMeshIdx) >= asset.meshSkinIndices.size())
                return;

            int32_t skinIdx = asset.meshSkinIndices[gltfMeshIdx];
            if (skinIdx < 0 || static_cast<size_t>(skinIdx) >= skelIds.size())
                return;

            // Attach animation components.
            reg.emplace<animation::SkeletonComponent>(
                entity, animation::SkeletonComponent{skelIds[static_cast<size_t>(skinIdx)]});

            animation::AnimatorComponent anim{};
            anim.clipId = clipIds.empty() ? UINT32_MAX : clipIds[0];
            anim.nextClipId = UINT32_MAX;
            anim.playbackTime = 0.0f;
            anim.prevPlaybackTime = 0.0f;
            anim.speed = 1.0f;
            anim.blendFactor = 0.0f;
            anim.blendDuration = 0.0f;
            anim.blendElapsed = 0.0f;
            anim.flags = animation::AnimatorComponent::kFlagLooping;  // looping, not auto-playing
            anim._pad[0] = anim._pad[1] = anim._pad[2] = 0;
            reg.emplace<animation::AnimatorComponent>(entity, anim);

            const animation::Skeleton* skel =
                animRes.getSkeleton(skelIds[static_cast<size_t>(skinIdx)]);
            uint32_t boneCount = skel ? skel->jointCount() : 0;

            reg.emplace<animation::SkinComponent>(entity, animation::SkinComponent{0, boneCount});
        });
}

}  // namespace engine::assets
