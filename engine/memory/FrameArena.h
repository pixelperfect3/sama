#pragma once

#include <cstddef>
#include <memory_resource>
#include <vector>

namespace engine::memory
{

class FrameArena
{
public:
    explicit FrameArena(size_t capacity = 1024 * 1024);

    // Get the pmr memory resource for use with std::pmr containers.
    std::pmr::memory_resource* resource() noexcept;

    // Reset the arena (call once at frame end). O(1), no destructors called.
    void reset() noexcept;

    // Stats for debugging.
    size_t bytesUsed() const noexcept;
    size_t capacity() const noexcept;

private:
    // A thin memory_resource wrapper that counts bytes allocated through it.
    class TrackingResource : public std::pmr::memory_resource
    {
    public:
        explicit TrackingResource(std::pmr::memory_resource* upstream) noexcept;

        size_t allocated() const noexcept;
        void resetCounter() noexcept;

    protected:
        void* do_allocate(size_t bytes, size_t alignment) override;
        void do_deallocate(void* p, size_t bytes, size_t alignment) override;
        bool do_is_equal(const memory_resource& other) const noexcept override;

    private:
        std::pmr::memory_resource* upstream_;
        size_t allocated_ = 0;
    };

    std::vector<std::byte> buffer_;             // backing storage (allocated once)
    std::pmr::monotonic_buffer_resource arena_; // bump allocator over buffer_
    TrackingResource tracker_;                  // counts bytes through arena_
};

}  // namespace engine::memory
