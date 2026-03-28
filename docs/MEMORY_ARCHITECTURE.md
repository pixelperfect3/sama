# Memory Allocation Architecture

## Goal

Zero dynamic memory allocations per frame in the hot path. All per-frame temporary data uses pre-allocated arenas or inline storage. Persistent allocations (entity creation, asset loading) happen at load time or on background threads, never in the frame loop.

## Strategy: std::pmr + Custom Allocators (No Large Dependencies)

Use C++20's built-in `std::pmr` (Polymorphic Memory Resources) for per-frame arenas, combined with a small inline-vector utility and a single-header open-addressing hash map. This avoids adding large dependencies (absl, Boost, EASTL) while achieving the allocation-free frame target.

### Why Not absl/EASTL/Boost?

| Option | Binary Size | Fit |
|--------|------------|-----|
| **absl** | ~50-150KB (containers only) | No arena/pool allocators — doesn't solve the core problem |
| **EASTL** | Minimal (header-only) | Best game-engine fit but lower maintenance activity |
| **Boost** | Large header footprint | Overkill; pulls in too much |
| **std::pmr** | Zero (built into C++20) | Perfect for arenas, no dependency |

absl's `flat_hash_map` and `InlinedVector` are good containers but absl provides no allocator infrastructure. EASTL is purpose-built for games but adds a dependency for what can be solved with std::pmr + ~200 lines of custom code.

## Components

### 1. FrameArena — Per-Frame Bump Allocator

A `std::pmr::monotonic_buffer_resource` backed by a pre-allocated buffer. Allocates linearly (bump pointer), never frees individual allocations — the entire arena is reset at frame end.

```cpp
namespace engine::memory
{

class FrameArena
{
public:
    explicit FrameArena(size_t capacity = 1024 * 1024);  // 1 MB default

    // Get the pmr allocator for use with std::pmr containers.
    std::pmr::memory_resource* resource() noexcept;

    // Reset the arena (call once at frame end). O(1), no destructors called.
    void reset() noexcept;

    // Stats for debugging.
    size_t bytesUsed() const noexcept;
    size_t capacity() const noexcept;

private:
    std::vector<std::byte> buffer_;            // backing storage (allocated once)
    std::pmr::monotonic_buffer_resource arena_; // bump allocator over buffer_
};

}
```

**Usage in the frame loop:**

```cpp
engine::memory::FrameArena frameArena(2 * 1024 * 1024);  // 2 MB, allocated once

while (running)
{
    // Per-frame temporary containers — zero heap allocations.
    std::pmr::vector<DrawCall> drawCalls(frameArena.resource());
    std::pmr::vector<LightData> visibleLights(frameArena.resource());

    // ... fill and use ...

    frameArena.reset();  // O(1) reset, ready for next frame
}
```

### 2. PoolAllocator — Fixed-Size Object Pools

For objects that are frequently created/destroyed at runtime (particles, projectiles, audio voices) but have a known maximum count:

```cpp
namespace engine::memory
{

template <typename T, size_t MaxCount>
class PoolAllocator
{
public:
    T* allocate();              // O(1), returns nullptr if full
    void deallocate(T* ptr);    // O(1), adds back to free list
    size_t activeCount() const;
    size_t capacity() const;    // = MaxCount

private:
    alignas(T) std::byte storage_[sizeof(T) * MaxCount];
    uint32_t freeList_[MaxCount];
    uint32_t freeCount_ = MaxCount;
};

}
```

### 3. InlinedVector — Small-Buffer Optimized Vector

Stores up to N elements inline (no heap). Falls back to heap if capacity exceeded. Replaces `std::vector` for small, bounded collections (child lists, component query results, scratch buffers).

```cpp
namespace engine::memory
{

template <typename T, size_t N>
class InlinedVector
{
public:
    // std::vector-compatible API: push_back, pop_back, operator[], begin/end,
    // size, capacity, clear, resize, reserve.
    // First N elements stored in inline buffer. Beyond N, spills to heap.

private:
    alignas(T) std::byte inlineStorage_[sizeof(T) * N];
    T* data_ = reinterpret_cast<T*>(inlineStorage_);
    size_t size_ = 0;
    size_t capacity_ = N;
    bool heapAllocated_ = false;
};

}
```

### 4. HashMap — Open-Addressing Flat Hash Map

Replace `std::unordered_map` (node-based, pointer-chasing, per-insert heap allocation) with a flat, open-addressing hash map. Use `ankerl::unordered_dense` (single header, MIT license, ~1500 lines) or write a custom robin-hood map.

Key properties:
- Values stored inline in a contiguous array (cache-friendly)
- No per-entry heap allocation
- SIMD-friendly metadata for fast probing
- ~2-3x faster than `std::unordered_map` for lookup/insert

**Where this matters most:**
- `Registry::componentStores_` — currently `std::unordered_map<std::type_index, unique_ptr<ISparseSetBase>>`
- `RenderResources` internal maps
- Any system that does per-frame map lookups

## Migration Plan

### Phase 1: FrameArena (Immediate Impact)

1. Create `engine/memory/FrameArena.h` and `.cpp` — **done**
2. Add `FrameArena` to the main loop in each app — callers pass `frameArena.resource()` to systems
3. Convert per-frame `std::vector` allocations in render systems to `std::pmr::vector` backed by the arena — **done** for `InstanceBufferBuildSystem`
4. Key targets identified after audit:
   - `InstanceBufferBuildSystem` — **converted**: per-frame `GroupData::instances` vectors now use `std::pmr::vector<InstanceEntry>` backed by an optional arena. The `update()` method accepts an optional `std::pmr::memory_resource*` (defaults to `nullptr` for backward compatibility; falls back to `std::pmr::get_default_resource()`).
   - `DrawCallBuildSystem` — no per-frame local vectors; iterates ECS views and submits directly. No change needed.
   - `FrustumCullSystem` — no per-frame local vectors; iterates ECS views directly. No change needed.
   - `LightClusterBuilder` — already uses fixed-size `std::array` members; no per-frame heap allocation. No change needed.

### Phase 2: InlinedVector (Reduce Heap Pressure)

1. Create `engine/memory/InlinedVector.h`
2. Replace `ChildrenComponent::children` with `InlinedVector<EntityID, 8>` (most nodes have <8 children)
3. Replace small scratch vectors in systems

### Phase 3: Flat Hash Map (Persistent Data Structures)

1. Vendor `ankerl::unordered_dense.h` (single header, MIT)
2. Replace `std::unordered_map` in `Registry::componentStores_`
3. Replace `std::unordered_map` in `RenderResources`
4. Replace `std::unordered_map` in `JoltPhysicsEngine` body/entity mappings

### Phase 4: PoolAllocator (Specialized Pools)

1. Create `engine/memory/PoolAllocator.h`
2. Use for particle systems, audio voice handles, network message buffers
3. Only add pools where profiling shows allocation pressure

## Allocation Budget

| Category | Allocator | Per-Frame Allocs | Notes |
|----------|-----------|-----------------|-------|
| Draw calls, visibility lists | FrameArena | 0 | Reset each frame |
| ECS component storage | SparseSet (existing) | 0 (steady state) | Grows only on entity creation |
| Scene graph child lists | InlinedVector<8> | 0 (typical) | Heap only if >8 children |
| Hash map lookups | Flat hash map | 0 (steady state) | Grows only on insert |
| Asset loading | Standard allocator | N/A | Background thread, not in frame loop |
| Particles, projectiles | PoolAllocator | 0 | Pre-allocated pool |

## File Layout

```
engine/memory/
    FrameArena.h           // std::pmr monotonic bump allocator
    FrameArena.cpp
    InlinedVector.h        // small-buffer optimized vector (header-only)
    PoolAllocator.h        // fixed-size object pool (header-only)
third_party/
    ankerl/
        unordered_dense.h  // single-header flat hash map (vendored, MIT)
tests/memory/
    TestFrameArena.cpp
    TestInlinedVector.cpp
    TestPoolAllocator.cpp
```

## Testing

- **FrameArena**: verify zero heap allocations via pmr containers, reset clears usage, capacity limits
- **InlinedVector**: verify inline storage up to N, heap fallback beyond N, move semantics, iterator validity
- **PoolAllocator**: verify allocate/deallocate cycle, full pool returns nullptr, no leaks
- **Integration**: instrument a frame with allocation counting, verify zero dynamic allocations in steady state

## Future FrameArena Candidates

Systems that could benefit from FrameArena when scenes scale up:
- `PhysicsSystem::registerNewBodies/cleanupDestroyedBodies` — currently uses InlinedVector<16>, switch to pmr::vector if body counts exceed 16/frame
- `DrawCallBuildSystem` — if draw call sorting/batching is added, temporary sort buffers should use the arena
- `LightClusterBuilder` — currently uses fixed arrays, but dynamic cluster assignment would benefit
- Any future particle system, visibility query, or spatial partitioning that builds per-frame lists

The arena is pre-allocated in demos (2MB) and ready to use — pass `frameArena.resource()` to any system that accepts `std::pmr::memory_resource*`.

## Deferred

- **Thread-local arenas**: Each worker thread gets its own FrameArena for parallel system execution. Add when the system executor runs systems on multiple threads.
- **GPU upload staging**: Ring buffer for bgfx transient vertex/index buffers. Add when profiling shows staging allocation pressure.
- **Custom global allocator**: Override `operator new` with a tracking allocator to detect unexpected heap allocations in debug builds. Add when the allocation budget is enforced.
