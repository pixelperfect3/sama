#include "engine/rendering/systems/FrustumCullSystem.h"

#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/systems/CullHelpers.h"

namespace engine::rendering
{

void FrustumCullSystem::update(ecs::Registry& reg, const RenderResources& res,
                               const math::Frustum& frustum)
{
    auto meshView = reg.view<WorldTransformComponent, MeshComponent>();

    meshView.each(
        [&](ecs::EntityID entity, const WorldTransformComponent& wtc, const MeshComponent& mc)
        {
            // Single ECS probe for the prior visibility state.  Re-used to
            // skip the redundant `has`-then-`emplace`/`remove` round-trip
            // when the result didn't change between frames — which is the
            // common case in steady scenes.  See audit item line 42.
            const bool wasVisible = reg.has<VisibleTag>(entity);

            const Mesh* mesh = res.getMesh(mc.mesh);
            if (mesh == nullptr)
            {
                // No mesh data — treat as not visible.
                if (wasVisible)
                {
                    reg.remove<VisibleTag>(entity);
                }
                return;
            }

            // Approximate world-space AABB (conservative — never culls a
            // visible entity, but a rotated bound is slightly larger than
            // a perfectly-fit OBB).  See CullHelpers.h for the explicit-
            // fabsf form that replaces the `glm::abs(Vec4)` + Mat3 ctor +
            // Mat3*Vec3 chain (audit line 41).
            const math::Vec3 localCenter = (mesh->boundsMin + mesh->boundsMax) * 0.5F;
            const math::Vec3 localHalfExtent = (mesh->boundsMax - mesh->boundsMin) * 0.5F;
            math::Vec3 worldMin;
            math::Vec3 worldMax;
            computeConservativeWorldAabb(wtc.matrix, localCenter, localHalfExtent, worldMin,
                                         worldMax);

            const bool isVisible = frustum.containsAABB(worldMin, worldMax);

            // Only touch the ECS when the state changes.  Steady-state hot
            // entities (visible last frame, still visible this frame) skip
            // the emplace/remove entirely and pay just the single `has`
            // probe above.
            if (isVisible && !wasVisible)
            {
                reg.emplace<VisibleTag>(entity);
            }
            else if (!isVisible && wasVisible)
            {
                reg.remove<VisibleTag>(entity);
            }
        });
}

}  // namespace engine::rendering
