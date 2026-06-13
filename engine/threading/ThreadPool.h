#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

namespace engine::threading
{

// ---------------------------------------------------------------------------
// ThreadPool — fixed-size worker pool.
//
// Two submission paths:
//
//   submitTask(fn, arg)        — POD path: void(*)(void*) + raw arg pointer.
//                                No heap allocation, no virtual dispatch.
//                                The fast path used by SystemExecutor and any
//                                per-frame caller where dispatch latency
//                                matters.  Caller owns `arg` and must keep it
//                                live until the task has run.
//
//   submit(std::function<void()>) — backward-compat path: heap-allocates a
//                                std::function* and dispatches it via
//                                submitTask under the hood.  Existing
//                                low-frequency users (AssetManager, the
//                                editor) keep working unchanged.
//
// Synchronisation primitives (see docs/PERF_AUDIT_2026-05-25.md items #H1,
// line 78, line 79 for the why):
//
//   - Tasks live in a bounded ring buffer.  No deque, no per-task allocation.
//   - A single short-held mutex guards the ring's head/tail indices and the
//     ring slot writes/reads.  Critical section is a few instructions; the
//     long-held mutex of the old design (which spanned `std::function`
//     allocation + CV `notify_one`) is gone.
//   - `std::counting_semaphore<>` (C++20) signals work-available — no CV with
//     mutex-held wakeup pattern.
//   - `activeTasks_` is `std::atomic<uint32_t>`.  Workers only fire the
//     `doneCv_` notify when the post-decrement reaches zero (the audit's
//     line 78 fix).
//   - `waitAll()` spin-polls the atomic briefly (~100 iterations) then
//     falls back to a mutex + CV wait for the slow path.
//
// Single-mutex-only thread-safety — `submit*` may be called from any thread,
// `waitAll()` must be called from the pool-owning thread (the one that
// constructed it).  Don't call submit* from inside a worker — that would
// queue into the same ring it's draining and can deadlock on a full ring.
// ---------------------------------------------------------------------------

class ThreadPool
{
public:
    // Bounded ring capacity.  1024 task slots — engine workloads (per-frame
    // system dispatch + occasional asset uploads) never approach this.
    // Producers that overrun assert; back-pressure is the caller's job, not
    // the pool's.
    static constexpr uint32_t kRingCapacity = 1024;

    explicit ThreadPool(uint32_t threadCount);
    ~ThreadPool();

    // Non-copyable, non-movable — owns threads.
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Submit a POD task — fastest path.  `arg` lifetime is the caller's
    // responsibility: it must outlive the task's execution (typically a
    // pointer into the caller's per-frame state, or a heap allocation the
    // task itself frees).
    //
    // Thread-safe: may be called from any thread except a worker.
    void submitTask(void (*fn)(void* arg), void* arg);

    // Submit a std::function — backward-compat slow path.  Heap-allocates a
    // `std::function*` and dispatches it via `submitTask`.  Existing users
    // unchanged.
    void submit(std::function<void()> task);

    // Block until all submitted tasks have completed.  Must be called from
    // the pool-owning thread.
    void waitAll();

    [[nodiscard]] uint32_t threadCount() const noexcept;

private:
    struct Task
    {
        void (*fn)(void*);
        void* arg;
    };

    void workerLoop();

    // Pop one task from the ring.  Returns true if a task was popped into
    // `out`, false if the ring was empty.  Caller holds `ringMutex_`.
    bool popTask(Task& out);

    std::vector<std::thread> workers_;

    // Bounded ring buffer.  head_ and tail_ are monotonic counters; the
    // slot index is `counter % kRingCapacity`.  Wrap-around is fine for
    // the 32-bit counter because we only care about (tail - head) which
    // stays small.
    std::array<Task, kRingCapacity> ring_{};
    uint32_t head_ = 0;
    uint32_t tail_ = 0;

    // Short-held mutex protecting head_, tail_, and the slot reads/writes.
    // Critical sections are a few instructions — no allocation, no
    // condition-variable interaction.
    std::mutex ringMutex_;

    // C++20 counting semaphore signals "work available" to workers.
    // INT32_MAX max count = effectively unbounded tokens.  release(n) on
    // shutdown wakes every worker.
    std::counting_semaphore<INT32_MAX> workSem_{0};

    // Number of submitted-but-not-completed tasks (includes queued and
    // currently running).  Workers decrement and notify doneCv_ ONLY on
    // the transition to zero — eliminates the audit's notify-every-time
    // overhead at line 78.
    std::atomic<uint32_t> activeTasks_{0};

    // waitAll() waits on this CV when the spin-poll didn't drain the work
    // queue fast enough.  The mutex is shared with the ring for simplicity;
    // the wait is rare (only when the pool is genuinely backed up).
    std::condition_variable doneCv_;
    std::mutex doneMutex_;

    std::atomic<bool> stop_{false};
};

}  // namespace engine::threading
