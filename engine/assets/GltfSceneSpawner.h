#pragma once

namespace engine::animation
{
class AnimationResources;
}

namespace engine::ecs
{
class Registry;
}

namespace engine::rendering
{
class RenderResources;
}

namespace engine::assets
{

struct GltfAsset;

// ---------------------------------------------------------------------------
// GltfSceneSpawner — instantiates a loaded GltfAsset into the ECS.
//
// spawn() walks the GltfAsset node tree and creates one entity per node
// that has a mesh.  Each entity receives:
//   - WorldTransformComponent  (node world-space matrix, root-relative)
//   - MeshComponent            (RenderResources mesh ID)
//   - MaterialComponent        (RenderResources material ID)
//   - VisibleTag
//   - ShadowVisibleTag         (cascade 0)
//
// Meshes and materials are registered into RenderResources on the first
// call for each GltfAsset; subsequent calls re-use the same IDs.
// ---------------------------------------------------------------------------

class GltfSceneSpawner
{
public:
    // Spawn the scene from asset into reg, registering GPU resources into res.
    // parentTransform: optional root transform applied to the whole scene
    //   (pass glm::mat4(1.0f) for no additional transform).
    static void spawn(const GltfAsset& asset, ecs::Registry& reg, rendering::RenderResources& res);

    // Spawn with animation support. Registers skeletons and clips into animRes
    // and attaches SkeletonComponent, AnimatorComponent, SkinComponent to
    // entities with skinned meshes.
    static void spawn(const GltfAsset& asset, ecs::Registry& reg, rendering::RenderResources& res,
                      animation::AnimationResources& animRes);
};

}  // namespace engine::assets
