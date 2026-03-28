#include <catch2/catch_test_macros.hpp>
#include <memory_resource>
#include <vector>

#include "engine/memory/FrameArena.h"

using engine::memory::FrameArena;

TEST_CASE("FrameArena construction with default capacity", "[memory]")
{
    FrameArena arena;
    REQUIRE(arena.capacity() == 1024 * 1024);
    REQUIRE(arena.bytesUsed() == 0);
}

TEST_CASE("FrameArena construction with custom capacity", "[memory]")
{
    FrameArena arena(4096);
    REQUIRE(arena.capacity() == 4096);
    REQUIRE(arena.bytesUsed() == 0);
}

TEST_CASE("FrameArena pmr::vector backed by arena", "[memory]")
{
    FrameArena arena(4096);
    std::pmr::vector<int> vec(arena.resource());
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    REQUIRE(vec.size() == 3);
    REQUIRE(vec[0] == 1);
    REQUIRE(vec[1] == 2);
    REQUIRE(vec[2] == 3);
    REQUIRE(arena.bytesUsed() > 0);
}

TEST_CASE("FrameArena reset clears bytesUsed", "[memory]")
{
    FrameArena arena(4096);
    std::pmr::vector<int> vec(arena.resource());
    vec.push_back(42);
    REQUIRE(arena.bytesUsed() > 0);

    arena.reset();
    REQUIRE(arena.bytesUsed() == 0);
}

TEST_CASE("FrameArena multiple allocations succeed within capacity", "[memory]")
{
    FrameArena arena(8192);
    std::pmr::vector<int> v1(arena.resource());
    std::pmr::vector<double> v2(arena.resource());

    for (int i = 0; i < 100; ++i)
    {
        v1.push_back(i);
    }
    for (int i = 0; i < 50; ++i)
    {
        v2.push_back(static_cast<double>(i) * 1.5);
    }

    REQUIRE(v1.size() == 100);
    REQUIRE(v2.size() == 50);
    REQUIRE(arena.bytesUsed() > 0);
    REQUIRE(arena.bytesUsed() <= arena.capacity());
}

TEST_CASE("FrameArena bytesUsed and capacity return correct values", "[memory]")
{
    FrameArena arena(2048);
    REQUIRE(arena.capacity() == 2048);
    REQUIRE(arena.bytesUsed() == 0);

    // Allocate some memory directly through the resource.
    void* p = arena.resource()->allocate(128, alignof(std::max_align_t));
    REQUIRE(p != nullptr);
    REQUIRE(arena.bytesUsed() >= 128);
    REQUIRE(arena.capacity() == 2048);
}
