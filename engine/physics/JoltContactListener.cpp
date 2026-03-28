#include "engine/physics/JoltContactListener.h"

#include <Jolt/Physics/PhysicsSystem.h>

#include "engine/physics/PhysicsConversions.h"

namespace engine::physics
{

void JoltContactListener::clearEvents()
{
    beginEvents_.clear();
    endEvents_.clear();
}

const std::vector<IPhysicsEngine::ContactEvent>& JoltContactListener::getBeginEvents() const
{
    return beginEvents_;
}

const std::vector<IPhysicsEngine::ContactEvent>& JoltContactListener::getEndEvents() const
{
    return endEvents_;
}

void JoltContactListener::setPhysicsSystem(JPH::PhysicsSystem* system)
{
    physicsSystem_ = system;
}

JPH::ValidateResult JoltContactListener::OnContactValidate(
    const JPH::Body& /*inBody1*/, const JPH::Body& /*inBody2*/, JPH::RVec3Arg /*inBaseOffset*/,
    const JPH::CollideShapeResult& /*inCollisionResult*/)
{
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
}

void JoltContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                         const JPH::ContactManifold& inManifold,
                                         JPH::ContactSettings& /*ioSettings*/)
{
    IPhysicsEngine::ContactEvent event;
    event.entityA = static_cast<ecs::EntityID>(inBody1.GetUserData());
    event.entityB = static_cast<ecs::EntityID>(inBody2.GetUserData());
    event.contactPoint = fromJolt(inManifold.GetWorldSpaceContactPointOn1(0));
    event.contactNormal = fromJolt(inManifold.mWorldSpaceNormal);
    event.penetrationDepth = inManifold.mPenetrationDepth;

    std::lock_guard<std::mutex> lock(mutex_);
    beginEvents_.push_back(event);
}

void JoltContactListener::OnContactPersisted(const JPH::Body& /*inBody1*/,
                                             const JPH::Body& /*inBody2*/,
                                             const JPH::ContactManifold& /*inManifold*/,
                                             JPH::ContactSettings& /*ioSettings*/)
{
    // Deferred for v1: most games only need begin/end.
}

void JoltContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
{
    if (!physicsSystem_)
    {
        return;
    }

    IPhysicsEngine::ContactEvent event;

    // Look up bodies by their IDs to get entity user data.
    const JPH::BodyLockInterfaceNoLock& lockInterface =
        physicsSystem_->GetBodyLockInterfaceNoLock();
    {
        JPH::BodyLockRead lock1(lockInterface, inSubShapePair.GetBody1ID());
        if (lock1.Succeeded())
        {
            event.entityA = static_cast<ecs::EntityID>(lock1.GetBody().GetUserData());
        }
    }
    {
        JPH::BodyLockRead lock2(lockInterface, inSubShapePair.GetBody2ID());
        if (lock2.Succeeded())
        {
            event.entityB = static_cast<ecs::EntityID>(lock2.GetBody().GetUserData());
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    endEvents_.push_back(event);
}

}  // namespace engine::physics
