#include "engine/rendering/systems/ShadowCullSystem.h"

#include <cassert>

#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/systems/CullHelpers.h"

namespace engine::rendering
{

void ShadowCullSystem::update(ecs::Registry& reg, const RenderResources& res,
                              const math::Frustum& shadowFrustum, uint32_t cascadeIndex)
{
    // Single-cascade form delegates to the multi-cascade form with a
    // one-element span.  Keeps the two paths semantically identical and
    // ensures any future fix lands in one place.
    const math::Frustum frustum = shadowFrustum;
    update(reg, res, std::span<const math::Frustum>{&frustum, 1}, cascadeIndex);
}

void ShadowCullSystem::update(ecs::Registry& reg, const RenderResources& res,
                              std::span<const math::Frustum> cascadeFrustums,
                              uint32_t baseCascadeIdx)
{
    assert(!cascadeFrustums.empty() && "ShadowCullSystem: empty cascade list");
    // cascadeMask is uint8_t (see EcsComponents.h:38), so we can address at
    // most 8 cascades total — fail loudly rather than silently overflowing.
    assert(baseCascadeIdx + cascadeFrustums.size() <= 8U &&
           "ShadowCullSystem: cascade range exceeds cascadeMask width");

    // Pre-compute the "this call owns these bits" mask so we can clear all
    // covered cascades in a single OR-clear at write time.  Bit i of
    // ownedRange corresponds to absolute cascade `baseCascadeIdx + i`.
    uint8_t ownedRange = 0;
    for (size_t i = 0; i < cascadeFrustums.size(); ++i)
    {
        ownedRange |= static_cast<uint8_t>(1U << (baseCascadeIdx + i));
    }
    const uint8_t preservedRangeMask = static_cast<uint8_t>(~ownedRange);

    auto meshView = reg.view<WorldTransformComponent, MeshComponent>();

    meshView.each(
        [&](ecs::EntityID entity, const WorldTransformComponent& wtc, const MeshComponent& mc)
        {
            // Resolve the existing ShadowVisibleTag once.  We need it to
            // preserve bits outside our owned range AND to avoid emplacing
            // a fresh tag when our computed mask is zero (would dirty the
            // SparseSet for nothing).
            ShadowVisibleTag* existingTag = reg.get<ShadowVisibleTag>(entity);
            const uint8_t preservedBits =
                (existingTag != nullptr) ? (existingTag->cascadeMask & preservedRangeMask) : 0;

            const Mesh* mesh = res.getMesh(mc.mesh);
            if (mesh == nullptr)
            {
                // No mesh — clear every cascade bit we own.
                if (existingTag != nullptr)
                {
                    existingTag->cascadeMask = preservedBits;
                    if (existingTag->cascadeMask == 0)
                    {
                        reg.remove<ShadowVisibleTag>(entity);
                    }
                }
                return;
            }

            // Build the conservative world-space AABB once, then test
            // against every frustum.  This is the audit's line-43 win:
            // 3-cascade callers used to walk the mesh view 3× and rebuild
            // the AABB 3× per entity.
            const math::Vec3 localCenter = (mesh->boundsMin + mesh->boundsMax) * 0.5F;
            const math::Vec3 localHalfExtent = (mesh->boundsMax - mesh->boundsMin) * 0.5F;
            math::Vec3 worldMin;
            math::Vec3 worldMax;
            computeConservativeWorldAabb(wtc.matrix, localCenter, localHalfExtent, worldMin,
                                         worldMax);

            // Accumulate cascade bits into a local before touching the
            // tag — keeps the inner loop branch-free of ECS ops.
            uint8_t computedMask = 0;
            for (size_t i = 0; i < cascadeFrustums.size(); ++i)
            {
                if (cascadeFrustums[i].containsAABB(worldMin, worldMax))
                {
                    computedMask |= static_cast<uint8_t>(1U << (baseCascadeIdx + i));
                }
            }

            const uint8_t finalMask = static_cast<uint8_t>(preservedBits | computedMask);

            if (finalMask == 0)
            {
                // Empty post-merge → no tag needed.  Strip any leftover.
                if (existingTag != nullptr)
                {
                    reg.remove<ShadowVisibleTag>(entity);
                }
                return;
            }

            if (existingTag != nullptr)
            {
                existingTag->cascadeMask = finalMask;
            }
            else
            {
                ShadowVisibleTag tag{};
                tag.cascadeMask = finalMask;
                reg.emplace<ShadowVisibleTag>(entity, tag);
            }
        });
}

}  // namespace engine::rendering
