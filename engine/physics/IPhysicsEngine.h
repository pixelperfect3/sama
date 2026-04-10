#pragma once

#include <vector>

#include "engine/ecs/Entity.h"
#include "engine/math/Types.h"
#include "engine/physics/PhysicsComponents.h"

namespace engine::physics
{

struct RayHit
{
    ecs::EntityID entity = ecs::INVALID_ENTITY;
    math::Vec3 point{0.0f};
    math::Vec3 normal{0.0f};
    float fraction = 0.0f;  // [0,1] along ray
};

struct BodyDesc
{
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    ColliderShape shape = ColliderShape::Box;
    math::Vec3 halfExtents{0.5f};
    float radius = 0.5f;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    BodyType type = BodyType::Dynamic;
    uint8_t layer = 0;
    bool isSensor = false;                       // sensor: overlap-only, no physical response
    ecs::EntityID entity = ecs::INVALID_ENTITY;  // back-reference for callbacks
};

class IPhysicsEngine
{
public:
    virtual ~IPhysicsEngine() = default;

    // Lifecycle
    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // Simulation
    virtual void step(float deltaTime, int maxSubSteps = 4) = 0;

    // Body management
    virtual uint32_t addBody(const BodyDesc& desc) = 0;
    virtual void removeBody(uint32_t bodyID) = 0;

    // Destroy every body currently owned by the engine.  Used by the editor
    // to reset physics state on Play/Stop transitions, where per-body velocity
    // and sleep state cannot be cleanly snapshotted.
    virtual void destroyAllBodies() = 0;

    // Transform queries (world-space)
    virtual void getBodyTransform(uint32_t bodyID, math::Vec3& outPos,
                                  math::Quat& outRot) const = 0;
    virtual void moveKinematic(uint32_t bodyID, const math::Vec3& targetPos,
                               const math::Quat& targetRot, float deltaTime) = 0;

    // Direct position/rotation setters (for teleporting dynamic bodies)
    virtual void setBodyPosition(uint32_t bodyID, const math::Vec3& position) = 0;
    virtual void setBodyRotation(uint32_t bodyID, const math::Quat& rotation) = 0;

    // Forces and impulses
    virtual void applyForce(uint32_t bodyID, const math::Vec3& force) = 0;
    virtual void applyImpulse(uint32_t bodyID, const math::Vec3& impulse) = 0;
    virtual void setLinearVelocity(uint32_t bodyID, const math::Vec3& velocity) = 0;
    virtual void setAngularVelocity(uint32_t bodyID, const math::Vec3& velocity) = 0;
    virtual math::Vec3 getLinearVelocity(uint32_t bodyID) const = 0;

    // Raycasting
    virtual bool rayCastClosest(const math::Vec3& origin, const math::Vec3& direction,
                                float maxDistance, RayHit& outHit) const = 0;
    virtual std::vector<RayHit> rayCastAll(const math::Vec3& origin, const math::Vec3& direction,
                                           float maxDistance) const = 0;

    // Collision event polling (filled during step, cleared at next step)
    struct ContactEvent
    {
        ecs::EntityID entityA = ecs::INVALID_ENTITY;
        ecs::EntityID entityB = ecs::INVALID_ENTITY;
        math::Vec3 contactPoint{0.0f};
        math::Vec3 contactNormal{0.0f};
        float penetrationDepth = 0.0f;
    };

    virtual const std::vector<ContactEvent>& getContactBeginEvents() const = 0;
    virtual const std::vector<ContactEvent>& getContactEndEvents() const = 0;
};

}  // namespace engine::physics
