#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/SceneGraph.h"
#include "engine/scene/TransformSystem.h"

using namespace engine::ecs;
using namespace engine::scene;
using namespace engine::rendering;
using namespace engine::math;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool approxEqual(const Mat4& a, const Mat4& b, float eps = 1e-4f)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > eps)
                return false;
    return true;
}

static Mat4 composeTRS(Vec3 pos, Quat rot, Vec3 scl)
{
    return glm::translate(Mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(Mat4(1.0f), scl);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("root entity: world position matches local position", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID e = reg.createEntity();
    Vec3 pos{1.0f, 2.0f, 3.0f};
    Quat rot = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 scl{1.0f, 1.0f, 1.0f};
    reg.emplace<TransformComponent>(e, TransformComponent{pos, rot, scl, 0, {}});

    sys.update(reg);

    auto* wtc = reg.get<WorldTransformComponent>(e);
    REQUIRE(wtc != nullptr);

    // World position = translation column of the world matrix.
    Vec4 worldPos = wtc->matrix[3];
    CHECK(std::abs(worldPos.x - 1.0f) < 1e-4f);
    CHECK(std::abs(worldPos.y - 2.0f) < 1e-4f);
    CHECK(std::abs(worldPos.z - 3.0f) < 1e-4f);
    CHECK(std::abs(worldPos.w - 1.0f) < 1e-4f);
}

TEST_CASE("child entity: world position = parent + child offset", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();

    Vec3 posA{5.0f, 0.0f, 0.0f};
    Vec3 posB{0.0f, 3.0f, 0.0f};
    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 one{1.0f, 1.0f, 1.0f};

    reg.emplace<TransformComponent>(A, TransformComponent{posA, identity, one, 0, {}});
    reg.emplace<TransformComponent>(B, TransformComponent{posB, identity, one, 0, {}});

    REQUIRE(setParent(reg, B, A));

    sys.update(reg);

    auto* wtcB = reg.get<WorldTransformComponent>(B);
    REQUIRE(wtcB != nullptr);

    // B at local (0,3,0) + parent A at (5,0,0) => world (5,3,0).
    Vec4 worldPos = wtcB->matrix[3];
    CHECK(std::abs(worldPos.x - 5.0f) < 1e-4f);
    CHECK(std::abs(worldPos.y - 3.0f) < 1e-4f);
    CHECK(std::abs(worldPos.z - 0.0f) < 1e-4f);
}

TEST_CASE("three-level hierarchy: positions accumulate", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();

    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 one{1.0f, 1.0f, 1.0f};

    Vec3 posA{1.0f, 0.0f, 0.0f};
    Vec3 posB{0.0f, 2.0f, 0.0f};
    Vec3 posC{0.0f, 0.0f, 3.0f};

    reg.emplace<TransformComponent>(A, TransformComponent{posA, identity, one, 0, {}});
    reg.emplace<TransformComponent>(B, TransformComponent{posB, identity, one, 0, {}});
    reg.emplace<TransformComponent>(C, TransformComponent{posC, identity, one, 0, {}});

    REQUIRE(setParent(reg, B, A));
    REQUIRE(setParent(reg, C, B));

    sys.update(reg);

    // C at (0,0,3) under B at (0,2,0) under A at (1,0,0) => world (1,2,3).
    auto* wtcC = reg.get<WorldTransformComponent>(C);
    REQUIRE(wtcC != nullptr);
    Vec4 worldPos = wtcC->matrix[3];
    CHECK(std::abs(worldPos.x - 1.0f) < 1e-4f);
    CHECK(std::abs(worldPos.y - 2.0f) < 1e-4f);
    CHECK(std::abs(worldPos.z - 3.0f) < 1e-4f);
}

TEST_CASE("scale propagates through hierarchy", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID parent = reg.createEntity();
    EntityID child = reg.createEntity();

    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 parentScale{2.0f, 2.0f, 2.0f};
    Vec3 childPos{1.0f, 0.0f, 0.0f};
    Vec3 one{1.0f, 1.0f, 1.0f};

    reg.emplace<TransformComponent>(parent,
                                    TransformComponent{Vec3{0.0f}, identity, parentScale, 0, {}});
    reg.emplace<TransformComponent>(child, TransformComponent{childPos, identity, one, 0, {}});

    REQUIRE(setParent(reg, child, parent));

    sys.update(reg);

    auto* wtc = reg.get<WorldTransformComponent>(child);
    REQUIRE(wtc != nullptr);

    // Child local pos (1,0,0) scaled by parent (2,2,2) => world pos (2,0,0).
    Vec4 worldPos = wtc->matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(std::abs(worldPos.x - 2.0f) < 1e-4f);
    CHECK(std::abs(worldPos.y - 0.0f) < 1e-4f);
    CHECK(std::abs(worldPos.z - 0.0f) < 1e-4f);
}

TEST_CASE("rotation propagates through hierarchy", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID parent = reg.createEntity();
    EntityID child = reg.createEntity();

    Vec3 one{1.0f, 1.0f, 1.0f};
    // 90 degrees around Y axis.
    Quat rot90Y = glm::angleAxis(glm::radians(90.0f), Vec3{0.0f, 1.0f, 0.0f});
    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);

    Vec3 parentPos{0.0f, 0.0f, 0.0f};
    Vec3 childPos{1.0f, 0.0f, 0.0f};

    reg.emplace<TransformComponent>(parent, TransformComponent{parentPos, rot90Y, one, 0, {}});
    reg.emplace<TransformComponent>(child, TransformComponent{childPos, identity, one, 0, {}});

    REQUIRE(setParent(reg, child, parent));

    sys.update(reg);

    auto* wtc = reg.get<WorldTransformComponent>(child);
    REQUIRE(wtc != nullptr);

    // Child at local (1,0,0), parent rotated 90deg around Y.
    // In world space, child should be at approximately (0,0,-1).
    Vec4 worldPos = wtc->matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(std::abs(worldPos.x - 0.0f) < 1e-4f);
    CHECK(std::abs(worldPos.y - 0.0f) < 1e-4f);
    CHECK(std::abs(worldPos.z - (-1.0f)) < 1e-4f);
}

TEST_CASE("standalone entity (no hierarchy)", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID e = reg.createEntity();
    Vec3 pos{7.0f, -3.0f, 1.5f};
    Quat rot = glm::angleAxis(glm::radians(45.0f), glm::normalize(Vec3{1.0f, 1.0f, 0.0f}));
    Vec3 scl{0.5f, 2.0f, 1.0f};

    reg.emplace<TransformComponent>(e, TransformComponent{pos, rot, scl, 0, {}});

    sys.update(reg);

    auto* wtc = reg.get<WorldTransformComponent>(e);
    REQUIRE(wtc != nullptr);
    CHECK(approxEqual(wtc->matrix, composeTRS(pos, rot, scl)));
}

TEST_CASE("entity with WorldTransformComponent but no TransformComponent unchanged", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID e = reg.createEntity();
    Mat4 identity(1.0f);
    reg.emplace<WorldTransformComponent>(e, WorldTransformComponent{identity});

    sys.update(reg);

    auto* wtc = reg.get<WorldTransformComponent>(e);
    REQUIRE(wtc != nullptr);
    CHECK(approxEqual(wtc->matrix, identity));
}

// ---------------------------------------------------------------------------
// Dirty flag optimization tests
// ---------------------------------------------------------------------------

TEST_CASE("clean entity is not recomputed", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID e = reg.createEntity();
    Vec3 pos{1.0f, 2.0f, 3.0f};
    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 one{1.0f, 1.0f, 1.0f};
    reg.emplace<TransformComponent>(e, TransformComponent{pos, identity, one, 0x01, {}});

    // First update: computes world matrix (dirty flag set).
    sys.update(reg);
    auto* wtc = reg.get<WorldTransformComponent>(e);
    REQUIRE(wtc != nullptr);
    Mat4 firstMatrix = wtc->matrix;

    // Modify position but do NOT set dirty flag.
    auto* tc = reg.get<TransformComponent>(e);
    tc->position = Vec3{99.0f, 99.0f, 99.0f};
    // Flag is already cleared from first update, don't set it.

    sys.update(reg);

    wtc = reg.get<WorldTransformComponent>(e);
    REQUIRE(wtc != nullptr);
    // World matrix should be unchanged because entity was not dirty.
    CHECK(approxEqual(wtc->matrix, firstMatrix));
}

TEST_CASE("dirty flag triggers recomputation", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID e = reg.createEntity();
    Vec3 pos{1.0f, 2.0f, 3.0f};
    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 one{1.0f, 1.0f, 1.0f};
    reg.emplace<TransformComponent>(e, TransformComponent{pos, identity, one, 0x01, {}});

    sys.update(reg);

    // Modify position AND set dirty flag.
    auto* tc = reg.get<TransformComponent>(e);
    tc->position = Vec3{10.0f, 20.0f, 30.0f};
    tc->flags |= 0x01;

    sys.update(reg);

    auto* wtc = reg.get<WorldTransformComponent>(e);
    REQUIRE(wtc != nullptr);
    Mat4 expected = composeTRS(Vec3{10.0f, 20.0f, 30.0f}, identity, one);
    CHECK(approxEqual(wtc->matrix, expected));
}

TEST_CASE("dirty parent propagates to children", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID parent = reg.createEntity();
    EntityID child = reg.createEntity();

    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 one{1.0f, 1.0f, 1.0f};

    reg.emplace<TransformComponent>(
        parent, TransformComponent{Vec3{0.0f, 0.0f, 0.0f}, identity, one, 0x01, {}});
    reg.emplace<TransformComponent>(
        child, TransformComponent{Vec3{1.0f, 0.0f, 0.0f}, identity, one, 0x01, {}});

    REQUIRE(setParent(reg, child, parent));
    sys.update(reg);

    // Now move parent and mark dirty; child is NOT explicitly marked dirty.
    auto* ptc = reg.get<TransformComponent>(parent);
    ptc->position = Vec3{5.0f, 0.0f, 0.0f};
    ptc->flags |= 0x01;

    sys.update(reg);

    auto* wtcChild = reg.get<WorldTransformComponent>(child);
    REQUIRE(wtcChild != nullptr);
    // Child at local (1,0,0) under parent at (5,0,0) => world (6,0,0).
    Vec4 worldPos = wtcChild->matrix[3];
    CHECK(std::abs(worldPos.x - 6.0f) < 1e-4f);
    CHECK(std::abs(worldPos.y - 0.0f) < 1e-4f);
    CHECK(std::abs(worldPos.z - 0.0f) < 1e-4f);
}

TEST_CASE("clean parent with dirty child", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID parent = reg.createEntity();
    EntityID child = reg.createEntity();

    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 one{1.0f, 1.0f, 1.0f};

    reg.emplace<TransformComponent>(
        parent, TransformComponent{Vec3{5.0f, 0.0f, 0.0f}, identity, one, 0x01, {}});
    reg.emplace<TransformComponent>(
        child, TransformComponent{Vec3{1.0f, 0.0f, 0.0f}, identity, one, 0x01, {}});

    REQUIRE(setParent(reg, child, parent));
    sys.update(reg);

    // Save parent's world matrix.
    auto* wtcParent = reg.get<WorldTransformComponent>(parent);
    REQUIRE(wtcParent != nullptr);
    Mat4 parentMatBefore = wtcParent->matrix;

    // Mark only the child dirty and change its position.
    auto* ctc = reg.get<TransformComponent>(child);
    ctc->position = Vec3{2.0f, 0.0f, 0.0f};
    ctc->flags |= 0x01;

    sys.update(reg);

    // Parent matrix should be unchanged.
    wtcParent = reg.get<WorldTransformComponent>(parent);
    REQUIRE(wtcParent != nullptr);
    CHECK(approxEqual(wtcParent->matrix, parentMatBefore));

    // Child should be recomputed: parent(5,0,0) + child(2,0,0) => (7,0,0).
    auto* wtcChild = reg.get<WorldTransformComponent>(child);
    REQUIRE(wtcChild != nullptr);
    Vec4 childWorldPos = wtcChild->matrix[3];
    CHECK(std::abs(childWorldPos.x - 7.0f) < 1e-4f);
    CHECK(std::abs(childWorldPos.y - 0.0f) < 1e-4f);
    CHECK(std::abs(childWorldPos.z - 0.0f) < 1e-4f);
}

TEST_CASE("re-parenting marks dirty and produces correct world matrix", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID child = reg.createEntity();

    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 one{1.0f, 1.0f, 1.0f};

    reg.emplace<TransformComponent>(
        A, TransformComponent{Vec3{1.0f, 0.0f, 0.0f}, identity, one, 0x01, {}});
    reg.emplace<TransformComponent>(
        B, TransformComponent{Vec3{0.0f, 10.0f, 0.0f}, identity, one, 0x01, {}});
    reg.emplace<TransformComponent>(
        child, TransformComponent{Vec3{0.0f, 0.0f, 1.0f}, identity, one, 0x01, {}});

    REQUIRE(setParent(reg, child, A));
    sys.update(reg);

    // child under A: world = (1,0,1).
    auto* wtc = reg.get<WorldTransformComponent>(child);
    REQUIRE(wtc != nullptr);
    CHECK(std::abs(wtc->matrix[3].x - 1.0f) < 1e-4f);
    CHECK(std::abs(wtc->matrix[3].z - 1.0f) < 1e-4f);

    // Re-parent child under B. setParent should mark child dirty.
    REQUIRE(setParent(reg, child, B));
    sys.update(reg);

    // child under B: world = (0, 10, 1).
    wtc = reg.get<WorldTransformComponent>(child);
    REQUIRE(wtc != nullptr);
    CHECK(std::abs(wtc->matrix[3].x - 0.0f) < 1e-4f);
    CHECK(std::abs(wtc->matrix[3].y - 10.0f) < 1e-4f);
    CHECK(std::abs(wtc->matrix[3].z - 1.0f) < 1e-4f);
}

TEST_CASE("after re-parenting, update produces correct world matrix", "[transform]")
{
    Registry reg;
    TransformSystem sys;

    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();

    Quat identity = Quat(1.0f, 0.0f, 0.0f, 0.0f);
    Vec3 one{1.0f, 1.0f, 1.0f};

    Vec3 posA{1.0f, 0.0f, 0.0f};
    Vec3 posB{0.0f, 2.0f, 0.0f};
    Vec3 posC{0.0f, 0.0f, 5.0f};

    reg.emplace<TransformComponent>(A, TransformComponent{posA, identity, one, 0, {}});
    reg.emplace<TransformComponent>(B, TransformComponent{posB, identity, one, 0, {}});
    reg.emplace<TransformComponent>(C, TransformComponent{posC, identity, one, 0, {}});

    // Initially B is child of A.
    REQUIRE(setParent(reg, B, A));
    sys.update(reg);

    auto* wtcB = reg.get<WorldTransformComponent>(B);
    REQUIRE(wtcB != nullptr);
    Mat4 expectedFirst = composeTRS(posA, identity, one) * composeTRS(posB, identity, one);
    CHECK(approxEqual(wtcB->matrix, expectedFirst));

    // Re-parent B under C.
    REQUIRE(setParent(reg, B, C));
    sys.update(reg);

    wtcB = reg.get<WorldTransformComponent>(B);
    REQUIRE(wtcB != nullptr);
    Mat4 expectedSecond = composeTRS(posC, identity, one) * composeTRS(posB, identity, one);
    CHECK(approxEqual(wtcB->matrix, expectedSecond));
}
