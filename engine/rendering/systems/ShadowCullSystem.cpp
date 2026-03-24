#include "engine/rendering/systems/ShadowCullSystem.h"

#include <glm/glm.hpp>

#include "engine/rendering/EcsComponents.h"

namespace engine::rendering
{

void ShadowCullSystem::update(ecs::Registry& reg, const RenderResources& res,
                              const math::Frustum& shadowFrustum, uint32_t cascadeIndex)
{
    const uint8_t cascadeBit = static_cast<uint8_t>(1u << cascadeIndex);

    auto meshView = reg.view<WorldTransformComponent, MeshComponent>();

    meshView.each(
        [&](ecs::EntityID entity, const WorldTransformComponent& wtc, const MeshComponent& mc)
        {
            const Mesh* mesh = res.getMesh(mc.mesh);
            if (!mesh)
            {
                // No mesh — clear this cascade's bit.
                if (reg.has<ShadowVisibleTag>(entity))
                {
                    ShadowVisibleTag* tag = reg.get<ShadowVisibleTag>(entity);
                    tag->cascadeMask &= ~cascadeBit;
                    if (tag->cascadeMask == 0)
                        reg.remove<ShadowVisibleTag>(entity);
                }
                return;
            }

            // Approximate world-space AABB — same conservative method as
            // FrustumCullSystem: transform local AABB center, then add local
            // half-extents projected through the absolute rotation columns.
            const math::Vec3 localCenter = (mesh->boundsMin + mesh->boundsMax) * 0.5f;
            const math::Vec3 localHalfExtent = (mesh->boundsMax - mesh->boundsMin) * 0.5f;

            const math::Vec3 worldCenter = math::Vec3(wtc.matrix * math::Vec4(localCenter, 1.0f));

            const math::Mat3 absRot = math::Mat3(glm::abs(wtc.matrix[0]), glm::abs(wtc.matrix[1]),
                                                 glm::abs(wtc.matrix[2]));
            const math::Vec3 worldHalfExtent = absRot * localHalfExtent;

            const math::Vec3 worldMin = worldCenter - worldHalfExtent;
            const math::Vec3 worldMax = worldCenter + worldHalfExtent;

            if (shadowFrustum.containsAABB(worldMin, worldMax))
            {
                if (!reg.has<ShadowVisibleTag>(entity))
                {
                    ShadowVisibleTag tag{};
                    tag.cascadeMask = cascadeBit;
                    reg.emplace<ShadowVisibleTag>(entity, tag);
                }
                else
                {
                    reg.get<ShadowVisibleTag>(entity)->cascadeMask |= cascadeBit;
                }
            }
            else
            {
                if (reg.has<ShadowVisibleTag>(entity))
                {
                    ShadowVisibleTag* tag = reg.get<ShadowVisibleTag>(entity);
                    tag->cascadeMask &= ~cascadeBit;
                    if (tag->cascadeMask == 0)
                        reg.remove<ShadowVisibleTag>(entity);
                }
            }
        });
}

}  // namespace engine::rendering
