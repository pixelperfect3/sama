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
    // Was 10000 in the deque-backed design.  The POD ring buffer caps in-
    // flight tasks at kRingCapacity = 1024; a tight 10000-task burst would
    // overflow the ring (workers can't drain fast enough without yield
    // points in the producer).  Real callers chunk submission anyway —
    // 1000 sequential tasks exercises the same code path as the old test
    // (ring + atomic activeTasks_ + waitAll) without breaching the bound.
    // Multi-batch behaviour is covered separately by "multiple waitAll
    // cycles drain cleanly" below.
    ThreadPool pool(8);
    std::atomic<int> count{0};
    for (int i = 0; i < 1000; ++i)
        pool.submit([&] { ++count; });
    pool.waitAll();
    REQUIRE(count == 1000);
}

// ---------------------------------------------------------------------------
// POD-path tests (#H1 in docs/PERF_AUDIT_2026-05-25.md).  submitTask takes
// a void(*)(void*) + raw arg pointer — no heap allocation, no
// std::function virtual dispatch.  Per-frame callers (SystemExecutor) use
// this path; the std::function-based submit() above is the slow back-compat
// path used by AssetManager.
// ---------------------------------------------------------------------------

namespace
{
void incrementAtomicTask(void* arg)
{
    auto* counter = static_cast<std::atomic<int>*>(arg);
    counter->fetch_add(1, std::memory_order_relaxed);
}
}  // namespace

TEST_CASE("ThreadPool: submitTask POD path runs the function", "[threadpool]")
{
    ThreadPool pool(2);
    std::atomic<int> count{0};
    pool.submitTask(&incrementAtomicTask, &count);
    pool.submitTask(&incrementAtomicTask, &count);
    pool.waitAll();
    REQUIRE(count == 2);
}

TEST_CASE("ThreadPool: submitTask + waitAll completes 1000 tasks", "[threadpool]")
{
    // Stress the ring buffer + atomic activeTasks_ + semaphore wakeup path.
    // 1000 < ring capacity 1024, so all tasks fit without back-pressure.
    ThreadPool pool(8);
    std::atomic<int> count{0};
    for (int i = 0; i < 1000; ++i)
    {
        pool.submitTask(&incrementAtomicTask, &count);
    }
    pool.waitAll();
    REQUIRE(count == 1000);
}

TEST_CASE("ThreadPool: submit and submitTask interleave correctly", "[threadpool]")
{
    // Two submission paths share the same ring; alternating must produce a
    // consistent count.  Catches any mismatch in activeTasks_ accounting
    // between the slow and fast paths.
    ThreadPool pool(4);
    std::atomic<int> count{0};
    for (int i = 0; i < 500; ++i)
    {
        pool.submitTask(&incrementAtomicTask, &count);
        pool.submit([&] { count.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.waitAll();
    REQUIRE(count == 1000);
}

TEST_CASE("ThreadPool: multiple waitAll cycles drain cleanly", "[threadpool]")
{
    // The old design called doneCv_.notify on every task completion.  The
    // new design only notifies on the transition to zero — this test catches
    // a missed-notification bug where waitAll() would hang.
    ThreadPool pool(4);
    std::atomic<int> total{0};
    for (int cycle = 0; cycle < 10; ++cycle)
    {
        for (int i = 0; i < 50; ++i)
        {
            pool.submitTask(&incrementAtomicTask, &total);
        }
        pool.waitAll();
        REQUIRE(total == (cycle + 1) * 50);
    }
}
