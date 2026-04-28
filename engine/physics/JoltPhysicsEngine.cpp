#include "engine/physics/JoltPhysicsEngine.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdio>

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

    // Drop the engine's hold on every pre-built shape. Any shape still
    // referenced by a body that already lived through the loop above would
    // already be unreferenced here; in practice both maps are empty by now.
    meshShapes_.clear();
    compoundShapes_.clear();

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

    // Create shape based on collider type. Sanitize half-extents and pick a
    // matching convex radius so Jolt's `inHalfExtent.ReduceMin() >= convex
    // radius` assert never trips on thin boxes (e.g. a 20x0.01x20 ground
    // slab). Default convex radius is 0.05, which would crash anything
    // thinner than 5cm — instead we shrink the convex radius to fit.
    constexpr float kMinHalfExtent = 1e-4f;  // hard floor; below this we refuse
    auto sanitizeHalf = [&](math::Vec3 in)
    {
        return math::Vec3{std::max(in.x, kMinHalfExtent), std::max(in.y, kMinHalfExtent),
                          std::max(in.z, kMinHalfExtent)};
    };

    JPH::ShapeRefC shape;
    switch (desc.shape)
    {
        case ColliderShape::Box:
        {
            math::Vec3 half = sanitizeHalf(desc.halfExtents);
            float minHalf = std::min({half.x, half.y, half.z});
            // Convex radius must be <= every half-extent. Clamp to a small
            // fraction of the smallest extent (Jolt also requires it >= 0).
            float convexRadius = std::min(0.05f, minHalf * 0.5f);
            if (convexRadius < 0.0f)
                convexRadius = 0.0f;
            if (half.x < kMinHalfExtent || half.y < kMinHalfExtent || half.z < kMinHalfExtent)
            {
                fprintf(stderr,
                        "[physics] addBody: invalid half-extents (%.4f, %.4f, %.4f) for "
                        "entity %u — refusing to create body\n",
                        desc.halfExtents.x, desc.halfExtents.y, desc.halfExtents.z,
                        static_cast<uint32_t>(desc.entity));
                return ~0u;
            }
            shape = new JPH::BoxShape(toJolt(half), convexRadius);
            break;
        }
        case ColliderShape::Sphere:
        {
            float r = std::max(desc.radius, kMinHalfExtent);
            shape = new JPH::SphereShape(r);
            break;
        }
        case ColliderShape::Capsule:
        {
            float halfH = std::max(desc.halfExtents.y, kMinHalfExtent);
            float r = std::max(desc.radius, kMinHalfExtent);
            shape = new JPH::CapsuleShape(halfH, r);
            break;
        }
        case ColliderShape::Mesh:
        {
            auto it = meshShapes_.find(desc.shapeID);
            if (it == meshShapes_.end())
            {
                fprintf(stderr,
                        "[physics] addBody: ColliderShape::Mesh requires a valid shapeID "
                        "(got %u) for entity %u — refusing to create body\n",
                        desc.shapeID, static_cast<uint32_t>(desc.entity));
                return ~0u;
            }
            shape = it->second.shape;
            break;
        }
        case ColliderShape::Compound:
        {
            auto it = compoundShapes_.find(desc.shapeID);
            if (it == compoundShapes_.end())
            {
                fprintf(stderr,
                        "[physics] addBody: ColliderShape::Compound requires a valid shapeID "
                        "(got %u) for entity %u — refusing to create body\n",
                        desc.shapeID, static_cast<uint32_t>(desc.entity));
                return ~0u;
            }
            shape = it->second.shape;
            break;
        }
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
    settings.mIsSensor = desc.isSensor;

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

void JoltPhysicsEngine::destroyAllBodies()
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    for (auto& [bodyIDVal, entityID] : bodyToEntity_)
    {
        JPH::BodyID jphBodyID(bodyIDVal);
        bodyInterface.RemoveBody(jphBodyID);
        bodyInterface.DestroyBody(jphBodyID);
    }
    bodyToEntity_.clear();
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

void JoltPhysicsEngine::setBodyPosition(uint32_t bodyID, const math::Vec3& position)
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyID jphBodyID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.SetPosition(jphBodyID, toJolt(position), JPH::EActivation::Activate);
}

void JoltPhysicsEngine::setBodyRotation(uint32_t bodyID, const math::Quat& rotation)
{
    if (!initialized_)
    {
        return;
    }

    JPH::BodyID jphBodyID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.SetRotation(jphBodyID, toJolt(rotation), JPH::EActivation::Activate);
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

const ankerl::unordered_dense::map<uint32_t, ecs::EntityID>& JoltPhysicsEngine::getBodyEntityMap()
    const
{
    return bodyToEntity_;
}

uint32_t JoltPhysicsEngine::createMeshShape(const float* positions, size_t vertexCount,
                                            const uint32_t* indices, size_t indexCount)
{
    if (!initialized_ || !positions || !indices || vertexCount == 0 || indexCount < 3 ||
        (indexCount % 3) != 0)
    {
        return ~0u;
    }

    JPH::TriangleList tris;
    tris.reserve(indexCount / 3);

    auto vert = [&](uint32_t idx) -> JPH::Float3
    {
        const float* p = positions + (idx * 3);
        return JPH::Float3(p[0], p[1], p[2]);
    };

    for (size_t i = 0; i < indexCount; i += 3)
    {
        const uint32_t i0 = indices[i + 0];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
        {
            return ~0u;
        }
        tris.emplace_back(vert(i0), vert(i1), vert(i2));
    }

    JPH::MeshShapeSettings settings(std::move(tris));
    settings.SetEmbedded();
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError())
    {
        fprintf(stderr, "[physics] createMeshShape failed: %s\n", result.GetError().c_str());
        return ~0u;
    }

    const uint32_t id = nextShapeID_++;
    meshShapes_.emplace(id, ShapeEntry{result.Get()});
    return id;
}

void JoltPhysicsEngine::destroyMeshShape(uint32_t shapeID)
{
    // Erasing the registry entry drops the engine's JPH::ShapeRefC. Bodies
    // already referencing the shape keep their own refs; the underlying shape
    // outlives this call as long as any body references it.
    meshShapes_.erase(shapeID);
}

uint32_t JoltPhysicsEngine::createCompoundShape(const CompoundChild* children, size_t count)
{
    if (!initialized_ || !children || count == 0)
    {
        return ~0u;
    }

    constexpr float kMinHalfExtent = 1e-4f;

    JPH::StaticCompoundShapeSettings settings;
    settings.SetEmbedded();

    for (size_t i = 0; i < count; ++i)
    {
        const CompoundChild& child = children[i];
        JPH::ShapeRefC childShape;
        switch (child.shape)
        {
            case ColliderShape::Box:
            {
                math::Vec3 half{std::max(child.halfExtents.x, kMinHalfExtent),
                                std::max(child.halfExtents.y, kMinHalfExtent),
                                std::max(child.halfExtents.z, kMinHalfExtent)};
                float minHalf = std::min({half.x, half.y, half.z});
                float convexRadius = std::min(0.05f, minHalf * 0.5f);
                if (convexRadius < 0.0f)
                    convexRadius = 0.0f;
                childShape = new JPH::BoxShape(toJolt(half), convexRadius);
                break;
            }
            case ColliderShape::Sphere:
            {
                float r = std::max(child.radius, kMinHalfExtent);
                childShape = new JPH::SphereShape(r);
                break;
            }
            case ColliderShape::Capsule:
            {
                float halfH = std::max(child.halfHeight, kMinHalfExtent);
                float r = std::max(child.radius, kMinHalfExtent);
                childShape = new JPH::CapsuleShape(halfH, r);
                break;
            }
            case ColliderShape::Mesh:
            case ColliderShape::Compound:
            default:
                fprintf(stderr,
                        "[physics] createCompoundShape: child %zu uses unsupported shape "
                        "(only Box / Sphere / Capsule allowed)\n",
                        i);
                return ~0u;
        }
        settings.AddShape(toJolt(child.localPosition), toJolt(child.localRotation), childShape);
    }

    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError())
    {
        fprintf(stderr, "[physics] createCompoundShape failed: %s\n", result.GetError().c_str());
        return ~0u;
    }

    const uint32_t id = nextShapeID_++;
    compoundShapes_.emplace(id, ShapeEntry{result.Get()});
    return id;
}

void JoltPhysicsEngine::destroyCompoundShape(uint32_t shapeID)
{
    // Same lifetime model as destroyMeshShape: drop the engine's hold; bodies
    // referencing the shape keep it alive via their own JPH::ShapeRefC.
    compoundShapes_.erase(shapeID);
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
