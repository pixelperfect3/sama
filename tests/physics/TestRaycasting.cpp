#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "engine/ecs/Registry.h"
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsSystem.h"
#include "engine/rendering/EcsComponents.h"

using namespace engine::ecs;
using namespace engine::physics;
using namespace engine::rendering;
using namespace engine::math;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static EntityID createBoxEntity(Registry& reg, JoltPhysicsEngine& physics, Vec3 position,
                                BodyType type = BodyType::Static, Vec3 halfExtents = Vec3{0.5f})
{
    EntityID e = reg.createEntity();
    Quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 one{1.0f, 1.0f, 1.0f};
    reg.emplace<TransformComponent>(e, TransformComponent{position, identity, one, 0, {}});
    reg.emplace<WorldTransformComponent>(
        e, WorldTransformComponent{glm::translate(Mat4(1.0f), position)});

    RigidBodyComponent rb;
    rb.type = type;
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

TEST_CASE("Ray hits a box collider", "[physics][raycast]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    Registry reg;
    PhysicsSystem sys;

    // Place a box at (0, 0, 5)
    EntityID box = createBoxEntity(reg, physics, Vec3{0.0f, 0.0f, 5.0f});

    // Register body
    sys.update(reg, physics, 1.0f / 60.0f);

    // Cast a ray from origin along +Z
    RayHit hit;
    bool didHit =
        physics.rayCastClosest(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, 100.0f, hit);

    CHECK(didHit);
    CHECK(hit.entity == box);
    CHECK(hit.fraction > 0.0f);
    CHECK(hit.fraction < 1.0f);
    // Hit point should be on the near face of the box (z ~= 4.5)
    CHECK(std::abs(hit.point.z - 4.5f) < 0.1f);

    physics.shutdown();
}

TEST_CASE("Ray misses returns false", "[physics][raycast]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    Registry reg;
    PhysicsSystem sys;

    // Place a box at (0, 0, 5)
    createBoxEntity(reg, physics, Vec3{0.0f, 0.0f, 5.0f});
    sys.update(reg, physics, 1.0f / 60.0f);

    // Cast a ray in the opposite direction
    RayHit hit;
    bool didHit =
        physics.rayCastClosest(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f}, 100.0f, hit);

    CHECK_FALSE(didHit);

    physics.shutdown();
}

TEST_CASE("rayCastAll returns multiple sorted hits", "[physics][raycast]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    Registry reg;
    PhysicsSystem sys;

    // Place boxes along the +Z axis at different distances
    EntityID near = createBoxEntity(reg, physics, Vec3{0.0f, 0.0f, 3.0f});
    EntityID far = createBoxEntity(reg, physics, Vec3{0.0f, 0.0f, 8.0f});

    sys.update(reg, physics, 1.0f / 60.0f);

    auto hits = physics.rayCastAll(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, 100.0f);

    // Should hit both boxes
    REQUIRE(hits.size() >= 2);

    // Hits should be sorted by fraction (nearest first)
    CHECK(hits[0].fraction <= hits[1].fraction);

    // Nearest hit should be the near box
    CHECK(hits[0].entity == near);

    physics.shutdown();
}
