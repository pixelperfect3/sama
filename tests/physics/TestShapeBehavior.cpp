// Behavioral tests for compound and mesh collider shapes.
//
// TestShapeRegistry.cpp covers the registry plumbing (creation, lifetime,
// instancing). This file covers the *physical* behavior — verifying that
// the children/triangles actually participate in collision, not just that
// the registry returns valid IDs. Each test simulates a few seconds of
// physics and checks resting state or velocity outcomes.

#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "engine/ecs/Registry.h"
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsSystem.h"
#include "engine/rendering/EcsComponents.h"

using namespace engine::ecs;
using namespace engine::physics;
using namespace engine::rendering;
using namespace engine::math;

namespace
{

// ---------------------------------------------------------------------------
// Step the simulation for `seconds` of game time at 60 Hz.
// ---------------------------------------------------------------------------
void simulate(JoltPhysicsEngine& physics, PhysicsSystem& sys, Registry& reg, float seconds)
{
    constexpr float dt = 1.0f / 60.0f;
    const int steps = static_cast<int>(seconds / dt);
    for (int i = 0; i < steps; ++i)
        sys.update(reg, physics, dt);
}

// ---------------------------------------------------------------------------
// Spawn a dynamic sphere at the given position. Returns the entity.
// ---------------------------------------------------------------------------
EntityID spawnDynamicSphere(Registry& reg, Vec3 position, float radius = 0.25f, float mass = 1.0f)
{
    EntityID e = reg.createEntity();
    Quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 one{1.0f, 1.0f, 1.0f};
    reg.emplace<TransformComponent>(e, TransformComponent{position, identity, one, 0, {}});
    reg.emplace<WorldTransformComponent>(
        e, WorldTransformComponent{glm::translate(Mat4(1.0f), position)});

    RigidBodyComponent rb;
    rb.type = BodyType::Dynamic;
    rb.mass = mass;
    rb.friction = 0.5f;
    rb.restitution = 0.0f;  // no bounce — keeps the resting position deterministic
    rb.linearDamping = 0.05f;
    reg.emplace<RigidBodyComponent>(e, rb);

    ColliderComponent col;
    col.shape = ColliderShape::Sphere;
    col.radius = radius;
    reg.emplace<ColliderComponent>(e, col);
    return e;
}

// ---------------------------------------------------------------------------
// Spawn a static body that references a pre-built shape.
// ---------------------------------------------------------------------------
EntityID spawnStaticShapeBody(Registry& reg, ColliderShape shape, uint32_t shapeID,
                              Vec3 position = Vec3{0.0f})
{
    EntityID e = reg.createEntity();
    Quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 one{1.0f, 1.0f, 1.0f};
    reg.emplace<TransformComponent>(e, TransformComponent{position, identity, one, 0, {}});
    reg.emplace<WorldTransformComponent>(
        e, WorldTransformComponent{glm::translate(Mat4(1.0f), position)});

    RigidBodyComponent rb;
    rb.type = BodyType::Static;
    rb.mass = 0.0f;
    reg.emplace<RigidBodyComponent>(e, rb);

    ColliderComponent col;
    col.shape = shape;
    col.shapeID = shapeID;
    reg.emplace<ColliderComponent>(e, col);
    return e;
}

// ---------------------------------------------------------------------------
// Build a horizontal triangulated quad spanning [-extent, extent] in X/Z at
// a given Y. Two triangles, four verts. Used as a static mesh "floor".
// ---------------------------------------------------------------------------
struct TriQuad
{
    std::vector<float> positions;
    std::vector<uint32_t> indices;
};

TriQuad makeQuad(float extent, float y)
{
    TriQuad q;
    q.positions = {
        -extent, y, -extent,  // 0
        extent,  y, -extent,  // 1
        extent,  y, extent,   // 2
        -extent, y, extent,   // 3
    };
    // CCW when viewed from +Y (top).
    q.indices = {0, 2, 1, 0, 3, 2};
    return q;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Compound children participate in collision — sphere lands on top.
// ---------------------------------------------------------------------------
TEST_CASE("Compound shape: falling sphere lands on a child box top", "[physics][shapes][behavior]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    // Three boxes spaced along X, top surface at y=1 (each half-height 0.5).
    std::vector<IPhysicsEngine::CompoundChild> children;
    for (int i = 0; i < 3; ++i)
    {
        IPhysicsEngine::CompoundChild c;
        c.shape = ColliderShape::Box;
        c.localPosition = Vec3{static_cast<float>(i - 1) * 2.0f, 0.5f, 0.0f};
        c.localRotation = Quat{1.0f, 0.0f, 0.0f, 0.0f};
        c.halfExtents = Vec3{0.5f};
        children.push_back(c);
    }
    uint32_t shapeID = physics.createCompoundShape(children.data(), children.size());
    REQUIRE(shapeID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    spawnStaticShapeBody(reg, ColliderShape::Compound, shapeID, Vec3{0.0f});
    EntityID sphere = spawnDynamicSphere(reg, Vec3{0.0f, 5.0f, 0.0f});

    simulate(physics, sys, reg, 2.0f);

    // Sphere (radius 0.25) should rest with center at y ≈ 1.25 (atop centre child).
    auto* rb = reg.get<RigidBodyComponent>(sphere);
    REQUIRE(rb != nullptr);
    Vec3 pos;
    Quat rot;
    physics.getBodyTransform(rb->bodyID, pos, rot);
    CHECK(pos.y > 1.10f);  // above box top minus tolerance
    CHECK(pos.y < 1.40f);  // hasn't fallen through

    physics.shutdown();
}

// ---------------------------------------------------------------------------
// 2. Compound child positions matter — sphere over a *missing* gap falls
//    through; sphere over a present child does not.  Drives the same compound
//    twice with different drop positions.
// ---------------------------------------------------------------------------
TEST_CASE("Compound shape: gap between children lets sphere fall through",
          "[physics][shapes][behavior]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    // Two boxes at x=-3 and x=+3, leaving a gap at x=0.
    std::vector<IPhysicsEngine::CompoundChild> children;
    for (int i : {-1, 1})
    {
        IPhysicsEngine::CompoundChild c;
        c.shape = ColliderShape::Box;
        c.localPosition = Vec3{static_cast<float>(i) * 3.0f, 0.5f, 0.0f};
        c.localRotation = Quat{1.0f, 0.0f, 0.0f, 0.0f};
        c.halfExtents = Vec3{0.5f};
        children.push_back(c);
    }
    uint32_t shapeID = physics.createCompoundShape(children.data(), children.size());
    REQUIRE(shapeID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    spawnStaticShapeBody(reg, ColliderShape::Compound, shapeID, Vec3{0.0f});

    // Sphere A drops over the +X child → lands on it.
    EntityID landed = spawnDynamicSphere(reg, Vec3{3.0f, 5.0f, 0.0f});
    // Sphere B drops over the gap → falls past y=0.
    EntityID fellThrough = spawnDynamicSphere(reg, Vec3{0.0f, 5.0f, 0.0f});

    simulate(physics, sys, reg, 2.0f);

    Vec3 pA;
    Quat rA;
    physics.getBodyTransform(reg.get<RigidBodyComponent>(landed)->bodyID, pA, rA);
    CHECK(pA.y > 1.10f);
    CHECK(pA.y < 1.40f);

    Vec3 pB;
    Quat rB;
    physics.getBodyTransform(reg.get<RigidBodyComponent>(fellThrough)->bodyID, pB, rB);
    CHECK(pB.y < -1.0f);  // unimpeded free fall past the compound

    physics.shutdown();
}

// ---------------------------------------------------------------------------
// 3. Mesh shape collides — sphere lands on a triangulated quad.
// ---------------------------------------------------------------------------
TEST_CASE("Mesh shape: falling sphere lands on a triangulated quad", "[physics][shapes][behavior]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    TriQuad quad = makeQuad(/*extent=*/3.0f, /*y=*/0.0f);
    const size_t vertexCount = quad.positions.size() / 3;
    uint32_t shapeID = physics.createMeshShape(quad.positions.data(), vertexCount,
                                               quad.indices.data(), quad.indices.size());
    REQUIRE(shapeID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    spawnStaticShapeBody(reg, ColliderShape::Mesh, shapeID, Vec3{0.0f});
    EntityID sphere = spawnDynamicSphere(reg, Vec3{0.0f, 5.0f, 0.0f}, /*radius=*/0.25f);

    simulate(physics, sys, reg, 2.0f);

    Vec3 pos;
    Quat rot;
    physics.getBodyTransform(reg.get<RigidBodyComponent>(sphere)->bodyID, pos, rot);
    // Sphere centre should rest at y ≈ radius (0.25) atop the quad at y=0.
    CHECK(pos.y > 0.10f);
    CHECK(pos.y < 0.40f);

    physics.shutdown();
}

// ---------------------------------------------------------------------------
// 4. Raycast hits compound and mesh shapes.
// ---------------------------------------------------------------------------
TEST_CASE("Raycast hits compound and mesh shape bodies", "[physics][shapes][raycast]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    // Compound: single box centred at world y=2.
    std::vector<IPhysicsEngine::CompoundChild> children;
    {
        IPhysicsEngine::CompoundChild c;
        c.shape = ColliderShape::Box;
        c.localPosition = Vec3{0.0f, 0.0f, 0.0f};
        c.localRotation = Quat{1.0f, 0.0f, 0.0f, 0.0f};
        c.halfExtents = Vec3{0.5f};
        children.push_back(c);
    }
    uint32_t cid = physics.createCompoundShape(children.data(), children.size());

    // Mesh: quad at y=-2 (below the ray origin).
    TriQuad quad = makeQuad(2.0f, -2.0f);
    uint32_t mid = physics.createMeshShape(quad.positions.data(), quad.positions.size() / 3,
                                           quad.indices.data(), quad.indices.size());

    Registry reg;
    PhysicsSystem sys;
    EntityID compoundEntity =
        spawnStaticShapeBody(reg, ColliderShape::Compound, cid, Vec3{0.0f, 2.0f, 0.0f});
    EntityID meshEntity = spawnStaticShapeBody(reg, ColliderShape::Mesh, mid, Vec3{0.0f});
    sys.update(reg, physics, 1.0f / 60.0f);

    // Cast +Y from origin → hits compound at y ≈ 1.5 (box bottom).
    RayHit upHit;
    REQUIRE(physics.rayCastClosest(Vec3{0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 10.0f, upHit));
    CHECK(upHit.entity == compoundEntity);

    // Cast −Y from origin → hits mesh at y ≈ -2.
    RayHit downHit;
    REQUIRE(physics.rayCastClosest(Vec3{0.0f}, Vec3{0.0f, -1.0f, 0.0f}, 10.0f, downHit));
    CHECK(downHit.entity == meshEntity);

    physics.shutdown();
}

// ---------------------------------------------------------------------------
// 5. Compound child rotation deflects falling body — 45° wedge slides sphere
//    off to the side.
// ---------------------------------------------------------------------------
TEST_CASE("Compound shape: rotated child deflects sphere along ramp", "[physics][shapes][behavior]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    // One wide flat box rotated +30° around Z. Right-hand rule: +X edge tips
    // UP, -X edge tips DOWN. A sphere dropped on the down-slope (-X) end
    // should slide further toward -X. We check sign-agnostically (moved >=1
    // unit from drop point) so a future swap of rotation convention doesn't
    // silently invalidate the test.
    const float rampAngleRad = glm::radians(30.0f);
    Quat ramp = glm::angleAxis(rampAngleRad, Vec3{0.0f, 0.0f, 1.0f});

    IPhysicsEngine::CompoundChild c;
    c.shape = ColliderShape::Box;
    c.localPosition = Vec3{0.0f, 0.5f, 0.0f};
    c.localRotation = ramp;
    c.halfExtents = Vec3{2.0f, 0.1f, 1.0f};

    uint32_t shapeID = physics.createCompoundShape(&c, 1);
    REQUIRE(shapeID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    spawnStaticShapeBody(reg, ColliderShape::Compound, shapeID, Vec3{0.0f});
    const Vec3 dropPos{-1.0f, 4.0f, 0.0f};
    EntityID sphere = spawnDynamicSphere(reg, dropPos, /*radius=*/0.25f);

    simulate(physics, sys, reg, 3.0f);

    Vec3 pos;
    Quat rot;
    physics.getBodyTransform(reg.get<RigidBodyComponent>(sphere)->bodyID, pos, rot);
    // The ramp must have deflected the sphere — it shouldn't have come to rest
    // straight below the drop point. Don't assert direction here so the test
    // stays valid if the engine flips its rotation convention.
    CHECK(std::abs(pos.x - dropPos.x) > 1.0f);

    physics.shutdown();
}
