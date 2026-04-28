#include <catch2/catch_test_macros.hpp>
#include <cstddef>

#include "engine/physics/PhysicsComponents.h"

using namespace engine::physics;

// ---------------------------------------------------------------------------
// Size and layout verification
// ---------------------------------------------------------------------------

TEST_CASE("RigidBodyComponent is 28 bytes", "[physics][components]")
{
    CHECK(sizeof(RigidBodyComponent) == 28);
}

TEST_CASE("ColliderComponent is 36 bytes", "[physics][components]")
{
    CHECK(sizeof(ColliderComponent) == 36);
}

TEST_CASE("RigidBodyComponent field offsets", "[physics][components]")
{
    CHECK(offsetof(RigidBodyComponent, bodyID) == 0);
    CHECK(offsetof(RigidBodyComponent, mass) == 4);
    CHECK(offsetof(RigidBodyComponent, linearDamping) == 8);
    CHECK(offsetof(RigidBodyComponent, angularDamping) == 12);
    CHECK(offsetof(RigidBodyComponent, friction) == 16);
    CHECK(offsetof(RigidBodyComponent, restitution) == 20);
    CHECK(offsetof(RigidBodyComponent, type) == 24);
    CHECK(offsetof(RigidBodyComponent, layer) == 25);
}

TEST_CASE("ColliderComponent field offsets", "[physics][components]")
{
    CHECK(offsetof(ColliderComponent, offset) == 0);
    CHECK(offsetof(ColliderComponent, halfExtents) == 12);
    CHECK(offsetof(ColliderComponent, radius) == 24);
    CHECK(offsetof(ColliderComponent, shape) == 28);
    CHECK(offsetof(ColliderComponent, isSensor) == 29);
    CHECK(offsetof(ColliderComponent, shapeID) == 32);
}

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------

TEST_CASE("RigidBodyComponent default values", "[physics][components]")
{
    RigidBodyComponent rb;

    CHECK(rb.bodyID == ~0u);
    CHECK(rb.mass == 1.0f);
    CHECK(rb.linearDamping == 0.05f);
    CHECK(rb.angularDamping == 0.05f);
    CHECK(rb.friction == 0.5f);
    CHECK(rb.restitution == 0.3f);
    CHECK(rb.type == BodyType::Dynamic);
    CHECK(rb.layer == 0);
}

TEST_CASE("ColliderComponent default values", "[physics][components]")
{
    ColliderComponent col;

    CHECK(col.offset.x == 0.0f);
    CHECK(col.offset.y == 0.0f);
    CHECK(col.offset.z == 0.0f);
    CHECK(col.halfExtents.x == 0.5f);
    CHECK(col.halfExtents.y == 0.5f);
    CHECK(col.halfExtents.z == 0.5f);
    CHECK(col.radius == 0.5f);
    CHECK(col.shape == ColliderShape::Box);
    CHECK(col.isSensor == 0);
    CHECK(col.shapeID == ~0u);
}

TEST_CASE("PhysicsBodyCreatedTag is a tag component", "[physics][components]")
{
    CHECK(sizeof(PhysicsBodyCreatedTag) == 1);
}
