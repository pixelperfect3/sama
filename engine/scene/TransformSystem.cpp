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

math::Mat4 composeLocal(const rendering::TransformComponent& tcomp)
{
    math::Mat4 mat = glm::translate(math::Mat4(1.0f), tcomp.position);
    mat *= glm::mat4_cast(tcomp.rotation);
    mat = glm::scale(mat, tcomp.scale);
    return mat;
}

// Write the world matrix to an entity, creating WorldTransformComponent if absent.
// NOTE: the caller must not hold any WorldTransformComponent pointers across this
// call — emplace may reallocate the dense WorldTransformComponent array.
// TransformComponent pointers are unaffected (different sparse-set storage).
void setWorldMatrix(ecs::Registry& reg, ecs::EntityID entity, const math::Mat4& world)
{
    auto* wtc = reg.get<rendering::WorldTransformComponent>(entity);
    if (wtc)
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

        const bool missingWorld = !reg.has<rendering::WorldTransformComponent>(child);
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
            setWorldMatrix(reg, child, world);
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
            auto* cc = reg.get<ChildrenComponent>(child);
            if (cc != nullptr)
            {
                auto* wtc = reg.get<rendering::WorldTransformComponent>(child);
                math::Mat4 world = wtc ? wtc->matrix : composeLocal(*tcomp);
                updateChildren(reg, world, *cc, false);
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

            const bool missingWorld = !reg.has<rendering::WorldTransformComponent>(entity);
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
                setWorldMatrix(reg, entity, world);
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
                // descend to find the dirty descendant(s).
                auto* cc = reg.get<ChildrenComponent>(entity);
                if (cc != nullptr)
                {
                    auto* wtc = reg.get<rendering::WorldTransformComponent>(entity);
                    math::Mat4 world = wtc ? wtc->matrix : composeLocal(tcomp);
                    updateChildren(reg, world, *cc, false);
                }
                tcomp.flags &= ~kSubtreeDirty;
            }
        });
}

}  // namespace engine::scene
