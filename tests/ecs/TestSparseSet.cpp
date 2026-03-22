#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <numeric>

#include "engine/ecs/SparseSet.h"

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
};  // zero-size component

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Fabricate a simple entity ID (generation=1 for all helpers here)
static EntityID makeE(uint32_t index)
{
    return makeEntityID(index, 1u);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("SparseSet: insert and retrieve a component", "[sparseset]")
{
    SparseSet<Position> ss;
    EntityID e = makeE(0);
    ss.insert(e, Position{1.f, 2.f, 3.f});

    Position* p = ss.get(e);
    REQUIRE(p != nullptr);
    CHECK(p->x == 1.f);
    CHECK(p->y == 2.f);
    CHECK(p->z == 3.f);
}

TEST_CASE("SparseSet: contains returns true after insert, false before", "[sparseset]")
{
    SparseSet<Health> ss;
    EntityID e = makeE(5);

    CHECK_FALSE(ss.contains(e));
    ss.insert(e, Health{42});
    CHECK(ss.contains(e));
}

TEST_CASE("SparseSet: get returns nullptr for absent entity", "[sparseset]")
{
    SparseSet<Position> ss;
    EntityID absent = makeE(99);
    CHECK(ss.get(absent) == nullptr);

    // Also absent from a set that has other entities
    ss.insert(makeE(0), Position{});
    CHECK(ss.get(absent) == nullptr);
}

TEST_CASE("SparseSet: remove makes entity absent", "[sparseset]")
{
    SparseSet<Health> ss;
    EntityID e = makeE(3);
    ss.insert(e, Health{50});
    REQUIRE(ss.contains(e));

    ss.remove(e);
    CHECK_FALSE(ss.contains(e));
    CHECK(ss.get(e) == nullptr);
    CHECK(ss.size() == 0u);
}

TEST_CASE("SparseSet: remove of non-existent entity is a no-op", "[sparseset]")
{
    SparseSet<Health> ss;
    ss.insert(makeE(1), Health{10});

    // Should not crash
    ss.remove(makeE(99));
    CHECK(ss.size() == 1u);
}

TEST_CASE("SparseSet: swap-and-pop preserves remaining entries", "[sparseset]")
{
    // Insert A(0), B(1), C(2); remove B(1); verify A and C are still accessible
    // and the dense array is compact (no holes).
    SparseSet<Health> ss;
    EntityID A = makeE(0);
    EntityID B = makeE(1);
    EntityID C = makeE(2);

    ss.insert(A, Health{10});
    ss.insert(B, Health{20});
    ss.insert(C, Health{30});
    REQUIRE(ss.size() == 3u);

    ss.remove(B);

    REQUIRE(ss.size() == 2u);
    CHECK_FALSE(ss.contains(B));
    CHECK(ss.get(B) == nullptr);

    // A and C must still be retrievable with correct values
    Health* ha = ss.get(A);
    REQUIRE(ha != nullptr);
    CHECK(ha->value == 10);

    Health* hc = ss.get(C);
    REQUIRE(hc != nullptr);
    CHECK(hc->value == 30);

    // Dense array must be fully packed (size == 2)
    auto comps = ss.components();
    CHECK(comps.size() == 2u);

    // Entity list mirrors dense array
    auto ents = ss.entities();
    CHECK(ents.size() == 2u);

    // Every entity in the entity span must be contained
    for (EntityID eid : ents)
        CHECK(ss.contains(eid));
}

TEST_CASE("SparseSet: swap-and-pop when removing the last element", "[sparseset]")
{
    SparseSet<Health> ss;
    EntityID A = makeE(0);
    EntityID B = makeE(1);
    ss.insert(A, Health{1});
    ss.insert(B, Health{2});

    // Remove the last-inserted element (no actual swap needed)
    ss.remove(B);
    REQUIRE(ss.size() == 1u);
    CHECK(ss.contains(A));
    CHECK_FALSE(ss.contains(B));
}

TEST_CASE("SparseSet: components() span reflects current state", "[sparseset]")
{
    SparseSet<Position> ss;
    CHECK(ss.components().empty());

    ss.insert(makeE(0), Position{1.f, 0.f, 0.f});
    ss.insert(makeE(1), Position{2.f, 0.f, 0.f});

    auto span = ss.components();
    CHECK(span.size() == 2u);

    // Modifying through the span must be reflected in get()
    span[0].x = 99.f;
    CHECK(ss.get(makeE(0))->x == 99.f);
}

TEST_CASE("SparseSet: entities() span reflects current state", "[sparseset]")
{
    SparseSet<Health> ss;
    EntityID e0 = makeE(0);
    EntityID e1 = makeE(1);
    ss.insert(e0, Health{1});
    ss.insert(e1, Health{2});

    auto ents = ss.entities();
    REQUIRE(ents.size() == 2u);

    // Both entities must appear in the span
    bool foundE0 = false, foundE1 = false;
    for (EntityID e : ents)
    {
        if (e == e0)
            foundE0 = true;
        if (e == e1)
            foundE1 = true;
    }
    CHECK(foundE0);
    CHECK(foundE1);
}

TEST_CASE("SparseSet: size() tracks insertions and removals correctly", "[sparseset]")
{
    SparseSet<Health> ss;
    CHECK(ss.size() == 0u);

    for (uint32_t i = 0; i < 10u; ++i)
        ss.insert(makeE(i), Health{static_cast<int>(i)});
    CHECK(ss.size() == 10u);

    for (uint32_t i = 0; i < 5u; ++i)
        ss.remove(makeE(i));
    CHECK(ss.size() == 5u);
}

TEST_CASE("SparseSet: clear() empties everything", "[sparseset]")
{
    SparseSet<Health> ss;
    for (uint32_t i = 0; i < 5u; ++i)
        ss.insert(makeE(i), Health{static_cast<int>(i)});
    REQUIRE(ss.size() == 5u);

    ss.clear();
    CHECK(ss.size() == 0u);
    CHECK(ss.components().empty());
    CHECK(ss.entities().empty());

    // Previously inserted entities must no longer be present
    for (uint32_t i = 0; i < 5u; ++i)
    {
        CHECK_FALSE(ss.contains(makeE(i)));
        CHECK(ss.get(makeE(i)) == nullptr);
    }
}

TEST_CASE("SparseSet: inserting same entity twice overwrites the component", "[sparseset]")
{
    SparseSet<Health> ss;
    EntityID e = makeE(2);

    ss.insert(e, Health{100});
    CHECK(ss.size() == 1u);
    CHECK(ss.get(e)->value == 100);

    ss.insert(e, Health{42});
    // Size must NOT grow — it's an overwrite
    CHECK(ss.size() == 1u);
    CHECK(ss.get(e)->value == 42);
}

TEST_CASE("SparseSet: zero-size component (Tag) works correctly", "[sparseset]")
{
    SparseSet<Tag> ss;
    EntityID e = makeE(0);

    CHECK_FALSE(ss.contains(e));
    ss.insert(e, Tag{});
    CHECK(ss.contains(e));
    CHECK(ss.get(e) != nullptr);
    CHECK(ss.size() == 1u);

    ss.remove(e);
    CHECK_FALSE(ss.contains(e));
    CHECK(ss.size() == 0u);
}

TEST_CASE("SparseSet: many insertions and removals remain consistent", "[sparseset]")
{
    SparseSet<Health> ss;
    constexpr int N = 200;

    for (int i = 0; i < N; ++i)
        ss.insert(makeE(static_cast<uint32_t>(i)), Health{i});

    CHECK(ss.size() == static_cast<std::size_t>(N));

    // Remove every other entity
    for (int i = 0; i < N; i += 2)
        ss.remove(makeE(static_cast<uint32_t>(i)));

    CHECK(ss.size() == static_cast<std::size_t>(N / 2));

    // Odd entities still present with correct values
    for (int i = 1; i < N; i += 2)
    {
        REQUIRE(ss.contains(makeE(static_cast<uint32_t>(i))));
        CHECK(ss.get(makeE(static_cast<uint32_t>(i)))->value == i);
    }

    // Even entities gone
    for (int i = 0; i < N; i += 2)
        CHECK_FALSE(ss.contains(makeE(static_cast<uint32_t>(i))));

    // Dense array is still fully packed
    CHECK(ss.components().size() == ss.size());
    CHECK(ss.entities().size() == ss.size());
}
