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
