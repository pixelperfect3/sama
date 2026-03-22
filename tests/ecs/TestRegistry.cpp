#include <catch2/catch_test_macros.hpp>
#include <unordered_set>
#include <vector>

#include "engine/ecs/Registry.h"

using namespace engine::ecs;

// ---------------------------------------------------------------------------
// Local component types
// ---------------------------------------------------------------------------

struct Position
{
    float x = 0, y = 0, z = 0;
};

struct Velocity
{
    float x = 0, y = 0, z = 0;
};

struct Health
{
    int value = 100;
};

struct Tag
{
};

// ---------------------------------------------------------------------------
// Entity lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("Registry: createEntity returns a valid entity", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    CHECK(e != INVALID_ENTITY);
    CHECK(reg.isValid(e));
}

TEST_CASE("Registry: createEntity returns unique IDs", "[registry]")
{
    Registry reg;
    std::unordered_set<EntityID> ids;
    constexpr int N = 100;

    for (int i = 0; i < N; ++i)
        ids.insert(reg.createEntity());

    CHECK(ids.size() == static_cast<std::size_t>(N));
}

TEST_CASE("Registry: INVALID_ENTITY is never valid", "[registry]")
{
    Registry reg;
    CHECK_FALSE(reg.isValid(INVALID_ENTITY));
}

TEST_CASE("Registry: destroyEntity makes the entity invalid", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    REQUIRE(reg.isValid(e));

    reg.destroyEntity(e);
    CHECK_FALSE(reg.isValid(e));
}

TEST_CASE("Registry: destroyed entity ID stays invalid even after index reuse", "[registry]")
{
    Registry reg;
    EntityID first = reg.createEntity();
    reg.destroyEntity(first);

    // Creating a new entity recycles the same index
    EntityID second = reg.createEntity();
    CHECK(reg.isValid(second));

    // The old ID must not be valid — generation has changed
    CHECK_FALSE(reg.isValid(first));
    CHECK(entityIndex(first) == entityIndex(second));
    CHECK(entityGeneration(first) != entityGeneration(second));
}

TEST_CASE("Registry: destroyEntity on already-destroyed entity is a no-op", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.destroyEntity(e);
    // Should not crash or assert
    reg.destroyEntity(e);
    CHECK_FALSE(reg.isValid(e));
}

// ---------------------------------------------------------------------------
// Component management
// ---------------------------------------------------------------------------

TEST_CASE("Registry: emplace and get roundtrip", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();

    Position& ref = reg.emplace<Position>(e, 1.f, 2.f, 3.f);
    CHECK(ref.x == 1.f);
    CHECK(ref.y == 2.f);
    CHECK(ref.z == 3.f);

    Position* p = reg.get<Position>(e);
    REQUIRE(p != nullptr);
    CHECK(p->x == 1.f);
    CHECK(p->y == 2.f);
    CHECK(p->z == 3.f);

    // emplace reference and get pointer must alias the same object
    CHECK(p == &ref);
}

TEST_CASE("Registry: get returns nullptr for missing component", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();

    CHECK(reg.get<Position>(e) == nullptr);
    CHECK(reg.get<Health>(e) == nullptr);
}

TEST_CASE("Registry: get returns nullptr for invalid entity", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.emplace<Position>(e, 1.f, 2.f, 3.f);
    reg.destroyEntity(e);

    CHECK(reg.get<Position>(e) == nullptr);
}

TEST_CASE("Registry: has returns correct bool", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();

    CHECK_FALSE(reg.has<Position>(e));
    reg.emplace<Position>(e);
    CHECK(reg.has<Position>(e));
    CHECK_FALSE(reg.has<Health>(e));
}

TEST_CASE("Registry: has returns false for invalid entity", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.emplace<Position>(e);
    reg.destroyEntity(e);

    CHECK_FALSE(reg.has<Position>(e));
}

TEST_CASE("Registry: remove<T> removes the component", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.emplace<Position>(e, 5.f, 5.f, 5.f);
    REQUIRE(reg.has<Position>(e));

    reg.remove<Position>(e);
    CHECK_FALSE(reg.has<Position>(e));
    CHECK(reg.get<Position>(e) == nullptr);
}

TEST_CASE("Registry: remove<T> on entity without component is a no-op", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    // No Position added — should not crash
    reg.remove<Position>(e);
    CHECK_FALSE(reg.has<Position>(e));
}

TEST_CASE("Registry: destroyEntity removes all components", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.emplace<Position>(e, 1.f, 2.f, 3.f);
    reg.emplace<Health>(e, 50);
    reg.emplace<Tag>(e);

    reg.destroyEntity(e);

    // After destroy the entity is invalid; get must return nullptr
    CHECK(reg.get<Position>(e) == nullptr);
    CHECK(reg.get<Health>(e) == nullptr);
}

TEST_CASE("Registry: emplace overwrites existing component", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();

    reg.emplace<Health>(e, 100);
    reg.emplace<Health>(e, 42);

    Health* h = reg.get<Health>(e);
    REQUIRE(h != nullptr);
    CHECK(h->value == 42);
}

TEST_CASE("Registry: multiple components on one entity are independent", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();

    reg.emplace<Position>(e, 1.f, 2.f, 3.f);
    reg.emplace<Velocity>(e, 4.f, 5.f, 6.f);
    reg.emplace<Health>(e, 77);

    CHECK(reg.get<Position>(e)->x == 1.f);
    CHECK(reg.get<Velocity>(e)->x == 4.f);
    CHECK(reg.get<Health>(e)->value == 77);

    // Removing one must not affect others
    reg.remove<Velocity>(e);
    CHECK(reg.get<Velocity>(e) == nullptr);
    CHECK(reg.get<Position>(e)->x == 1.f);
    CHECK(reg.get<Health>(e)->value == 77);
}

// ---------------------------------------------------------------------------
// Stress / correctness tests
// ---------------------------------------------------------------------------

TEST_CASE("Registry: create 1000 entities, destroy half, create more", "[registry][stress]")
{
    Registry reg;
    constexpr int N = 1000;
    std::vector<EntityID> live;
    live.reserve(N);

    // Create 1000 entities with a Position
    for (int i = 0; i < N; ++i)
    {
        EntityID e = reg.createEntity();
        reg.emplace<Position>(e, static_cast<float>(i), 0.f, 0.f);
        live.push_back(e);
    }

    // Destroy the first 500
    for (int i = 0; i < N / 2; ++i)
        reg.destroyEntity(live[static_cast<std::size_t>(i)]);

    // Verify the surviving 500 are still valid with correct data
    for (int i = N / 2; i < N; ++i)
    {
        REQUIRE(reg.isValid(live[static_cast<std::size_t>(i)]));
        Position* p = reg.get<Position>(live[static_cast<std::size_t>(i)]);
        REQUIRE(p != nullptr);
        CHECK(p->x == static_cast<float>(i));
    }

    // Create 300 more entities — some indices will be recycled
    for (int i = 0; i < 300; ++i)
    {
        EntityID e = reg.createEntity();
        CHECK(reg.isValid(e));
        reg.emplace<Health>(e, i);
        REQUIRE(reg.get<Health>(e) != nullptr);
        CHECK(reg.get<Health>(e)->value == i);
    }

    // Original destroyed entities must still be invalid
    for (int i = 0; i < N / 2; ++i)
        CHECK_FALSE(reg.isValid(live[static_cast<std::size_t>(i)]));
}

TEST_CASE("Registry: stale EntityID after destroy returns nullptr from get", "[registry]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.emplace<Position>(e, 7.f, 8.f, 9.f);

    EntityID stale = e;  // copy before destroy
    reg.destroyEntity(e);

    CHECK(reg.get<Position>(stale) == nullptr);
    CHECK_FALSE(reg.isValid(stale));
}

TEST_CASE("Registry: generation increments prevent stale ID reuse", "[registry]")
{
    // Create, destroy, recreate many times at the same index and confirm
    // that every old ID is invalid while the new ID is valid.
    Registry reg;
    constexpr int cycles = 50;
    std::vector<EntityID> staleIDs;

    for (int i = 0; i < cycles; ++i)
    {
        EntityID e = reg.createEntity();
        staleIDs.push_back(e);
        reg.destroyEntity(e);
    }

    EntityID live = reg.createEntity();
    CHECK(reg.isValid(live));

    for (EntityID old : staleIDs)
        CHECK_FALSE(reg.isValid(old));
}
