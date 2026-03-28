#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include <mutex>
#include <vector>

#include "engine/physics/IPhysicsEngine.h"

namespace JPH
{
class PhysicsSystem;
}

namespace engine::physics
{

class JoltContactListener final : public JPH::ContactListener
{
public:
    // Called by JoltPhysicsEngine before each step to clear previous events.
    void clearEvents();

    // Read access to collected events (valid until next clearEvents call).
    const std::vector<IPhysicsEngine::ContactEvent>& getBeginEvents() const;
    const std::vector<IPhysicsEngine::ContactEvent>& getEndEvents() const;

    // JPH::ContactListener overrides
    JPH::ValidateResult OnContactValidate(
        const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset,
        const JPH::CollideShapeResult& inCollisionResult) override;

    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                        const JPH::ContactManifold& inManifold,
                        JPH::ContactSettings& ioSettings) override;

    void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                            const JPH::ContactManifold& inManifold,
                            JPH::ContactSettings& ioSettings) override;

    void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

    // Set the physics system so we can look up bodies for OnContactRemoved.
    void setPhysicsSystem(JPH::PhysicsSystem* system);

private:
    std::vector<IPhysicsEngine::ContactEvent> beginEvents_;
    std::vector<IPhysicsEngine::ContactEvent> endEvents_;
    std::mutex mutex_;
    JPH::PhysicsSystem* physicsSystem_ = nullptr;
};

}  // namespace engine::physics
