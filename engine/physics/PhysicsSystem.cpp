#include "engine/physics/PhysicsSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "engine/memory/InlinedVector.h"
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/scene/HierarchyComponents.h"

namespace engine::physics
{

static constexpr float kFixedDt = 1.0f / 60.0f;
static constexpr int kMaxSubSteps = 4;

void PhysicsSystem::update(ecs::Registry& reg, IPhysicsEngine& physics, float deltaTime)
{
    // 1. Register new bodies
    registerNewBodies(reg, physics);

    // 2. Sync kinematic body transforms to Jolt
    syncKinematicBodies(reg, physics, deltaTime);

    // 3. Step physics
    physics.step(deltaTime, kMaxSubSteps);

    // 4. Write back dynamic body transforms
    syncDynamicBodies(reg, physics);

    // 5. Cleanup bodies for destroyed entities
    cleanupDestroyedBodies(reg, physics);
}

void PhysicsSystem::registerNewBodies(ecs::Registry& reg, IPhysicsEngine& physics)
{
    // Collect entities that need body creation first, to avoid modifying components
    // while iterating.
    memory::InlinedVector<ecs::EntityID, 16> toRegister;

    reg.view<RigidBodyComponent, ColliderComponent>().each(
        [&](ecs::EntityID entity, RigidBodyComponent& /*rb*/, ColliderComponent& /*col*/)
        {
            if (!reg.has<PhysicsBodyCreatedTag>(entity))
            {
                toRegister.push_back(entity);
            }
        });

    for (ecs::EntityID entity : toRegister)
    {
        auto* rb = reg.get<RigidBodyComponent>(entity);
        auto* col = reg.get<ColliderComponent>(entity);
        if (!rb || !col)
        {
            continue;
        }

        // Determine initial position/rotation
        math::Vec3 position{0.0f};
        math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

        // Use WorldTransformComponent if entity has a parent, otherwise local transform
        auto* wtc = reg.get<rendering::WorldTransformComponent>(entity);
        auto* tc = reg.get<rendering::TransformComponent>(entity);

        if (wtc && reg.has<scene::HierarchyComponent>(entity))
        {
            // Decompose world matrix
            math::Vec3 scale;
            math::Vec3 skew;
            math::Vec4 perspective;
            glm::decompose(wtc->matrix, scale, rotation, position, skew, perspective);
        }
        else if (tc)
        {
            position = tc->position;
            rotation = tc->rotation;
        }

        // Add collider offset to position
        position += rotation * col->offset;

        BodyDesc desc;
        desc.position = position;
        desc.rotation = rotation;
        desc.shape = col->shape;
        desc.halfExtents = col->halfExtents;
        desc.radius = col->radius;
        desc.mass = rb->mass;
        desc.friction = rb->friction;
        desc.restitution = rb->restitution;
        desc.linearDamping = rb->linearDamping;
        desc.angularDamping = rb->angularDamping;
        desc.type = rb->type;
        desc.layer = rb->layer;
        desc.entity = entity;

        uint32_t bodyID = physics.addBody(desc);
        if (bodyID != ~0u)
        {
            rb->bodyID = bodyID;
            reg.emplace<PhysicsBodyCreatedTag>(entity);
        }
    }
}

void PhysicsSystem::syncKinematicBodies(ecs::Registry& reg, IPhysicsEngine& physics,
                                        float deltaTime)
{
    reg.view<RigidBodyComponent, PhysicsBodyCreatedTag>().each(
        [&](ecs::EntityID entity, RigidBodyComponent& rb, PhysicsBodyCreatedTag&)
        {
            if (rb.type != BodyType::Kinematic || rb.bodyID == ~0u)
            {
                return;
            }

            // Read world-space transform (computed last frame by TransformSystem)
            auto* wtc = reg.get<rendering::WorldTransformComponent>(entity);
            if (!wtc)
            {
                return;
            }

            // Decompose world matrix to get position and rotation.
            // glm::decompose can produce non-normalized quaternions;
            // Jolt asserts on normalized input.
            math::Vec3 position;
            math::Quat rotation;
            math::Vec3 scale;
            math::Vec3 skew;
            math::Vec4 perspective;
            glm::decompose(wtc->matrix, scale, rotation, position, skew, perspective);
            rotation = glm::normalize(rotation);

            physics.moveKinematic(rb.bodyID, position, rotation, deltaTime);
        });
}

void PhysicsSystem::syncDynamicBodies(ecs::Registry& reg, IPhysicsEngine& physics)
{
    reg.view<RigidBodyComponent, PhysicsBodyCreatedTag>().each(
        [&](ecs::EntityID entity, RigidBodyComponent& rb, PhysicsBodyCreatedTag&)
        {
            if (rb.type != BodyType::Dynamic || rb.bodyID == ~0u)
            {
                return;
            }

            auto* tc = reg.get<rendering::TransformComponent>(entity);
            if (!tc)
            {
                return;
            }

            math::Vec3 worldPos;
            math::Quat worldRot;
            physics.getBodyTransform(rb.bodyID, worldPos, worldRot);

            // If entity is in a hierarchy, convert world-space to local-space
            auto* hierarchy = reg.get<scene::HierarchyComponent>(entity);
            if (hierarchy && hierarchy->parent != ecs::INVALID_ENTITY)
            {
                auto* parentWtc = reg.get<rendering::WorldTransformComponent>(hierarchy->parent);
                if (parentWtc)
                {
                    math::Mat4 invParent = glm::inverse(parentWtc->matrix);
                    math::Vec4 localPos4 = invParent * math::Vec4(worldPos, 1.0f);
                    tc->position = math::Vec3(localPos4);

                    // Extract parent rotation and compute local rotation
                    math::Vec3 parentScale;
                    math::Quat parentRot;
                    math::Vec3 parentPos;
                    math::Vec3 skew;
                    math::Vec4 perspective;
                    glm::decompose(parentWtc->matrix, parentScale, parentRot, parentPos, skew,
                                   perspective);
                    parentRot = glm::normalize(parentRot);
                    tc->rotation = glm::normalize(glm::inverse(parentRot) * worldRot);
                    tc->flags |= 0x01;  // mark dirty
                    return;
                }
            }

            // Root entity: write directly
            tc->position = worldPos;
            tc->rotation = worldRot;
            tc->flags |= 0x01;  // mark dirty so TransformSystem recomputes world matrix
        });
}

void PhysicsSystem::cleanupDestroyedBodies(ecs::Registry& reg, IPhysicsEngine& physics)
{
    // Downcast to get body-entity map. This coupling is acceptable since
    // JoltPhysicsEngine is the only concrete implementation.
    auto* joltEngine = dynamic_cast<JoltPhysicsEngine*>(&physics);
    if (!joltEngine)
    {
        return;
    }

    const auto& bodyEntityMap = joltEngine->getBodyEntityMap();
    memory::InlinedVector<uint32_t, 16> toRemove;

    for (const auto& [bodyID, entityID] : bodyEntityMap)
    {
        if (!reg.isValid(entityID))
        {
            toRemove.push_back(bodyID);
        }
    }

    for (uint32_t bodyID : toRemove)
    {
        physics.removeBody(bodyID);
    }
}

}  // namespace engine::physics
