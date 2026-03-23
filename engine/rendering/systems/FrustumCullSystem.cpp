#include "engine/rendering/systems/FrustumCullSystem.h"

#include <glm/glm.hpp>

#include "engine/rendering/EcsComponents.h"

namespace engine::rendering
{

void FrustumCullSystem::update(ecs::Registry& reg, const RenderResources& res,
                               const math::Frustum& frustum)
{
    auto meshView = reg.view<WorldTransformComponent, MeshComponent>();

    meshView.each(
        [&](ecs::EntityID entity, const WorldTransformComponent& wtc, const MeshComponent& mc)
        {
            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh)
            {
                // No mesh data — treat as not visible.
                reg.remove<VisibleTag>(entity);
                return;
            }

            // Approximate world-space AABB:
            //   Transform the local AABB center by the world matrix, then add
            //   local half-extents to produce a conservative world AABB.
            //   This is an over-approximation (a rotated box won't fit perfectly),
            //   but it is always conservative — no visible entity is ever culled.
            const math::Vec3 localCenter = (mesh->boundsMin + mesh->boundsMax) * 0.5f;
            const math::Vec3 localHalfExtent = (mesh->boundsMax - mesh->boundsMin) * 0.5f;

            // Transform center into world space.
            const math::Vec3 worldCenter = math::Vec3(wtc.matrix * math::Vec4(localCenter, 1.0f));

            // Compute conservative world-space AABB by projecting the local
            // half-extents through the absolute values of the rotation columns.
            // This is exact for axis-aligned transforms and conservative for
            // rotated transforms.
            const math::Mat3 absRot = math::Mat3(glm::abs(wtc.matrix[0]), glm::abs(wtc.matrix[1]),
                                                 glm::abs(wtc.matrix[2]));
            const math::Vec3 worldHalfExtent = absRot * localHalfExtent;

            const math::Vec3 worldMin = worldCenter - worldHalfExtent;
            const math::Vec3 worldMax = worldCenter + worldHalfExtent;

            if (frustum.containsAABB(worldMin, worldMax))
            {
                if (!reg.has<VisibleTag>(entity))
                    reg.emplace<VisibleTag>(entity);
            }
            else
            {
                reg.remove<VisibleTag>(entity);
            }
        });
}

}  // namespace engine::rendering
