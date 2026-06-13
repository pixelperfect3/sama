#include "ThreadPool.h"

#include <cassert>
#include <chrono>
#include <thread>

namespace engine::threading
{

namespace
{

// Trampoline for the std::function backward-compat path.  The arg pointer is
// a heap-allocated std::function*; we run it then delete.  This adds a
// per-submit heap allocation, but only on the slow path — POD users skip it
// entirely.
void runStdFunctionTrampoline(void* arg)
{
    auto* func = static_cast<std::function<void()>*>(arg);
    (*func)();
    delete func;
}

}  // namespace

ThreadPool::ThreadPool(uint32_t threadCount)
{
    assert(threadCount > 0 && "ThreadPool requires at least one thread");
    workers_.reserve(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool()
{
    stop_.store(true, std::memory_order_release);
    // Wake every worker (one token each) so they observe stop_ and exit.
    // The semaphore's INT32_MAX max count makes this safe even if some
    // workers were already mid-acquire.
    workSem_.release(static_cast<std::ptrdiff_t>(workers_.size()));
    for (auto& worker : workers_)
    {
        worker.join();
    }
}

void ThreadPool::submitTask(void (*fn)(void* arg), void* arg)
{
    assert(fn != nullptr && "ThreadPool::submitTask: null function pointer");

    {
        std::scoped_lock lock(ringMutex_);
        const uint32_t inFlight = tail_ - head_;
        // Hard-fail on ring overflow — the audit's note: back-pressure is
        // the caller's job, not the pool's.  1024 slots is plenty for any
        // engine workload; an overflow would indicate a runaway submitter.
        assert(inFlight < kRingCapacity && "ThreadPool ring overflow");
        ring_[tail_ % kRingCapacity] = Task{fn, arg};
        ++tail_;
    }

    activeTasks_.fetch_add(1, std::memory_order_relaxed);
    workSem_.release();
}

void ThreadPool::submit(std::function<void()> task)
{
    // Heap-allocate the closure so submitTask's POD path can carry it
    // through the ring.  The trampoline frees it after running.  This is
    // the slow path — existing low-frequency users (AssetManager etc.)
    // keep working; per-frame callers should use submitTask directly.
    auto* heap = new std::function<void()>(std::move(task));
    submitTask(&runStdFunctionTrampoline, heap);
}

void ThreadPool::waitAll()
{
    // Spin-poll briefly first.  For typical engine workloads (small
    // batches of fast tasks) the workers drain the ring before we'd
    // benefit from sleeping.  Avoids the doneCv_ acquire/release entirely
    // in the common case.
    for (int spin = 0; spin < 100; ++spin)
    {
        if (activeTasks_.load(std::memory_order_acquire) == 0)
        {
            return;
        }
        std::this_thread::yield();
    }

    // Slow path — actually sleep on the CV.  Workers fire doneCv_.notify
    // when activeTasks_ transitions to zero.
    std::unique_lock lock(doneMutex_);
    doneCv_.wait(lock, [this] { return activeTasks_.load(std::memory_order_acquire) == 0; });
}

uint32_t ThreadPool::threadCount() const noexcept
{
    return static_cast<uint32_t>(workers_.size());
}

bool ThreadPool::popTask(Task& out)
{
    std::scoped_lock lock(ringMutex_);
    if (head_ == tail_)
    {
        return false;
    }
    out = ring_[head_ % kRingCapacity];
    ++head_;
    return true;
}

void ThreadPool::workerLoop()
{
    while (true)
    {
        workSem_.acquire();

        // Shutdown check — destructor releases one token per worker after
        // setting stop_, so an acquire that observes stop_ + empty ring
        // means it's time to exit.
        if (stop_.load(std::memory_order_acquire))
        {
            // Drain anything still queued before exiting — fire-and-forget
            // submitters expect their work to run.  After draining we leave.
            Task task{};
            while (popTask(task))
            {
                task.fn(task.arg);
                if (activeTasks_.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    std::scoped_lock done(doneMutex_);
                    doneCv_.notify_all();
                }
            }
            return;
        }

        // Normal path — pop and run.  We might lose the race to another
        // worker (semaphore tokens and ring slots are decoupled), in which
        // case popTask returns false and we go back to acquire.
        Task task{};
        if (!popTask(task))
        {
            continue;
        }

        task.fn(task.arg);

        // The audit's line 78 fix: only notify doneCv_ on the transition
        // to zero, not on every decrement.  fetch_sub returns the prior
        // value, so prior == 1 means we hit zero.
        if (activeTasks_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            std::scoped_lock done(doneMutex_);
            doneCv_.notify_all();
        }
    }
}

}  // namespace engine::threading
