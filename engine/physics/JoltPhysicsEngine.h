#pragma once

// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <ankerl/unordered_dense.h>

#include <memory>

#include "engine/physics/IPhysicsEngine.h"
#include "engine/physics/JoltContactListener.h"
#include "engine/physics/JoltLayerConfig.h"

namespace engine::physics
{

class JoltPhysicsEngine final : public IPhysicsEngine
{
public:
    JoltPhysicsEngine() = default;
    ~JoltPhysicsEngine() override;

    // IPhysicsEngine interface
    bool init() override;
    void shutdown() override;
    void step(float deltaTime, int maxSubSteps = 4) override;

    uint32_t addBody(const BodyDesc& desc) override;
    void removeBody(uint32_t bodyID) override;
    void destroyAllBodies() override;

    void getBodyTransform(uint32_t bodyID, math::Vec3& outPos, math::Quat& outRot) const override;
    void moveKinematic(uint32_t bodyID, const math::Vec3& targetPos, const math::Quat& targetRot,
                       float deltaTime) override;

    void setBodyPosition(uint32_t bodyID, const math::Vec3& position) override;
    void setBodyRotation(uint32_t bodyID, const math::Quat& rotation) override;

    void applyForce(uint32_t bodyID, const math::Vec3& force) override;
    void applyImpulse(uint32_t bodyID, const math::Vec3& impulse) override;
    void setLinearVelocity(uint32_t bodyID, const math::Vec3& velocity) override;
    void setAngularVelocity(uint32_t bodyID, const math::Vec3& velocity) override;
    math::Vec3 getLinearVelocity(uint32_t bodyID) const override;

    bool rayCastClosest(const math::Vec3& origin, const math::Vec3& direction, float maxDistance,
                        RayHit& outHit) const override;
    std::vector<RayHit> rayCastAll(const math::Vec3& origin, const math::Vec3& direction,
                                   float maxDistance) const override;

    const std::vector<ContactEvent>& getContactBeginEvents() const override;
    const std::vector<ContactEvent>& getContactEndEvents() const override;

    // For cleanup: map of body ID -> entity ID
    const ankerl::unordered_dense::map<uint32_t, ecs::EntityID>& getBodyEntityMap() const;

private:
    JPH::ObjectLayer bodyTypeToObjectLayer(BodyType type, uint8_t layer) const;

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;

    BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface_;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseFilter_;
    ObjectLayerPairFilterImpl objectLayerPairFilter_;
    JoltContactListener contactListener_;

    // Entity <-> body mappings
    ankerl::unordered_dense::map<uint32_t, ecs::EntityID> bodyToEntity_;

    bool initialized_ = false;
};

}  // namespace engine::physics
