#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>

namespace engine::memory
{

template <typename T, size_t MaxCount>
class PoolAllocator
{
    static_assert(MaxCount > 0, "MaxCount must be greater than 0");

public:
    PoolAllocator() noexcept
    {
        for (uint32_t i = 0; i < MaxCount; ++i)
        {
            freeList_[i] = i;
        }
    }

    // Non-copyable, non-movable.
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) = delete;
    PoolAllocator& operator=(PoolAllocator&&) = delete;

    /// Allocate a slot and return a pointer suitable for placement-new.
    /// Returns nullptr if the pool is full. Does NOT call any constructor.
    T* allocate() noexcept
    {
        if (freeCount_ == 0)
        {
            return nullptr;
        }
        --freeCount_;
        uint32_t index = freeList_[freeCount_];
        return reinterpret_cast<T*>(storage_ + sizeof(T) * index);
    }

    /// Return a previously-allocated slot to the free list.
    /// Does NOT call any destructor. The caller must destroy the object first.
    void deallocate(T* ptr) noexcept
    {
        auto* raw = reinterpret_cast<std::byte*>(ptr);
        assert(raw >= storage_ && raw < storage_ + sizeof(T) * MaxCount);
        size_t offset = static_cast<size_t>(raw - storage_);
        assert(offset % sizeof(T) == 0);
        uint32_t index = static_cast<uint32_t>(offset / sizeof(T));
        assert(freeCount_ < MaxCount);
        freeList_[freeCount_] = index;
        ++freeCount_;
    }

    /// Number of currently-allocated (in-use) slots.
    size_t activeCount() const noexcept
    {
        return MaxCount - freeCount_;
    }

    /// Total number of slots in the pool.
    static constexpr size_t capacity() noexcept
    {
        return MaxCount;
    }

private:
    alignas(T) std::byte storage_[sizeof(T) * MaxCount];
    uint32_t freeList_[MaxCount];
    uint32_t freeCount_ = static_cast<uint32_t>(MaxCount);
};

}  // namespace engine::memory
