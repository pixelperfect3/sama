#include "engine/physics/JoltPhysicsEngine.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>

#include "engine/physics/PhysicsConversions.h"

// Jolt asserts callback
JPH_SUPPRESS_WARNINGS

#ifdef JPH_ENABLE_ASSERTS
static bool JoltAssertFailed(const char* inExpression, const char* inMessage, const char* inFile,
                             JPH::uint inLine)
{
    (void)inExpression;
    (void)inMessage;
    (void)inFile;
    (void)inLine;
    return true;  // break into debugger
}
#endif

namespace engine::physics
{

static constexpr uint32_t kMaxBodies = 10240;
static constexpr uint32_t kNumBodyMutexes = 0;  // default
static constexpr uint32_t kMaxBodyPairs = 10240;
static constexpr uint32_t kMaxContactConstraints = 10240;

JoltPhysicsEngine::~JoltPhysicsEngine()
{
    if (initialized_)
    {
        shutdown();
    }
}

bool JoltPhysicsEngine::init()
{
    if (initialized_)
    {
        return false;
    }

    JPH::RegisterDefaultAllocator();

#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = JoltAssertFailed;
#endif

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);  // 10 MB
    jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs,
                                                            JPH::cMaxPhysicsBarriers, 1);

    physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
    physicsSystem_->Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
                         broadPhaseLayerInterface_, objectVsBroadPhaseFilter_,
                         objectLayerPairFilter_);

    // Set gravity to standard Earth gravity
    physicsSystem_->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    // Install contact listener
    contactListener_.setPhysicsSystem(physicsSystem_.get());
    physicsSystem_->SetContactListener(&contactListener_);

    initialized_ = true;
    return true;
}

void JoltPhysicsEngine::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    // Remove all remaining bodies
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    for (auto& [bodyIDVal, entityID] : bodyToEntity_)
    {
        JPH::BodyID jphBodyID(bodyIDVal);
        bodyInterface.RemoveBody(jphBodyID);
        bodyInterface.DestroyBody(jphBodyID);
    }
    bodyToEntity_.clear();

    physicsSystem_.reset();
    jobSystem_.reset();
    tempAllocator_.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    initialized_ = false;
}

void JoltPhysicsEngine::step(float deltaTime, int maxSubSteps)
{
    if (!initialized_)
    {
        return;
    }

    contactListener_.clearEvents();
    physicsSystem_->Update(deltaTime, maxSubSteps, tempAllocator_.get(), jobSystem_.get());
}

uint32_t JoltPhysicsEngine::addBody(const BodyDesc& desc)
{
    if (!initialized_)
    {
        return ~0u;
    }

    // Create shape based on collider type
    JPH::ShapeRefC shape;
    switch (desc.shape)
    {
        case ColliderShape::Box:
            shape = new JPH::BoxShape(toJolt(desc.halfExtents));
            break;
        case ColliderShape::Sphere:
            shape = new JPH::SphereShape(desc.radius);
            break;
        case ColliderShape::Capsule:
            shape = new JPH::CapsuleShape(desc.halfExtents.y, desc.radius);
            break;
        case ColliderShape::Mesh:
            // Mesh colliders are not yet supported; fall back to box.
            shape = new JPH::BoxShape(toJolt(desc.halfExtents));
            break;
    }

    // Determine motion type and layer
    JPH::EMotionType motionType;
    JPH::ObjectLayer objectLayer = bodyTypeToObjectLayer(desc.type, desc.layer);
    switch (desc.type)
    {
        case BodyType::Static:
            motionType = JPH::EMotionType::Static;
            break;
        case BodyType::Kinematic:
            motionType = JPH::EMotionType::Kinematic;
            break;
        case BodyType::Dynamic:
        default:
            motionType = JPH::EMotionType::Dynamic;
            break;
    }

    JPH::BodyCreationSettings settings(shape, toJolt(desc.position), toJolt(desc.rotation),
                                       motionType, objectLayer);

    if (desc.type == BodyType::Dynamic)
    {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mLinearDamping = desc.linearDamping;
    settings.mAngularDamping = desc.angularDamping;

    // Store entity ID in user data for contact listener lookups
    settings.mUserData = static_cast<uint64_t>(desc.entity);

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (!body)
    {
        return ~0u;
    }

    JPH::BodyID jphBodyID = body->GetID();
    bodyInterface.AddBody(jphBodyID, JPH::EActivation::Activate);

    uint32_t bodyIDVal = jphBodyID.GetIndexAndSequenceNumber();
    bodyToEntity_[bodyIDVal] = desc.entity;
    return bodyIDVal;
}

void JoltPhysicsEngine::removeBody(uint32_t bodyID)
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyID jphBodyID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.RemoveBody(jphBodyID);
    bodyInterface.DestroyBody(jphBodyID);
    bodyToEntity_.erase(bodyID);
}

void JoltPhysicsEngine::getBodyTransform(uint32_t bodyID, math::Vec3& outPos,
                                         math::Quat& outRot) const
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyID jphBodyID(bodyID);
    const JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterfaceNoLock();
    outPos = fromJolt(bodyInterface.GetPosition(jphBodyID));
    outRot = fromJolt(bodyInterface.GetRotation(jphBodyID));
}

void JoltPhysicsEngine::moveKinematic(uint32_t bodyID, const math::Vec3& targetPos,
                                      const math::Quat& targetRot, float deltaTime)
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyID jphBodyID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.MoveKinematic(jphBodyID, toJolt(targetPos), toJolt(targetRot), deltaTime);
}

void JoltPhysicsEngine::applyForce(uint32_t bodyID, const math::Vec3& force)
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyID jphBodyID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.AddForce(jphBodyID, toJolt(force));
}

void JoltPhysicsEngine::applyImpulse(uint32_t bodyID, const math::Vec3& impulse)
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyID jphBodyID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.AddImpulse(jphBodyID, toJolt(impulse));
}

void JoltPhysicsEngine::setLinearVelocity(uint32_t bodyID, const math::Vec3& velocity)
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyID jphBodyID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.SetLinearVelocity(jphBodyID, toJolt(velocity));
}

void JoltPhysicsEngine::setAngularVelocity(uint32_t bodyID, const math::Vec3& velocity)
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyID jphBodyID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.SetAngularVelocity(jphBodyID, toJolt(velocity));
}

math::Vec3 JoltPhysicsEngine::getLinearVelocity(uint32_t bodyID) const
{
    if (!initialized_)
    {
        return math::Vec3(0.0f);
    }

    JPH::BodyID jphBodyID(bodyID);
    const JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterfaceNoLock();
    return fromJolt(bodyInterface.GetLinearVelocity(jphBodyID));
}

bool JoltPhysicsEngine::rayCastClosest(const math::Vec3& origin, const math::Vec3& direction,
                                       float maxDistance, RayHit& outHit) const
{
    if (!initialized_)
    {
        return false;
    }

    math::Vec3 normalizedDir = glm::normalize(direction);
    JPH::RRayCast ray(toJolt(origin), toJolt(normalizedDir) * maxDistance);

    JPH::RayCastResult result;
    const JPH::NarrowPhaseQuery& query = physicsSystem_->GetNarrowPhaseQuery();
    bool hit = query.CastRay(ray, result);

    if (hit)
    {
        JPH::BodyID hitBodyID = result.mBodyID;
        const JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterfaceNoLock();

        outHit.fraction = result.mFraction;
        outHit.point = fromJolt(ray.GetPointOnRay(result.mFraction));

        // Get entity from user data
        JPH::BodyLockRead lock(physicsSystem_->GetBodyLockInterfaceNoLock(), hitBodyID);
        if (lock.Succeeded())
        {
            outHit.entity = static_cast<ecs::EntityID>(lock.GetBody().GetUserData());

            // Get surface normal at hit point
            JPH::Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(
                result.mSubShapeID2, ray.GetPointOnRay(result.mFraction));
            outHit.normal = fromJolt(normal);
        }
    }

    return hit;
}

std::vector<RayHit> JoltPhysicsEngine::rayCastAll(const math::Vec3& origin,
                                                  const math::Vec3& direction,
                                                  float maxDistance) const
{
    std::vector<RayHit> hits;
    if (!initialized_)
    {
        return hits;
    }

    math::Vec3 normalizedDir = glm::normalize(direction);
    JPH::RRayCast ray(toJolt(origin), toJolt(normalizedDir) * maxDistance);

    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    const JPH::NarrowPhaseQuery& query = physicsSystem_->GetNarrowPhaseQuery();
    query.CastRay(ray, JPH::RayCastSettings(), collector);

    collector.Sort();

    for (auto& result : collector.mHits)
    {
        RayHit hit;
        hit.fraction = result.mFraction;
        hit.point = fromJolt(ray.GetPointOnRay(result.mFraction));

        JPH::BodyLockRead lock(physicsSystem_->GetBodyLockInterfaceNoLock(), result.mBodyID);
        if (lock.Succeeded())
        {
            hit.entity = static_cast<ecs::EntityID>(lock.GetBody().GetUserData());
            JPH::Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(
                result.mSubShapeID2, ray.GetPointOnRay(result.mFraction));
            hit.normal = fromJolt(normal);
        }
        hits.push_back(hit);
    }

    return hits;
}

const std::vector<IPhysicsEngine::ContactEvent>& JoltPhysicsEngine::getContactBeginEvents() const
{
    return contactListener_.getBeginEvents();
}

const std::vector<IPhysicsEngine::ContactEvent>& JoltPhysicsEngine::getContactEndEvents() const
{
    return contactListener_.getEndEvents();
}

const std::unordered_map<uint32_t, ecs::EntityID>& JoltPhysicsEngine::getBodyEntityMap() const
{
    return bodyToEntity_;
}

JPH::ObjectLayer JoltPhysicsEngine::bodyTypeToObjectLayer(BodyType type, uint8_t /*layer*/) const
{
    switch (type)
    {
        case BodyType::Static:
            return Layers::STATIC;
        case BodyType::Dynamic:
            return Layers::DYNAMIC;
        case BodyType::Kinematic:
            return Layers::KINEMATIC;
        default:
            return Layers::DYNAMIC;
    }
}

}  // namespace engine::physics
