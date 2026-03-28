#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "engine/ecs/Registry.h"
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsSystem.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/SceneGraph.h"
#include "engine/scene/TransformSystem.h"

using namespace engine::ecs;
using namespace engine::physics;
using namespace engine::rendering;
using namespace engine::math;
using namespace engine::scene;

// ---------------------------------------------------------------------------
// Helper: create a minimal physics entity with a box collider
// ---------------------------------------------------------------------------

static EntityID createPhysicsEntity(Registry& reg, Vec3 position, BodyType type,
                                    Vec3 halfExtents = Vec3{0.5f})
{
    EntityID e = reg.createEntity();
    Quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 one{1.0f, 1.0f, 1.0f};
    reg.emplace<TransformComponent>(e, TransformComponent{position, identity, one, 0, {}});
    reg.emplace<WorldTransformComponent>(
        e, WorldTransformComponent{glm::translate(Mat4(1.0f), position)});

    RigidBodyComponent rb;
    rb.type = type;
    rb.mass = 1.0f;
    reg.emplace<RigidBodyComponent>(e, rb);

    ColliderComponent col;
    col.shape = ColliderShape::Box;
    col.halfExtents = halfExtents;
    reg.emplace<ColliderComponent>(e, col);

    return e;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("Entity gets PhysicsBodyCreatedTag after registration", "[physics][system]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    Registry reg;
    PhysicsSystem sys;

    EntityID e = createPhysicsEntity(reg, Vec3{0.0f, 10.0f, 0.0f}, BodyType::Dynamic);

    CHECK_FALSE(reg.has<PhysicsBodyCreatedTag>(e));

    sys.update(reg, physics, 1.0f / 60.0f);

    CHECK(reg.has<PhysicsBodyCreatedTag>(e));

    auto* rb = reg.get<RigidBodyComponent>(e);
    REQUIRE(rb != nullptr);
    CHECK(rb->bodyID != ~0u);

    physics.shutdown();
}

TEST_CASE("Dynamic body falls under gravity", "[physics][system]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    Registry reg;
    PhysicsSystem sys;

    EntityID e = createPhysicsEntity(reg, Vec3{0.0f, 10.0f, 0.0f}, BodyType::Dynamic);

    float initialY = 10.0f;

    // Run several physics steps
    for (int i = 0; i < 60; ++i)
    {
        sys.update(reg, physics, 1.0f / 60.0f);
    }

    auto* tc = reg.get<TransformComponent>(e);
    REQUIRE(tc != nullptr);
    CHECK(tc->position.y < initialY);

    physics.shutdown();
}

TEST_CASE("Static body does not move", "[physics][system]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    Registry reg;
    PhysicsSystem sys;

    Vec3 startPos{0.0f, 0.0f, 0.0f};
    EntityID e = createPhysicsEntity(reg, startPos, BodyType::Static);

    // Run several physics steps
    for (int i = 0; i < 60; ++i)
    {
        sys.update(reg, physics, 1.0f / 60.0f);
    }

    auto* tc = reg.get<TransformComponent>(e);
    REQUIRE(tc != nullptr);
    CHECK(std::abs(tc->position.x - startPos.x) < 1e-4f);
    CHECK(std::abs(tc->position.y - startPos.y) < 1e-4f);
    CHECK(std::abs(tc->position.z - startPos.z) < 1e-4f);

    physics.shutdown();
}

TEST_CASE("Entity destroyed: body removed from Jolt", "[physics][system]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    Registry reg;
    PhysicsSystem sys;

    EntityID e = createPhysicsEntity(reg, Vec3{0.0f, 5.0f, 0.0f}, BodyType::Dynamic);

    // Register the body
    sys.update(reg, physics, 1.0f / 60.0f);
    CHECK(reg.has<PhysicsBodyCreatedTag>(e));

    size_t bodyCountBefore = physics.getBodyEntityMap().size();
    CHECK(bodyCountBefore == 1);

    // Destroy the entity
    reg.destroyEntity(e);

    // Run update to trigger cleanup
    sys.update(reg, physics, 1.0f / 60.0f);

    CHECK(physics.getBodyEntityMap().size() == 0);

    physics.shutdown();
}

TEST_CASE("Kinematic body tracks TransformComponent changes", "[physics][system]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    Registry reg;
    PhysicsSystem sys;
    TransformSystem transformSys;

    EntityID e = createPhysicsEntity(reg, Vec3{0.0f, 0.0f, 0.0f}, BodyType::Kinematic);

    // Initial registration
    sys.update(reg, physics, 1.0f / 60.0f);
    CHECK(reg.has<PhysicsBodyCreatedTag>(e));

    // Move the kinematic body via TransformComponent
    auto* tc = reg.get<TransformComponent>(e);
    REQUIRE(tc != nullptr);
    tc->position = Vec3{5.0f, 3.0f, 0.0f};
    tc->flags |= 0x01;  // mark dirty so TransformSystem recomputes

    // Recompose world matrix
    transformSys.update(reg);

    // Step physics (kinematic sync reads WorldTransformComponent)
    sys.update(reg, physics, 1.0f / 60.0f);

    // Verify the Jolt body moved to the new position
    auto* rb = reg.get<RigidBodyComponent>(e);
    REQUIRE(rb != nullptr);

    Vec3 bodyPos;
    Quat bodyRot;
    physics.getBodyTransform(rb->bodyID, bodyPos, bodyRot);

    CHECK(std::abs(bodyPos.x - 5.0f) < 0.5f);
    CHECK(std::abs(bodyPos.y - 3.0f) < 0.5f);

    physics.shutdown();
}

TEST_CASE("Multiple bodies registered correctly", "[physics][system]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    Registry reg;
    PhysicsSystem sys;

    EntityID e1 = createPhysicsEntity(reg, Vec3{0.0f, 5.0f, 0.0f}, BodyType::Dynamic);
    EntityID e2 = createPhysicsEntity(reg, Vec3{5.0f, 5.0f, 0.0f}, BodyType::Static);
    EntityID e3 = createPhysicsEntity(reg, Vec3{10.0f, 5.0f, 0.0f}, BodyType::Kinematic);

    sys.update(reg, physics, 1.0f / 60.0f);

    CHECK(reg.has<PhysicsBodyCreatedTag>(e1));
    CHECK(reg.has<PhysicsBodyCreatedTag>(e2));
    CHECK(reg.has<PhysicsBodyCreatedTag>(e3));
    CHECK(physics.getBodyEntityMap().size() == 3);

    physics.shutdown();
}
