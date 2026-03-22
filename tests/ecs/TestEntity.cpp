#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/Entity.h"

using namespace engine::ecs;

// ---------------------------------------------------------------------------
// TestEntity.cpp — unit tests for EntityID packing / unpacking helpers
// ---------------------------------------------------------------------------

TEST_CASE("INVALID_ENTITY is zero", "[entity]")
{
    CHECK(INVALID_ENTITY == 0u);
}

TEST_CASE("makeEntityID / entityIndex / entityGeneration roundtrip", "[entity]")
{
    SECTION("index=1, generation=1")
    {
        EntityID id = makeEntityID(1u, 1u);
        CHECK(entityIndex(id) == 1u);
        CHECK(entityGeneration(id) == 1u);
    }

    SECTION("index=0, generation=1  (lowest valid entity)")
    {
        EntityID id = makeEntityID(0u, 1u);
        CHECK(entityIndex(id) == 0u);
        CHECK(entityGeneration(id) == 1u);
        // Must not equal INVALID_ENTITY (which is 0)
        CHECK(id != INVALID_ENTITY);
    }

    SECTION("large index, small generation")
    {
        EntityID id = makeEntityID(0xDEADBEEFu, 1u);
        CHECK(entityIndex(id) == 0xDEADBEEFu);
        CHECK(entityGeneration(id) == 1u);
    }

    SECTION("small index, large generation")
    {
        EntityID id = makeEntityID(7u, 0xCAFEBABEu);
        CHECK(entityIndex(id) == 7u);
        CHECK(entityGeneration(id) == 0xCAFEBABEu);
    }

    SECTION("max index and max generation")
    {
        EntityID id = makeEntityID(0xFFFFFFFFu, 0xFFFFFFFFu);
        CHECK(entityIndex(id) == 0xFFFFFFFFu);
        CHECK(entityGeneration(id) == 0xFFFFFFFFu);
    }

    SECTION("index=0, generation=0 equals INVALID_ENTITY")
    {
        EntityID id = makeEntityID(0u, 0u);
        CHECK(id == INVALID_ENTITY);
    }
}

TEST_CASE("entityIndex extracts only the lower 32 bits", "[entity]")
{
    // Build a value with distinct upper/lower halves
    EntityID id = (static_cast<uint64_t>(0xAAAAAAAAu) << 32u) | static_cast<uint64_t>(0xBBBBBBBBu);
    CHECK(entityIndex(id) == 0xBBBBBBBBu);
}

TEST_CASE("entityGeneration extracts only the upper 32 bits", "[entity]")
{
    EntityID id = (static_cast<uint64_t>(0xAAAAAAAAu) << 32u) | static_cast<uint64_t>(0xBBBBBBBBu);
    CHECK(entityGeneration(id) == 0xAAAAAAAAu);
}

TEST_CASE("makeEntityID is constexpr-usable", "[entity]")
{
    // Evaluated at compile time — any compile error would be caught here
    constexpr EntityID id = makeEntityID(42u, 7u);
    static_assert(entityIndex(id) == 42u);
    static_assert(entityGeneration(id) == 7u);
    CHECK(entityIndex(id) == 42u);
    CHECK(entityGeneration(id) == 7u);
}

TEST_CASE("Different index/generation pairs produce distinct IDs", "[entity]")
{
    EntityID a = makeEntityID(1u, 2u);
    EntityID b = makeEntityID(2u, 1u);
    EntityID c = makeEntityID(1u, 3u);
    EntityID d = makeEntityID(1u, 2u);  // same as a

    CHECK(a != b);
    CHECK(a != c);
    CHECK(a == d);
}
