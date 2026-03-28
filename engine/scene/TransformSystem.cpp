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
                    const ChildrenComponent& children)
{
    for (ecs::EntityID child : children.children)
    {
        auto* tc = reg.get<rendering::TransformComponent>(child);
        if (!tc)
            continue;

        math::Mat4 world = parentWorld * composeLocal(*tc);
        setWorldMatrix(reg, child, world);

        auto* cc = reg.get<ChildrenComponent>(child);
        if (cc)
            updateChildren(reg, world, *cc);
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

            math::Mat4 world = composeLocal(tc);
            setWorldMatrix(reg, entity, world);

            auto* cc = reg.get<ChildrenComponent>(entity);
            if (cc)
                updateChildren(reg, world, *cc);
        });
}

}  // namespace engine::scene
