#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/Registry.h"
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/SceneGraph.h"

using namespace engine::ecs;
using namespace engine::scene;

// ---------------------------------------------------------------------------
// Hierarchy mutation
// ---------------------------------------------------------------------------

TEST_CASE("setParent establishes parent-child relationship", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();

    REQUIRE(setParent(reg, B, A));

    CHECK(getParent(reg, B) == A);

    const auto* children = getChildren(reg, A);
    REQUIRE(children != nullptr);
    REQUIRE(children->size() == 1);
    CHECK((*children)[0] == B);

    CHECK(reg.has<HierarchyComponent>(B));
    CHECK(reg.has<ChildrenComponent>(A));
}

TEST_CASE("setParent to INVALID_ENTITY detaches", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();

    setParent(reg, B, A);
    REQUIRE(getParent(reg, B) == A);

    setParent(reg, B, INVALID_ENTITY);

    CHECK_FALSE(reg.has<HierarchyComponent>(B));
    CHECK_FALSE(reg.has<ChildrenComponent>(A));
    CHECK(getParent(reg, B) == INVALID_ENTITY);
}

TEST_CASE("setParent re-parents between parents", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();

    setParent(reg, B, A);
    REQUIRE(getParent(reg, B) == A);

    setParent(reg, B, C);

    // A should have no children (ChildrenComponent removed when empty).
    CHECK_FALSE(reg.has<ChildrenComponent>(A));

    // C should now have B as child.
    const auto* children = getChildren(reg, C);
    REQUIRE(children != nullptr);
    REQUIRE(children->size() == 1);
    CHECK((*children)[0] == B);

    // B's parent should be C.
    CHECK(getParent(reg, B) == C);
}

TEST_CASE("setParent rejects cycle", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();

    setParent(reg, B, A);
    setParent(reg, C, B);
    // A -> B -> C. Trying to set A's parent to C would create a cycle.

    CHECK_FALSE(setParent(reg, A, C));

    // A should still be a root (no HierarchyComponent).
    CHECK(getParent(reg, A) == INVALID_ENTITY);
    CHECK_FALSE(reg.has<HierarchyComponent>(A));

    // Original hierarchy should be intact.
    CHECK(getParent(reg, B) == A);
    CHECK(getParent(reg, C) == B);
}

TEST_CASE("setParent rejects self-parent", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();

    CHECK_FALSE(setParent(reg, A, A));

    CHECK(getParent(reg, A) == INVALID_ENTITY);
    CHECK_FALSE(reg.has<HierarchyComponent>(A));
}

TEST_CASE("setParent with multiple children", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();
    EntityID D = reg.createEntity();

    setParent(reg, B, A);
    setParent(reg, C, A);
    setParent(reg, D, A);

    const auto* children = getChildren(reg, A);
    REQUIRE(children != nullptr);
    REQUIRE(children->size() == 3);

    CHECK(std::find(children->begin(), children->end(), B) != children->end());
    CHECK(std::find(children->begin(), children->end(), C) != children->end());
    CHECK(std::find(children->begin(), children->end(), D) != children->end());
}

// ---------------------------------------------------------------------------
// Detach
// ---------------------------------------------------------------------------

TEST_CASE("detach removes from parent", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();

    setParent(reg, B, A);
    REQUIRE(getParent(reg, B) == A);

    detach(reg, B);

    // B is now a root.
    CHECK(getParent(reg, B) == INVALID_ENTITY);
    CHECK_FALSE(reg.has<HierarchyComponent>(B));

    // A has no children left, so ChildrenComponent should be removed.
    CHECK_FALSE(reg.has<ChildrenComponent>(A));
}

TEST_CASE("detach on root is no-op", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();

    // A has no parent. detach should not crash.
    detach(reg, A);

    CHECK(reg.isValid(A));
    CHECK(getParent(reg, A) == INVALID_ENTITY);
}

// ---------------------------------------------------------------------------
// Destroy hierarchy
// ---------------------------------------------------------------------------

TEST_CASE("destroyHierarchy destroys subtree", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();

    setParent(reg, B, A);
    setParent(reg, C, B);

    destroyHierarchy(reg, A);

    CHECK_FALSE(reg.isValid(A));
    CHECK_FALSE(reg.isValid(B));
    CHECK_FALSE(reg.isValid(C));
}

TEST_CASE("destroyHierarchy on leaf", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();

    setParent(reg, B, A);

    destroyHierarchy(reg, B);

    CHECK_FALSE(reg.isValid(B));
    CHECK(reg.isValid(A));
    CHECK_FALSE(reg.has<ChildrenComponent>(A));
}

TEST_CASE("destroyHierarchy with wide tree", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();
    EntityID D = reg.createEntity();

    setParent(reg, B, A);
    setParent(reg, C, A);
    setParent(reg, D, A);

    destroyHierarchy(reg, A);

    CHECK_FALSE(reg.isValid(A));
    CHECK_FALSE(reg.isValid(B));
    CHECK_FALSE(reg.isValid(C));
    CHECK_FALSE(reg.isValid(D));
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

TEST_CASE("getParent returns INVALID_ENTITY for root", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();

    CHECK(getParent(reg, A) == INVALID_ENTITY);
}

TEST_CASE("getChildren returns nullptr for leaf", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();

    CHECK(getChildren(reg, A) == nullptr);
}

TEST_CASE("isAncestor walks chain", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();

    setParent(reg, B, A);
    setParent(reg, C, B);

    CHECK(isAncestor(reg, A, C));
    CHECK(isAncestor(reg, A, B));
    CHECK(isAncestor(reg, B, C));
}

TEST_CASE("isAncestor false for unrelated", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();

    CHECK_FALSE(isAncestor(reg, A, B));
    CHECK_FALSE(isAncestor(reg, B, A));
}

TEST_CASE("isAncestor false for self", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();

    CHECK_FALSE(isAncestor(reg, A, A));
}

// ---------------------------------------------------------------------------
// Edge cases (from review)
// ---------------------------------------------------------------------------

TEST_CASE("setParent on destroyed entity returns false", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    reg.destroyEntity(B);

    CHECK_FALSE(setParent(reg, B, A));
    CHECK_FALSE(setParent(reg, A, B));
}

TEST_CASE("destroyHierarchy on middle subtree", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();

    REQUIRE(setParent(reg, B, A));
    REQUIRE(setParent(reg, C, B));

    // Destroy B and its subtree (C), leaving A alive.
    destroyHierarchy(reg, B);

    CHECK(reg.isValid(A));
    CHECK_FALSE(reg.isValid(B));
    CHECK_FALSE(reg.isValid(C));
    CHECK_FALSE(reg.has<ChildrenComponent>(A));
}

TEST_CASE("sequential re-parenting cleans up correctly", "[scenegraph]")
{
    Registry reg;
    EntityID A = reg.createEntity();
    EntityID B = reg.createEntity();
    EntityID C = reg.createEntity();
    EntityID D = reg.createEntity();

    REQUIRE(setParent(reg, A, B));
    REQUIRE(setParent(reg, A, C));
    REQUIRE(setParent(reg, A, D));

    CHECK(getParent(reg, A) == D);
    CHECK_FALSE(reg.has<ChildrenComponent>(B));
    CHECK_FALSE(reg.has<ChildrenComponent>(C));

    const auto* children = getChildren(reg, D);
    REQUIRE(children != nullptr);
    CHECK(children->size() == 1);
    CHECK((*children)[0] == A);
}
