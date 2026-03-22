#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>

#include "engine/threading/ThreadPool.h"

using namespace engine::threading;

TEST_CASE("ThreadPool: tasks execute", "[threadpool]")
{
    ThreadPool pool(2);
    std::atomic<int> count{0};
    pool.submit([&] { ++count; });
    pool.submit([&] { ++count; });
    pool.waitAll();
    REQUIRE(count == 2);
}

TEST_CASE("ThreadPool: waitAll blocks until all tasks complete", "[threadpool]")
{
    ThreadPool pool(4);
    std::atomic<int> count{0};
    for (int i = 0; i < 100; ++i)
        pool.submit([&] { ++count; });
    pool.waitAll();
    REQUIRE(count == 100);
}

TEST_CASE("ThreadPool: multiple sequential batches run correctly", "[threadpool]")
{
    ThreadPool pool(4);
    std::atomic<int> total{0};

    for (int batch = 0; batch < 5; ++batch)
    {
        for (int i = 0; i < 20; ++i)
            pool.submit([&] { ++total; });
        pool.waitAll();
        REQUIRE(total == (batch + 1) * 20);
    }
}

TEST_CASE("ThreadPool: threadCount returns configured count", "[threadpool]")
{
    ThreadPool pool(3);
    REQUIRE(pool.threadCount() == 3);
}

TEST_CASE("ThreadPool: single thread processes all tasks", "[threadpool]")
{
    ThreadPool pool(1);
    std::atomic<int> count{0};
    for (int i = 0; i < 50; ++i)
        pool.submit([&] { ++count; });
    pool.waitAll();
    REQUIRE(count == 50);
}

TEST_CASE("ThreadPool: tasks from multiple submitters complete correctly", "[threadpool]")
{
    ThreadPool pool(4);

    // Submit from multiple threads simultaneously
    std::vector<std::thread> submitters;
    std::atomic<int> count{0};

    for (int t = 0; t < 4; ++t)
    {
        submitters.emplace_back(
            [&pool, &count]
            {
                for (int i = 0; i < 25; ++i)
                    pool.submit([&count] { ++count; });
            });
    }

    for (auto& t : submitters)
        t.join();

    pool.waitAll();
    REQUIRE(count == 100);
}

TEST_CASE("ThreadPool: large task count completes correctly", "[threadpool]")
{
    ThreadPool pool(8);
    std::atomic<int> count{0};
    for (int i = 0; i < 10000; ++i)
        pool.submit([&] { ++count; });
    pool.waitAll();
    REQUIRE(count == 10000);
}
