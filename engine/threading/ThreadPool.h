#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace engine::threading
{

// ---------------------------------------------------------------------------
// ThreadPool
// Fixed-size worker thread pool. Thread count is set at construction and
// does not change — size should be configured per platform at startup
// (e.g. fewer threads on mobile).
//
// Usage:
//   ThreadPool pool(4);
//   pool.submit([]{ doWork(); });
//   pool.waitAll();  // block until all submitted tasks complete
// ---------------------------------------------------------------------------

class ThreadPool
{
public:
    explicit ThreadPool(uint32_t threadCount);
    ~ThreadPool();

    // Non-copyable, non-movable (owns threads)
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Submit a task for execution on a worker thread.
    // Thread-safe — may be called from any thread.
    void submit(std::function<void()> task);

    // Block until all previously submitted tasks have completed.
    // Must be called from the thread that owns the pool (not from a worker).
    void waitAll();

    [[nodiscard]] uint32_t threadCount() const noexcept;

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable workCv_;  // wakes workers when work is available
    std::condition_variable doneCv_;  // wakes waitAll() when active count hits 0
    uint32_t activeTasks_ = 0;
    bool stop_ = false;
};

}  // namespace engine::threading
