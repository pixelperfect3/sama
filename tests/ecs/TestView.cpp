#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "engine/ecs/Registry.h"
#include "engine/ecs/View.h"

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
// Single-component view tests
// ---------------------------------------------------------------------------

TEST_CASE("View: single-component view iterates all matching entities", "[view]")
{
    Registry reg;

    EntityID e0 = reg.createEntity();
    EntityID e1 = reg.createEntity();
    EntityID e2 = reg.createEntity();  // no Position

    reg.emplace<Position>(e0, 1.f, 0.f, 0.f);
    reg.emplace<Position>(e1, 2.f, 0.f, 0.f);
    reg.emplace<Health>(e2, 50);

    std::unordered_set<EntityID> visited;
    reg.view<Position>().each([&](EntityID eid, Position&) { visited.insert(eid); });

    CHECK(visited.size() == 2u);
    CHECK(visited.count(e0) == 1u);
    CHECK(visited.count(e1) == 1u);
    CHECK(visited.count(e2) == 0u);
}

TEST_CASE("View: single-component view with no matching entities iterates zero times", "[view]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.emplace<Health>(e, 99);

    int count = 0;
    reg.view<Position>().each([&](EntityID, Position&) { ++count; });
    CHECK(count == 0);
}

TEST_CASE("View: single-component view on empty registry iterates zero times", "[view]")
{
    Registry reg;
    int count = 0;
    reg.view<Position>().each([&](EntityID, Position&) { ++count; });
    CHECK(count == 0);
}

// ---------------------------------------------------------------------------
// Multi-component view tests
// ---------------------------------------------------------------------------

TEST_CASE("View: two-component view only visits entities with BOTH components", "[view]")
{
    Registry reg;

    EntityID withBoth = reg.createEntity();
    EntityID posOnly = reg.createEntity();
    EntityID velOnly = reg.createEntity();
    EntityID withNeither = reg.createEntity();

    reg.emplace<Position>(withBoth, 1.f, 0.f, 0.f);
    reg.emplace<Velocity>(withBoth, 0.f, 1.f, 0.f);

    reg.emplace<Position>(posOnly, 3.f, 0.f, 0.f);

    reg.emplace<Velocity>(velOnly, 0.f, 5.f, 0.f);

    reg.emplace<Health>(withNeither, 10);

    std::unordered_set<EntityID> visited;
    reg.view<Position, Velocity>().each([&](EntityID eid, Position&, Velocity&)
                                        { visited.insert(eid); });

    CHECK(visited.size() == 1u);
    CHECK(visited.count(withBoth) == 1u);
}

TEST_CASE("View: two-component view with no overlap iterates zero times", "[view]")
{
    Registry reg;
    EntityID e1 = reg.createEntity();
    EntityID e2 = reg.createEntity();
    reg.emplace<Position>(e1);
    reg.emplace<Velocity>(e2);

    int count = 0;
    reg.view<Position, Velocity>().each([&](EntityID, Position&, Velocity&) { ++count; });
    CHECK(count == 0);
}

TEST_CASE("View: three-component view requires all three", "[view]")
{
    Registry reg;

    EntityID allThree = reg.createEntity();
    reg.emplace<Position>(allThree);
    reg.emplace<Velocity>(allThree);
    reg.emplace<Health>(allThree, 50);

    EntityID twoOnly = reg.createEntity();
    reg.emplace<Position>(twoOnly);
    reg.emplace<Velocity>(twoOnly);

    int count = 0;
    reg.view<Position, Velocity, Health>().each(
        [&](EntityID eid, Position&, Velocity&, Health&)
        {
            ++count;
            CHECK(eid == allThree);
        });
    CHECK(count == 1);
}

// ---------------------------------------------------------------------------
// Mutation through view
// ---------------------------------------------------------------------------

TEST_CASE("View: each receives correct component references, mutations persist", "[view]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.emplace<Position>(e, 0.f, 0.f, 0.f);

    reg.view<Position>().each([](EntityID, Position& p) { p.x = 42.f; });

    REQUIRE(reg.get<Position>(e) != nullptr);
    CHECK(reg.get<Position>(e)->x == 42.f);
}

TEST_CASE("View: mutations via two-component each persist on both components", "[view]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.emplace<Position>(e, 0.f, 0.f, 0.f);
    reg.emplace<Velocity>(e, 0.f, 0.f, 0.f);

    reg.view<Position, Velocity>().each(
        [](EntityID, Position& p, Velocity& v)
        {
            p.x = 10.f;
            v.x = 20.f;
        });

    CHECK(reg.get<Position>(e)->x == 10.f);
    CHECK(reg.get<Velocity>(e)->x == 20.f);
}

// ---------------------------------------------------------------------------
// Range-based for
// ---------------------------------------------------------------------------

TEST_CASE("View: range-based for yields correct tuple", "[view]")
{
    Registry reg;
    EntityID e = reg.createEntity();
    reg.emplace<Position>(e, 5.f, 6.f, 7.f);

    int count = 0;
    for (auto [eid, pos] : reg.view<Position>())
    {
        ++count;
        CHECK(eid == e);
        CHECK(pos.x == 5.f);
        CHECK(pos.y == 6.f);
        CHECK(pos.z == 7.f);
    }
    CHECK(count == 1);
}

TEST_CASE("View: range-based for over two-component view", "[view]")
{
    Registry reg;

    EntityID e0 = reg.createEntity();
    EntityID e1 = reg.createEntity();
    reg.emplace<Position>(e0, 1.f, 0.f, 0.f);
    reg.emplace<Velocity>(e0, 0.f, 1.f, 0.f);
    reg.emplace<Position>(e1, 2.f, 0.f, 0.f);  // no Velocity

    std::unordered_set<EntityID> visited;
    for (auto [eid, pos, vel] : reg.view<Position, Velocity>())
        visited.insert(eid);

    CHECK(visited.size() == 1u);
    CHECK(visited.count(e0) == 1u);
}

TEST_CASE("View: range-based for with no matching entities produces empty range", "[view]")
{
    Registry reg;
    (void)reg.createEntity();  // entity has no components

    int count = 0;
    for (auto [eid, pos] : reg.view<Position>())
        ++count;
    CHECK(count == 0);
}

TEST_CASE("View: range-based for mutations persist", "[view]")
{
    Registry reg;
    std::vector<EntityID> entities;
    for (int i = 0; i < 5; ++i)
    {
        EntityID e = reg.createEntity();
        reg.emplace<Health>(e, i * 10);
        entities.push_back(e);
    }

    for (auto [eid, h] : reg.view<Health>())
        h.value += 1;

    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(reg.get<Health>(entities[static_cast<std::size_t>(i)]) != nullptr);
        CHECK(reg.get<Health>(entities[static_cast<std::size_t>(i)])->value == i * 10 + 1);
    }
}

// ---------------------------------------------------------------------------
// Adding / removing components between views
// ---------------------------------------------------------------------------

TEST_CASE("View: adding component between views gives correct results", "[view]")
{
    Registry reg;
    EntityID e0 = reg.createEntity();
    EntityID e1 = reg.createEntity();
    reg.emplace<Position>(e0);

    // First view: only e0
    {
        int count = 0;
        reg.view<Position>().each([&](EntityID, Position&) { ++count; });
        CHECK(count == 1);
    }

    // Add Position to e1
    reg.emplace<Position>(e1, 9.f, 9.f, 9.f);

    // Second view: both e0 and e1
    {
        int count = 0;
        reg.view<Position>().each([&](EntityID, Position&) { ++count; });
        CHECK(count == 2);
    }
}

TEST_CASE("View: removing component between views gives correct results", "[view]")
{
    Registry reg;
    EntityID e0 = reg.createEntity();
    EntityID e1 = reg.createEntity();
    reg.emplace<Position>(e0);
    reg.emplace<Position>(e1);

    // Remove from e0
    reg.remove<Position>(e0);

    std::unordered_set<EntityID> visited;
    reg.view<Position>().each([&](EntityID eid, Position&) { visited.insert(eid); });

    CHECK(visited.size() == 1u);
    CHECK(visited.count(e0) == 0u);
    CHECK(visited.count(e1) == 1u);
}

TEST_CASE("View: destroying entity between views excludes it", "[view]")
{
    Registry reg;
    EntityID e0 = reg.createEntity();
    EntityID e1 = reg.createEntity();
    reg.emplace<Position>(e0, 1.f, 0.f, 0.f);
    reg.emplace<Position>(e1, 2.f, 0.f, 0.f);

    reg.destroyEntity(e0);

    std::unordered_set<EntityID> visited;
    reg.view<Position>().each([&](EntityID eid, Position&) { visited.insert(eid); });

    // e0 was destroyed; only e1 should appear
    CHECK(visited.count(e0) == 0u);
    CHECK(visited.count(e1) == 1u);
}

// ---------------------------------------------------------------------------
// Large iteration test
// ---------------------------------------------------------------------------

TEST_CASE("View: 10k entities with Position+Velocity, iterate and update", "[view][stress]")
{
    Registry reg;
    constexpr int N = 10000;
    std::vector<EntityID> entities;
    entities.reserve(N);

    for (int i = 0; i < N; ++i)
    {
        EntityID e = reg.createEntity();
        reg.emplace<Position>(e, static_cast<float>(i), 0.f, 0.f);
        reg.emplace<Velocity>(e, 1.f, 0.f, 0.f);
        entities.push_back(e);
    }

    // Simulate one update step: pos += vel
    reg.view<Position, Velocity>().each(
        [](EntityID, Position& p, Velocity& v)
        {
            p.x += v.x;
            p.y += v.y;
            p.z += v.z;
        });

    // Verify every entity received the update
    int errors = 0;
    for (int i = 0; i < N; ++i)
    {
        Position* p = reg.get<Position>(entities[static_cast<std::size_t>(i)]);
        if (p == nullptr || p->x != static_cast<float>(i) + 1.f)
            ++errors;
    }
    CHECK(errors == 0);
}

TEST_CASE("View: 10k entities — half have both components, verify count", "[view][stress]")
{
    Registry reg;
    constexpr int N = 10000;
    std::vector<EntityID> entities;
    entities.reserve(N);

    for (int i = 0; i < N; ++i)
    {
        EntityID e = reg.createEntity();
        reg.emplace<Position>(e, static_cast<float>(i), 0.f, 0.f);
        if (i % 2 == 0)
            reg.emplace<Velocity>(e, 0.f, static_cast<float>(i), 0.f);
        entities.push_back(e);
    }

    int count = 0;
    reg.view<Position, Velocity>().each([&](EntityID, Position&, Velocity&) { ++count; });
    CHECK(count == N / 2);
}

// ---------------------------------------------------------------------------
// Tag (zero-size component) view test
// ---------------------------------------------------------------------------

TEST_CASE("View: tag-only single-component view", "[view]")
{
    Registry reg;
    EntityID tagged = reg.createEntity();
    EntityID untagged = reg.createEntity();
    reg.emplace<Tag>(tagged);
    reg.emplace<Position>(untagged);

    std::unordered_set<EntityID> visited;
    reg.view<Tag>().each([&](EntityID eid, Tag&) { visited.insert(eid); });

    CHECK(visited.size() == 1u);
    CHECK(visited.count(tagged) == 1u);
    CHECK(visited.count(untagged) == 0u);
}
