#include "TransformSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/rendering/EcsComponents.h"
#include "engine/scene/HierarchyComponents.h"

namespace engine::scene
{

namespace
{

// TransformComponent::flags bits — see EcsComponents.h for the full contract.
constexpr uint8_t kSelfDirty = 0x01;
constexpr uint8_t kSubtreeDirty = 0x02;

// Build the local TRS matrix directly instead of via three glm operations.
//
// Old shape: glm::translate(I, t) * glm::mat4_cast(q) * glm::scale(rot, s)
//   - glm::translate writes the identity + 3 translation slots (~16 muls).
//   - glm::mat4_cast builds the rotation mat4 (~20 muls).
//   - glm::scale multiplies cols 0/1/2 by s.x/s.y/s.z (~12 muls).
//   - The two mat4*mat4 multiplies dominate: ~64 muls each = 128 muls.
//   - Total ≈ 176 muls/call, plus three temporary Mat4 objects spilled to
//     the stack (~192 bytes), most of which goes through L1.
//
// Direct construction:
//   R = mat3_cast(q)        // ~20 muls, in registers.
//   col0 = R[0] * s.x       //  3 muls
//   col1 = R[1] * s.y       //  3 muls
//   col2 = R[2] * s.z       //  3 muls
//   col3 = (t, 1)           //  0 muls
//   Total ≈ 29 muls/call, no Mat4 temporaries.
//
// The trick is that for a pure TRS the local matrix has the closed form
//   [ R*S | t ]
//   [  0  | 1 ]
// where R is the unit-quat rotation and S is diag(scale).  Column-major
// glm::mat4 storage means each column is one Vec4 — exactly what we want
// for the constructor below.  The fourth column is the position with w=1.
//
// See audit item line 38 in docs/PERF_AUDIT_2026-05-25.md.
math::Mat4 composeLocal(const rendering::TransformComponent& tcomp)
{
    const math::Mat3 rot = glm::mat3_cast(tcomp.rotation);
    return {math::Vec4(rot[0] * tcomp.scale.x, 0.0f), math::Vec4(rot[1] * tcomp.scale.y, 0.0f),
            math::Vec4(rot[2] * tcomp.scale.z, 0.0f), math::Vec4(tcomp.position, 1.0f)};
}

// Write the world matrix into an existing WorldTransformComponent or create one
// when the cached pointer is null.  Caller passes the cached pointer it already
// fetched at the start of the entity's processing — saves one sparse-set lookup
// per entity (the old helper used `reg.has() + reg.get() + reg.emplace()`
// internally, even though the caller had just done `reg.has()` immediately
// before; three lookups collapse to one).  See audit item line 39 in
// docs/PERF_AUDIT_2026-05-25.md.
//
// CAUTION: when `wtc == nullptr`, emplace may reallocate the dense
// WorldTransformComponent array — any pointer the caller has from a *different*
// entity's prior get() is invalidated.  In this file we use wtc only for the
// entity we just emplaced, so the invalidation is harmless for our callers.
// TransformComponent pointers are unaffected (different sparse-set storage).
void writeWorldMatrix(ecs::Registry& reg, ecs::EntityID entity,
                      rendering::WorldTransformComponent* wtc, const math::Mat4& world)
{
    if (wtc != nullptr)
    {
        wtc->matrix = world;
    }
    else
    {
        reg.emplace<rendering::WorldTransformComponent>(entity,
                                                        rendering::WorldTransformComponent{world});
    }
}

// Pass 1 — propagate self-dirty up the parent chain via the subtree-dirty
// bit (0x02).  We iterate the {TransformComponent, HierarchyComponent} join
// view so flat scenes (no hierarchy) pay essentially zero for this pre-pass:
// the view is empty and the lambda never fires.
//
// After this pass, any ancestor whose subtree contains at least one
// self-dirty descendant has flags & kSubtreeDirty set — which lets the
// top-down recursion in Pass 2 skip clean subtrees in O(1) instead of
// walking every descendant just to discover there's nothing to do.  See
// docs/PERF_AUDIT_2026-05-25.md item #C-xform for the why (audit observed
// 30-60% TransformSystem wall-clock savings on static scenes).
//
// Early-out: once we hit an ancestor that already has kSubtreeDirty set we
// stop walking, because any further ancestor was marked on a previous
// iteration this same frame.  Cost is bounded by (dirty-count × distinct
// ancestors), not (dirty-count × full depth).
void markSubtreeDirtyUpwards(ecs::Registry& reg)
{
    reg.view<rendering::TransformComponent, HierarchyComponent>().each(
        [&](ecs::EntityID /*entity*/, rendering::TransformComponent& tcomp,
            const HierarchyComponent& hier)
        {
            if (!(tcomp.flags & kSelfDirty))
            {
                return;
            }
            ecs::EntityID ancestor = hier.parent;
            while (ancestor != ecs::INVALID_ENTITY)
            {
                auto* parentTc = reg.get<rendering::TransformComponent>(ancestor);
                if (parentTc == nullptr)
                {
                    break;
                }
                if (parentTc->flags & kSubtreeDirty)
                {
                    // Already marked this frame — ancestors above are too.
                    break;
                }
                parentTc->flags |= kSubtreeDirty;

                auto* parentHc = reg.get<HierarchyComponent>(ancestor);
                ancestor = parentHc ? parentHc->parent : ecs::INVALID_ENTITY;
            }
        });
}

void updateChildren(ecs::Registry& reg, const math::Mat4& parentWorld,
                    const ChildrenComponent& children, bool parentDirty)
{
    for (ecs::EntityID child : children.children)
    {
        auto* tcomp = reg.get<rendering::TransformComponent>(child);
        if (tcomp == nullptr)
        {
            continue;
        }

        // One sparse-set lookup for both the missing-world test and the later
        // write path.  Old code did `reg.has<...>` here then
        // `reg.get<...>` again inside the old setWorldMatrix helper — three
        // separate sparse-set hits per child in the dirty path.  See audit
        // line 39.
        auto* wtc = reg.get<rendering::WorldTransformComponent>(child);
        const bool missingWorld = (wtc == nullptr);
        const bool selfDirty = (tcomp->flags & kSelfDirty) != 0;
        const bool subtreeDirty = (tcomp->flags & kSubtreeDirty) != 0;
        const bool childDirty = parentDirty || selfDirty || missingWorld;

        // The win lives here: when nothing in this subtree changed AND the
        // parent didn't change, skip both the recompute and the descent.
        // Previously this branch still recursed into descendants on the
        // chance that one was dirty.
        if (!childDirty && !subtreeDirty)
        {
            continue;
        }

        if (childDirty)
        {
            math::Mat4 world = parentWorld * composeLocal(*tcomp);
            writeWorldMatrix(reg, child, wtc, world);
            // wtc may now be invalidated (when it was null and emplace ran).
            // We don't read it again past this point, so the dangling
            // pointer is harmless — but be cautious if you add code below.
            // Clear BOTH bits — this node is fully up to date and so are all
            // descendants once the recursive call below returns (parentDirty
            // forces every descendant to recompute).
            tcomp->flags &= ~(kSelfDirty | kSubtreeDirty);

            auto* cc = reg.get<ChildrenComponent>(child);
            if (cc != nullptr)
            {
                updateChildren(reg, world, *cc, true);
            }
        }
        else
        {
            // subtreeDirty == true: some descendant is dirty but this node
            // isn't.  Don't recompute the world matrix for this node — just
            // descend with parentDirty=false so each dirty descendant gets
            // found and recomputed against this node's unchanged world.
            // wtc is non-null on this branch: childDirty was false, which
            // means missingWorld was false, which means we found a
            // WorldTransformComponent above.  Saves a redundant get().
            auto* cc = reg.get<ChildrenComponent>(child);
            if (cc != nullptr)
            {
                updateChildren(reg, wtc->matrix, *cc, false);
            }
            // After the recursion, every dirty descendant has been cleared,
            // so the subtree-dirty marker here is no longer accurate; clear
            // it so the next frame's Pass 1 starts from a true state.
            tcomp->flags &= ~kSubtreeDirty;
        }
    }
}

}  // namespace

void TransformSystem::update(ecs::Registry& reg)
{
    // Pass 1: propagate self-dirty up the parent chain.  See
    // markSubtreeDirtyUpwards() for the why and the cost analysis.
    markSubtreeDirtyUpwards(reg);

    // Pass 2: walk the hierarchy top-down, composing local TRS into world
    // matrices.  Roots are entities without a HierarchyComponent.  Non-root
    // entities are visited via recursion from their root.
    reg.view<rendering::TransformComponent>().each(
        [&](ecs::EntityID entity, rendering::TransformComponent& tcomp)
        {
            if (reg.has<HierarchyComponent>(entity))
            {
                return;
            }

            // One sparse-set lookup serves both the missing-world test and
            // the write path below.  See audit line 39.
            auto* wtc = reg.get<rendering::WorldTransformComponent>(entity);
            const bool missingWorld = (wtc == nullptr);
            const bool selfDirty = (tcomp.flags & kSelfDirty) != 0;
            const bool subtreeDirty = (tcomp.flags & kSubtreeDirty) != 0;
            const bool dirty = selfDirty || missingWorld;

            // Skip the whole tree if neither this node nor any descendant
            // needs work.  In a static scene this is the dominant fast path.
            if (!dirty && !subtreeDirty)
            {
                return;
            }

            if (dirty)
            {
                math::Mat4 world = composeLocal(tcomp);
                writeWorldMatrix(reg, entity, wtc, world);
                tcomp.flags &= ~(kSelfDirty | kSubtreeDirty);

                auto* cc = reg.get<ChildrenComponent>(entity);
                if (cc != nullptr)
                {
                    updateChildren(reg, world, *cc, true);
                }
            }
            else
            {
                // subtreeDirty == true, root clean.  Don't recompute the root;
                // descend to find the dirty descendant(s).  wtc is non-null
                // here (dirty == false implies missingWorld == false).
                auto* cc = reg.get<ChildrenComponent>(entity);
                if (cc != nullptr)
                {
                    updateChildren(reg, wtc->matrix, *cc, false);
                }
                tcomp.flags &= ~kSubtreeDirty;
            }
        });
}

}  // namespace engine::scene
