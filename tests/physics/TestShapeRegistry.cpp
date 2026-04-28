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

// ---------------------------------------------------------------------------
// Helper: build a small unit-cube triangle list (12 tris, 8 verts) for mesh
// shape tests. Centered at the origin, +/-0.5 on each axis.
// ---------------------------------------------------------------------------
struct CubeMeshData
{
    std::vector<float> positions;  // flat xyz
    std::vector<uint32_t> indices;
};

static CubeMeshData makeUnitCubeMesh()
{
    CubeMeshData m;
    m.positions = {
        -0.5f, -0.5f, -0.5f,  // 0
        0.5f,  -0.5f, -0.5f,  // 1
        0.5f,  0.5f,  -0.5f,  // 2
        -0.5f, 0.5f,  -0.5f,  // 3
        -0.5f, -0.5f, 0.5f,   // 4
        0.5f,  -0.5f, 0.5f,   // 5
        0.5f,  0.5f,  0.5f,   // 6
        -0.5f, 0.5f,  0.5f,   // 7
    };
    m.indices = {// -Z face
                 0, 2, 1, 0, 3, 2,
                 // +Z face
                 4, 5, 6, 4, 6, 7,
                 // -X face
                 0, 4, 7, 0, 7, 3,
                 // +X face
                 1, 2, 6, 1, 6, 5,
                 // -Y face
                 0, 1, 5, 0, 5, 4,
                 // +Y face
                 3, 7, 6, 3, 6, 2};
    return m;
}

// ---------------------------------------------------------------------------
// Helper: create a static body referencing a pre-built shape via shapeID.
// ---------------------------------------------------------------------------
static EntityID createShapeBody(Registry& reg, ColliderShape shape, uint32_t shapeID,
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
// 1. Round-trip: createCompoundShape with several boxes + addBody + queryable
// ---------------------------------------------------------------------------

TEST_CASE("Compound shape: build, attach, body is queryable", "[physics][shapes]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    std::vector<IPhysicsEngine::CompoundChild> children;
    for (int i = 0; i < 4; ++i)
    {
        IPhysicsEngine::CompoundChild c;
        c.shape = ColliderShape::Box;
        c.localPosition = Vec3{static_cast<float>(i) * 1.5f, 0.0f, 0.0f};
        c.localRotation = Quat{1.0f, 0.0f, 0.0f, 0.0f};
        c.halfExtents = Vec3{0.5f};
        children.push_back(c);
    }

    uint32_t shapeID = physics.createCompoundShape(children.data(), children.size());
    REQUIRE(shapeID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    EntityID e = createShapeBody(reg, ColliderShape::Compound, shapeID, Vec3{0.0f, 5.0f, 0.0f});
    sys.update(reg, physics, 1.0f / 60.0f);

    auto* rb = reg.get<RigidBodyComponent>(e);
    REQUIRE(rb != nullptr);
    CHECK(rb->bodyID != ~0u);
    CHECK(reg.has<PhysicsBodyCreatedTag>(e));
    CHECK(physics.getBodyEntityMap().size() == 1);

    // World query: body transform reads back at the spawn position.
    Vec3 pos;
    Quat rot;
    physics.getBodyTransform(rb->bodyID, pos, rot);
    CHECK(pos.y > 4.9f);  // Static body — does not fall.
    CHECK(pos.y < 5.1f);

    physics.shutdown();
}

// ---------------------------------------------------------------------------
// 2. Round-trip: createMeshShape with a unit cube + addBody + queryable
// ---------------------------------------------------------------------------

TEST_CASE("Mesh shape: build from triangle list, attach, body is queryable", "[physics][shapes]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    CubeMeshData mesh = makeUnitCubeMesh();
    const size_t vertexCount = mesh.positions.size() / 3;

    uint32_t shapeID = physics.createMeshShape(mesh.positions.data(), vertexCount,
                                               mesh.indices.data(), mesh.indices.size());
    REQUIRE(shapeID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    EntityID e = createShapeBody(reg, ColliderShape::Mesh, shapeID, Vec3{0.0f, 0.0f, 0.0f});
    sys.update(reg, physics, 1.0f / 60.0f);

    auto* rb = reg.get<RigidBodyComponent>(e);
    REQUIRE(rb != nullptr);
    CHECK(rb->bodyID != ~0u);
    CHECK(reg.has<PhysicsBodyCreatedTag>(e));
    CHECK(physics.getBodyEntityMap().size() == 1);

    Vec3 pos;
    Quat rot;
    physics.getBodyTransform(rb->bodyID, pos, rot);
    CHECK(std::abs(pos.x) < 1e-3f);
    CHECK(std::abs(pos.y) < 1e-3f);
    CHECK(std::abs(pos.z) < 1e-3f);

    physics.shutdown();
}

// ---------------------------------------------------------------------------
// 3. Lifetime: create -> addBody -> destroyShape -> body still works ->
//              removeBody -> no leak.
// ---------------------------------------------------------------------------

TEST_CASE("Mesh shape outlives destroyMeshShape while body still references it",
          "[physics][shapes][lifetime]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    CubeMeshData mesh = makeUnitCubeMesh();
    const size_t vertexCount = mesh.positions.size() / 3;
    uint32_t shapeID = physics.createMeshShape(mesh.positions.data(), vertexCount,
                                               mesh.indices.data(), mesh.indices.size());
    REQUIRE(shapeID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    EntityID e = createShapeBody(reg, ColliderShape::Mesh, shapeID);
    sys.update(reg, physics, 1.0f / 60.0f);
    auto* rb = reg.get<RigidBodyComponent>(e);
    REQUIRE(rb != nullptr);
    const uint32_t bodyID = rb->bodyID;
    REQUIRE(bodyID != ~0u);

    // Drop the engine's hold on the shape ID. The body must keep working
    // because Jolt's intrinsic JPH::ShapeRefC still references the shape.
    physics.destroyMeshShape(shapeID);

    // Body still reachable; step + transform query must still succeed.
    physics.step(1.0f / 60.0f);
    Vec3 pos;
    Quat rot;
    physics.getBodyTransform(bodyID, pos, rot);
    CHECK(std::abs(pos.x) < 1e-2f);

    // Now remove the body. With the registry already cleared, this is the
    // last reference: shape is fully released here. No assertion / leak.
    physics.removeBody(bodyID);
    CHECK(physics.getBodyEntityMap().count(bodyID) == 0);

    physics.shutdown();
}

TEST_CASE("Compound shape outlives destroyCompoundShape while body still references it",
          "[physics][shapes][lifetime]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    std::vector<IPhysicsEngine::CompoundChild> children;
    {
        IPhysicsEngine::CompoundChild c;
        c.shape = ColliderShape::Box;
        c.halfExtents = Vec3{0.5f};
        children.push_back(c);
        c.localPosition = Vec3{1.5f, 0.0f, 0.0f};
        children.push_back(c);
        c.localPosition = Vec3{-1.5f, 0.0f, 0.0f};
        children.push_back(c);
    }
    uint32_t shapeID = physics.createCompoundShape(children.data(), children.size());
    REQUIRE(shapeID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    EntityID e = createShapeBody(reg, ColliderShape::Compound, shapeID);
    sys.update(reg, physics, 1.0f / 60.0f);
    auto* rb = reg.get<RigidBodyComponent>(e);
    REQUIRE(rb != nullptr);
    const uint32_t bodyID = rb->bodyID;
    REQUIRE(bodyID != ~0u);

    physics.destroyCompoundShape(shapeID);

    // Body still alive — step the world and query the transform.
    physics.step(1.0f / 60.0f);
    Vec3 pos;
    Quat rot;
    physics.getBodyTransform(bodyID, pos, rot);
    CHECK(std::abs(pos.x) < 1e-2f);

    physics.removeBody(bodyID);
    CHECK(physics.getBodyEntityMap().count(bodyID) == 0);

    physics.shutdown();
}

// ---------------------------------------------------------------------------
// 4. Lifetime (reverse order): create -> addBody -> removeBody -> destroyShape
//    -> no use-after-free.
// ---------------------------------------------------------------------------

TEST_CASE("destroy*Shape after removeBody is safe (reverse order)", "[physics][shapes][lifetime]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    // Mesh path.
    CubeMeshData mesh = makeUnitCubeMesh();
    const size_t vertexCount = mesh.positions.size() / 3;
    uint32_t meshID = physics.createMeshShape(mesh.positions.data(), vertexCount,
                                              mesh.indices.data(), mesh.indices.size());
    REQUIRE(meshID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    EntityID meshEntity = createShapeBody(reg, ColliderShape::Mesh, meshID);
    sys.update(reg, physics, 1.0f / 60.0f);
    auto* meshRb = reg.get<RigidBodyComponent>(meshEntity);
    REQUIRE(meshRb != nullptr);
    REQUIRE(meshRb->bodyID != ~0u);
    physics.removeBody(meshRb->bodyID);
    physics.destroyMeshShape(meshID);  // engine's last hold released cleanly.

    // Compound path.
    IPhysicsEngine::CompoundChild child;
    child.shape = ColliderShape::Sphere;
    child.radius = 0.5f;
    uint32_t compoundID = physics.createCompoundShape(&child, 1);
    REQUIRE(compoundID != ~0u);
    EntityID compoundEntity = createShapeBody(reg, ColliderShape::Compound, compoundID);
    sys.update(reg, physics, 1.0f / 60.0f);
    auto* compoundRb = reg.get<RigidBodyComponent>(compoundEntity);
    REQUIRE(compoundRb != nullptr);
    REQUIRE(compoundRb->bodyID != ~0u);
    physics.removeBody(compoundRb->bodyID);
    physics.destroyCompoundShape(compoundID);

    // Engine still healthy — can keep stepping.
    physics.step(1.0f / 60.0f);

    physics.shutdown();
}

// ---------------------------------------------------------------------------
// 5. Sanity: addBody with ColliderShape::Mesh and shapeID == ~0u must fail
//    (no silent fallback to Box).
// ---------------------------------------------------------------------------

TEST_CASE("addBody with Mesh shape and invalid shapeID fails", "[physics][shapes]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    BodyDesc desc;
    desc.shape = ColliderShape::Mesh;
    desc.shapeID = ~0u;  // explicit "no shape registered"
    desc.type = BodyType::Static;

    uint32_t bodyID = physics.addBody(desc);
    CHECK(bodyID == ~0u);
    CHECK(physics.getBodyEntityMap().empty());

    desc.shape = ColliderShape::Compound;
    bodyID = physics.addBody(desc);
    CHECK(bodyID == ~0u);
    CHECK(physics.getBodyEntityMap().empty());

    physics.shutdown();
}

// ---------------------------------------------------------------------------
// Bonus: shape can back multiple bodies (instancing).
// ---------------------------------------------------------------------------

TEST_CASE("Compound shape backs multiple bodies (instancing)", "[physics][shapes]")
{
    JoltPhysicsEngine physics;
    REQUIRE(physics.init());

    IPhysicsEngine::CompoundChild child;
    child.shape = ColliderShape::Box;
    child.halfExtents = Vec3{0.5f};
    uint32_t shapeID = physics.createCompoundShape(&child, 1);
    REQUIRE(shapeID != ~0u);

    Registry reg;
    PhysicsSystem sys;
    EntityID e1 = createShapeBody(reg, ColliderShape::Compound, shapeID, Vec3{0.0f, 0.0f, 0.0f});
    EntityID e2 = createShapeBody(reg, ColliderShape::Compound, shapeID, Vec3{5.0f, 0.0f, 0.0f});
    EntityID e3 = createShapeBody(reg, ColliderShape::Compound, shapeID, Vec3{0.0f, 0.0f, 5.0f});
    sys.update(reg, physics, 1.0f / 60.0f);

    CHECK(reg.has<PhysicsBodyCreatedTag>(e1));
    CHECK(reg.has<PhysicsBodyCreatedTag>(e2));
    CHECK(reg.has<PhysicsBodyCreatedTag>(e3));
    CHECK(physics.getBodyEntityMap().size() == 3);

    physics.shutdown();
}
