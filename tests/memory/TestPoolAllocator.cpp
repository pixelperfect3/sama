#include <catch2/catch_test_macros.hpp>
#include <new>
#include <vector>

#include "engine/memory/PoolAllocator.h"

using engine::memory::PoolAllocator;

TEST_CASE("PoolAllocator allocate up to MaxCount succeeds", "[memory]")
{
    PoolAllocator<int, 4> pool;
    REQUIRE(pool.activeCount() == 0);
    REQUIRE(pool.capacity() == 4);

    std::vector<int*> ptrs;
    for (int i = 0; i < 4; ++i)
    {
        int* p = pool.allocate();
        REQUIRE(p != nullptr);
        ::new (p) int(i * 10);
        ptrs.push_back(p);
    }
    REQUIRE(pool.activeCount() == 4);

    // Verify stored values.
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(*ptrs[i] == i * 10);
    }

    // Cleanup.
    for (auto* p : ptrs)
    {
        pool.deallocate(p);
    }
}

TEST_CASE("PoolAllocator allocate when full returns nullptr", "[memory]")
{
    PoolAllocator<double, 2> pool;

    double* a = pool.allocate();
    double* b = pool.allocate();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    double* c = pool.allocate();
    REQUIRE(c == nullptr);

    pool.deallocate(a);
    pool.deallocate(b);
}

TEST_CASE("PoolAllocator deallocate then re-allocate succeeds", "[memory]")
{
    PoolAllocator<int, 2> pool;

    int* a = pool.allocate();
    int* b = pool.allocate();
    REQUIRE(pool.allocate() == nullptr);

    pool.deallocate(a);
    REQUIRE(pool.activeCount() == 1);

    int* c = pool.allocate();
    REQUIRE(c != nullptr);
    REQUIRE(pool.activeCount() == 2);

    // The returned pointer should be the same slot that was freed.
    REQUIRE(c == a);

    pool.deallocate(b);
    pool.deallocate(c);
}

TEST_CASE("PoolAllocator activeCount tracks correctly", "[memory]")
{
    PoolAllocator<int, 8> pool;
    REQUIRE(pool.activeCount() == 0);

    int* a = pool.allocate();
    REQUIRE(pool.activeCount() == 1);

    int* b = pool.allocate();
    REQUIRE(pool.activeCount() == 2);

    int* c = pool.allocate();
    REQUIRE(pool.activeCount() == 3);

    pool.deallocate(b);
    REQUIRE(pool.activeCount() == 2);

    pool.deallocate(a);
    REQUIRE(pool.activeCount() == 1);

    pool.deallocate(c);
    REQUIRE(pool.activeCount() == 0);
}

TEST_CASE("PoolAllocator multiple allocate/deallocate cycles", "[memory]")
{
    PoolAllocator<int, 4> pool;

    for (int cycle = 0; cycle < 10; ++cycle)
    {
        std::vector<int*> ptrs;
        for (int i = 0; i < 4; ++i)
        {
            int* p = pool.allocate();
            REQUIRE(p != nullptr);
            ::new (p) int(cycle * 100 + i);
            ptrs.push_back(p);
        }
        REQUIRE(pool.activeCount() == 4);
        REQUIRE(pool.allocate() == nullptr);

        for (auto* p : ptrs)
        {
            pool.deallocate(p);
        }
        REQUIRE(pool.activeCount() == 0);
    }
}
