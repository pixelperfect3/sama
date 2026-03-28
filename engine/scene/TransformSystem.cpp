#include "TransformSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/rendering/EcsComponents.h"
#include "engine/scene/HierarchyComponents.h"

namespace engine::scene
{

namespace
{

math::Mat4 composeLocal(const rendering::TransformComponent& t)
{
    math::Mat4 m = glm::translate(math::Mat4(1.0f), t.position);
    m *= glm::mat4_cast(t.rotation);
    m = glm::scale(m, t.scale);
    return m;
}

// Write the world matrix to an entity, creating WorldTransformComponent if absent.
// NOTE: the caller must not hold any component pointers across this call —
// emplace may reallocate the WorldTransformComponent dense array.
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

void updateChildren(ecs::Registry& reg, const math::Mat4& parentWorld,
                    const ChildrenComponent& children, bool parentDirty)
{
    for (ecs::EntityID child : children.children)
    {
        auto* tc = reg.get<rendering::TransformComponent>(child);
        if (!tc)
            continue;

        // Treat as dirty if: parent is dirty, own dirty flag is set, or
        // WorldTransformComponent doesn't exist yet (first frame).
        bool missingWorld = !reg.has<rendering::WorldTransformComponent>(child);
        bool childDirty = parentDirty || (tc->flags & 0x01) || missingWorld;

        if (childDirty)
        {
            math::Mat4 world = parentWorld * composeLocal(*tc);
            setWorldMatrix(reg, child, world);
            tc->flags &= ~0x01;  // clear dirty

            // All descendants must also update since this node changed.
            auto* cc = reg.get<ChildrenComponent>(child);
            if (cc)
                updateChildren(reg, world, *cc, true);
        }
        else
        {
            // Not dirty — skip recomputation but still recurse
            // (a descendant might be dirty even if this node isn't).
            auto* cc = reg.get<ChildrenComponent>(child);
            if (cc)
            {
                auto* wtc = reg.get<rendering::WorldTransformComponent>(child);
                math::Mat4 world = wtc ? wtc->matrix : composeLocal(*tc);
                updateChildren(reg, world, *cc, false);
            }
        }
    }
}

}  // namespace

void TransformSystem::update(ecs::Registry& reg)
{
    // Iterate all entities with TransformComponent.
    // Roots are those without a HierarchyComponent (no parent).
    reg.view<rendering::TransformComponent>().each(
        [&](ecs::EntityID entity, rendering::TransformComponent& tc)
        {
            // Skip non-root entities; they will be visited via recursion.
            if (reg.has<HierarchyComponent>(entity))
                return;

            bool missingWorld = !reg.has<rendering::WorldTransformComponent>(entity);
            bool dirty = (tc.flags & 0x01) || missingWorld;

            if (dirty)
            {
                math::Mat4 world = composeLocal(tc);
                setWorldMatrix(reg, entity, world);
                tc.flags &= ~0x01;  // clear dirty

                auto* cc = reg.get<ChildrenComponent>(entity);
                if (cc)
                    updateChildren(reg, world, *cc, true);
            }
            else
            {
                // Root is clean — still recurse in case a descendant is dirty.
                auto* cc = reg.get<ChildrenComponent>(entity);
                if (cc)
                {
                    auto* wtc = reg.get<rendering::WorldTransformComponent>(entity);
                    math::Mat4 world = wtc ? wtc->matrix : composeLocal(tc);
                    updateChildren(reg, world, *cc, false);
                }
            }
        });
}

}  // namespace engine::scene
