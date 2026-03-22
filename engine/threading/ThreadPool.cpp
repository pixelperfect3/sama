#include "ThreadPool.h"

#include <cassert>

namespace engine::threading
{

ThreadPool::ThreadPool(uint32_t threadCount)
{
    assert(threadCount > 0 && "ThreadPool requires at least one thread");
    workers_.reserve(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i)
        workers_.emplace_back(&ThreadPool::workerLoop, this);
}

ThreadPool::~ThreadPool()
{
    {
        std::scoped_lock lock(mutex_);
        stop_ = true;
    }
    workCv_.notify_all();
    for (auto& t : workers_)
        t.join();
}

void ThreadPool::submit(std::function<void()> task)
{
    {
        std::scoped_lock lock(mutex_);
        queue_.push_back(std::move(task));
        ++activeTasks_;
    }
    workCv_.notify_one();
}

void ThreadPool::waitAll()
{
    std::unique_lock lock(mutex_);
    doneCv_.wait(lock, [this] { return activeTasks_ == 0 && queue_.empty(); });
}

uint32_t ThreadPool::threadCount() const noexcept
{
    return static_cast<uint32_t>(workers_.size());
}

void ThreadPool::workerLoop()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            workCv_.wait(lock, [this] { return stop_ || !queue_.empty(); });

            if (stop_ && queue_.empty())
                return;

            task = std::move(queue_.front());
            queue_.pop_front();
        }

        task();

        {
            std::scoped_lock lock(mutex_);
            --activeTasks_;
        }
        doneCv_.notify_one();
    }
}

}  // namespace engine::threading
