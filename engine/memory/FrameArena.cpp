#include "engine/memory/FrameArena.h"

namespace engine::memory
{

// ---------------------------------------------------------------------------
// TrackingResource
// ---------------------------------------------------------------------------

FrameArena::TrackingResource::TrackingResource(std::pmr::memory_resource* upstream) noexcept
    : upstream_(upstream)
{
}

size_t FrameArena::TrackingResource::allocated() const noexcept
{
    return allocated_;
}

void FrameArena::TrackingResource::resetCounter() noexcept
{
    allocated_ = 0;
}

void* FrameArena::TrackingResource::do_allocate(size_t bytes, size_t alignment)
{
    void* p = upstream_->allocate(bytes, alignment);
    allocated_ += bytes;
    return p;
}

void FrameArena::TrackingResource::do_deallocate(void* p, size_t bytes, size_t alignment)
{
    upstream_->deallocate(p, bytes, alignment);
}

bool FrameArena::TrackingResource::do_is_equal(const memory_resource& other) const noexcept
{
    return this == &other;
}

// ---------------------------------------------------------------------------
// FrameArena
// ---------------------------------------------------------------------------

FrameArena::FrameArena(size_t cap)
    : buffer_(cap),
      arena_(buffer_.data(), buffer_.size(), std::pmr::null_memory_resource()),
      tracker_(&arena_)
{
}

std::pmr::memory_resource* FrameArena::resource() noexcept
{
    return &tracker_;
}

void FrameArena::reset() noexcept
{
    arena_.release();
    arena_.~monotonic_buffer_resource();
    ::new (&arena_) std::pmr::monotonic_buffer_resource(buffer_.data(), buffer_.size(),
                                                        std::pmr::null_memory_resource());
    // Reset counter only — tracker's upstream pointer (&arena_) is still valid
    // because arena_ occupies the same storage after placement new.
    tracker_.resetCounter();
}

size_t FrameArena::bytesUsed() const noexcept
{
    return tracker_.allocated();
}

size_t FrameArena::capacity() const noexcept
{
    return buffer_.size();
}

}  // namespace engine::memory
