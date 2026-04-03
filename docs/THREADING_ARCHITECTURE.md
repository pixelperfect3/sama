# Threading Architecture Analysis for Nimbus Engine

### 0. Thread Count Summary

**How many threads run simultaneously once fully implemented?**

| Thread | Role | Always active? |
|--------|------|----------------|
| **Main thread** | OS events, input, ImGui, asset uploads, bgfx::frame(), audio API | Yes |
| **Worker 1..N** | System execution (animation, cull, draw call recording) | Only during parallel phases |
| **Jolt internal** | Physics broadphase/narrowphase/solver (managed by Jolt, not us) | Only during physics.step() |
| **bgfx render** | GPU command buffer submission (created by bgfx internally) | Yes |

**Default worker count:** `std::thread::hardware_concurrency() - 2` (reserve 1 for main, 1 for bgfx render thread). Clamped to `[1, 16]`. On a typical 8-core machine: **6 worker threads**.

**Total threads at peak (8-core machine):** 8 — main + 6 workers + bgfx render. Jolt reuses the same worker pool (see below).

**Game developer configuration:**

Thread count is specified at engine init time via `EngineDesc` and cannot change at runtime:

```cpp
EngineDesc desc;
desc.windowTitle = "My Game";
desc.workerThreadCount = 4;           // explicit count (0 = auto-detect)
desc.physicsThreadCount = 2;          // Jolt-specific (0 = share worker pool)
desc.singleThreaded = false;          // true = no workers, everything on main thread

// Auto-detect (default): workerThreadCount = 0
// -> uses std::thread::hardware_concurrency() - 2, clamped to [1, 16]
```

The `ThreadPool` is created once in `Engine::init()` with the resolved count. All engine systems use this shared pool. Jolt can either share the same pool (recommended, avoids oversubscription) or get its own via `physicsThreadCount`.

**Platform guidelines for game developers:**

| Platform | Recommended workers | Rationale |
|----------|-------------------|-----------|
| Desktop (8+ cores) | 4-6 | Plenty of cores, leave headroom for OS |
| Desktop (4 cores) | 2 | Avoid oversubscription |
| Mobile (4 big + 4 little) | 2-3 | Only use big cores, battery/thermal |
| Mobile (2 cores) | 1 | Minimal threading, or `singleThreaded = true` |
| Web/Wasm | 0 (`singleThreaded = true`) | SharedArrayBuffer support varies |

**Single-threaded mode:** When `singleThreaded = true`, no `ThreadPool` is created. All systems run sequentially on the main thread. This is the safest option for debugging, profiling, and platforms with limited threading support. The frame schedule collapses to a flat list.

### 1. System Dependency Graph

Based on the code I read, here is the complete component read/write map for every system:

| System | Reads | Writes | Structural Changes |
|---|---|---|---|
| **AnimationSystem** | SkeletonComponent, AnimatorComponent (partial) | AnimatorComponent (playbackTime, blend state), SkinComponent (boneMatrixOffset, boneCount) | None |
| **TransformSystem** | TransformComponent (flags, TRS), HierarchyComponent, ChildrenComponent | WorldTransformComponent (matrix), TransformComponent (clears dirty flag) | emplace WorldTransformComponent (first frame only) |
| **PhysicsSystem** | WorldTransformComponent, RigidBodyComponent, ColliderComponent, HierarchyComponent | TransformComponent (position, rotation, dirty flag), RigidBodyComponent (bodyID) | emplace PhysicsBodyCreatedTag |
| **AudioSystem** | WorldTransformComponent, AudioListenerComponent, AudioSourceComponent | AudioSourceComponent (busHandle, flags) | None |
| **LightClusterBuilder** | WorldTransformComponent, PointLightComponent, SpotLightComponent | Internal arrays (lights_, grid_, indices_), GPU textures | None |
| **FrustumCullSystem** | WorldTransformComponent, MeshComponent | VisibleTag | emplace/remove VisibleTag |
| **ShadowCullSystem** | WorldTransformComponent, MeshComponent | ShadowVisibleTag (cascadeMask) | emplace/remove ShadowVisibleTag |
| **DrawCallBuildSystem** | VisibleTag, ShadowVisibleTag, WorldTransformComponent, MeshComponent, MaterialComponent, SkinComponent | bgfx command buffer | None |

The dependency DAG is:

```
AnimationSystem ---+
                   +--> TransformSystem --+--> FrustumCullSystem --+--> DrawCallBuildSystem
PhysicsSystem -----+                      |                        |
                                          +--> ShadowCullSystem ---+
                                          |                        |
                                          +--> LightClusterBuilder-+
                                          |
                                          +--> AudioSystem (independent reader)
```

Key observations from the code:
- **AnimationSystem** and **PhysicsSystem** both write to TransformComponent but on *disjoint entity sets* -- AnimationSystem writes to entities with `(SkeletonComponent, AnimatorComponent, SkinComponent)`, while PhysicsSystem writes to entities with `(RigidBodyComponent, PhysicsBodyCreatedTag)` where `rb.type == Dynamic`. In practice these never overlap (animated characters are typically kinematic or have no physics body). However, the *type-level* conflict analysis in `Schedule.h` would flag this as a conflict because both declare writes to TransformComponent.
- **FrustumCullSystem**, **ShadowCullSystem**, and **LightClusterBuilder** all read WorldTransformComponent and touch disjoint write sets (VisibleTag, ShadowVisibleTag, and internal arrays respectively). These three can run in parallel.
- **AudioSystem** only reads WorldTransformComponent and writes to AudioSourceComponent (which no other system touches) plus external IAudioEngine calls. It can run in parallel with the cull systems.

### 2. Thread-Safety Audit

**Registry read-only access (get/has/view) -- Conditionally Safe**

Looking at `Registry.h` lines 70-109, `get<T>()` calls `findStore<T>()` which does a `componentStores_.find(key)` lookup on an `ankerl::unordered_dense::map`. This is a read-only operation on the map itself and is safe for concurrent reads *as long as no thread is calling `getOrCreateStore<T>()`* which modifies the map. The `SparseSet<T>::get()` method (SparseSet.h lines 79-93) reads `sparse_` and `dense_` vectors without modification -- safe for concurrent reads. The `contains()` method (line 95-99) is similarly read-only.

**Registry structural changes (emplace/remove/destroyEntity) -- NOT Safe**

`emplace<T>()` calls `getOrCreateStore<T>()` which may insert into `componentStores_` (a concurrent write to the hash map). Even if the store exists, `SparseSet::insert()` (lines 30-45) modifies `sparse_`, `dense_`, and `denseEntities_` vectors. `SparseSet::remove()` (lines 48-70) does swap-and-pop, modifying all three internal vectors. These are fundamentally unsafe for concurrent access.

Critical problem: **FrustumCullSystem** and **ShadowCullSystem** both call `reg.emplace<VisibleTag>()` and `reg.remove<VisibleTag/ShadowVisibleTag>()` during iteration. These are structural changes that prevent naive parallel execution of these systems.

**SparseSet concurrent iteration of dense array -- Safe for reads, unsafe with concurrent mutation**

`View::eachImpl()` (View.h lines 148-158) iterates via pointer arithmetic over `entities()` span and calls `contains()` + `get()` on other stores. If no thread is calling `insert()`/`remove()` on the same SparseSet, this is safe. But if one thread is removing from a SparseSet while another iterates it, the swap-and-pop invalidates iteration.

**FrameArena -- NOT thread-safe**

`std::pmr::monotonic_buffer_resource` is explicitly documented as not thread-safe. The `TrackingResource` wrapper in `FrameArena.h` adds an `allocated_` counter that is modified in `do_allocate()` -- no synchronization.

**bgfx -- Main thread only for global API, Encoder API for multi-threaded recording**

The codebase already uses `bgfx::Encoder` in `UiRenderSystem.cpp` (line 37: `bgfx::Encoder* enc = bgfx::begin()`) and `InstanceBufferBuildSystem.cpp` (line 126). The Encoder API is designed for multi-threaded draw call recording -- each thread gets its own Encoder via `bgfx::begin()`, records draw calls, and calls `bgfx::end(enc)`. The submit happens at `bgfx::frame()`.

**Jolt Physics -- Self-contained**

`physics.step()` in PhysicsSystem.cpp line 28 is self-contained with its own internal thread pool. No external synchronization needed around the step call itself, but the sync phases before and after (reading WorldTransformComponent, writing TransformComponent) must be serialized with respect to TransformSystem.

**SoLoud -- Main thread only**

AudioSystem calls `engine_.play3D()`, `engine_.setPosition()`, `engine_.setVolume()`, etc. The SoLoud mixing thread is internal, but the API calls themselves are not thread-safe for concurrent invocation from multiple threads.

### 3. Proposed Parallel Frame Schedule

```
Phase 0 (Main Thread Only):
  beginFrame() -- timing, OS events, input, ImGui begin
  Asset uploads (bgfx resource creation)

Phase 1 (Parallel Group A):
  [AnimationSystem]  +  [AudioSystem]
  - Animation: reads SkeletonComp/AnimatorComp, writes AnimatorComp/SkinComp, boneBuffer
  - Audio: reads WorldTransformComp/AudioComps, writes AudioSourceComp, calls IAudioEngine
  - SAFE: disjoint component write sets, AudioSystem uses stale WorldTransform (one frame lag acceptable for audio)
  --- BARRIER ---

Phase 2 (Sequential):
  [PhysicsSystem]
  - Reads WorldTransformComp, writes TransformComp (dirty flags)
  - Internally parallel via Jolt's own thread pool
  - Must run alone: performs structural changes (emplace PhysicsBodyCreatedTag)
  --- BARRIER ---

Phase 3 (Sequential):
  [TransformSystem]
  - Reads TransformComp, writes WorldTransformComp
  - Must run alone: performs structural changes (emplace WorldTransformComp on first frame)
  - Hierarchy traversal has poor parallelism characteristics (tree-dependent ordering)
  --- BARRIER ---

Phase 4 (Parallel Group B):
  [FrustumCullSystem]  +  [ShadowCullSystem]  +  [LightClusterBuilder]
  - All read WorldTransformComp (read-only at this point)
  - FrustumCull writes VisibleTag, ShadowCull writes ShadowVisibleTag, LightCluster writes internal arrays
  - PROBLEM: FrustumCull and ShadowCull both call emplace/remove (structural changes)
  - SOLUTION: Use deferred command buffers (see Section 4)
  --- BARRIER ---

Phase 5 (Main Thread Only):
  [DrawCallBuildSystem] -- all submit calls (bgfx::submit)
  [PostProcessSystem] -- fullscreen triangle submits
  endFrame() -- ImGui end, arena reset, bgfx::frame()
```

### 4. Required Changes to Enable Parallelism

**4a. Thread-Local FrameArenas**

The current single FrameArena (`engine/memory/FrameArena.h`) uses `std::pmr::monotonic_buffer_resource` which is not thread-safe. Two options:

Option A (recommended): Create one FrameArena per worker thread, stored in thread-local storage or indexed by thread ID. Each worker allocates from its own arena. All arenas are reset together at frame end. This requires changing `AnimationSystem::update()` to accept a thread-specific arena.

Option B: Wrap the existing arena with a `std::pmr::synchronized_pool_resource` or a simple spinlock around `do_allocate`. Simpler but adds contention.

Implementation: Add a `ThreadFrameArenas` class that holds `N+1` arenas (N workers + main thread). Provide `arenaForThread(threadIndex)` accessor.

File changes: `engine/memory/FrameArena.h` (add thread-local variant), `engine/core/Engine.cpp` (create per-thread arenas), `engine/animation/AnimationSystem.cpp` (accept thread-indexed arena).

**4b. Deferred Command Buffers for ECS Structural Changes**

The biggest blocker for parallelism is that FrustumCullSystem and ShadowCullSystem call `reg.emplace<VisibleTag>()` and `reg.remove<VisibleTag>()` during iteration. This is a structural change to the SparseSet.

Solution: Introduce a `CommandBuffer` class:

```cpp
class CommandBuffer {
    struct AddCmd { EntityID entity; std::type_index type; /* data */ };
    struct RemoveCmd { EntityID entity; std::type_index type; };
    std::vector<AddCmd> adds_;
    std::vector<RemoveCmd> removes_;
public:
    template<typename T> void emplace(EntityID e, T&& val);
    template<typename T> void remove(EntityID e);
    void playback(Registry& reg); // apply all deferred changes
};
```

Each system running on a worker thread would receive its own `CommandBuffer`. After the parallel phase completes (barrier), command buffers are played back sequentially on the main thread.

For FrustumCullSystem: instead of `reg.emplace<VisibleTag>(entity)`, it would call `cmds.emplace<VisibleTag>(entity)`. This changes the system signatures:

```cpp
void FrustumCullSystem::update(ecs::Registry& reg, const RenderResources& res,
                                const math::Frustum& frustum, ecs::CommandBuffer& cmds);
```

File changes: New file `engine/ecs/CommandBuffer.h`, modifications to `FrustumCullSystem.cpp`, `ShadowCullSystem.cpp`, `TransformSystem.cpp` (for first-frame emplace), `PhysicsSystem.cpp` (for PhysicsBodyCreatedTag).

**4c. Read-Only vs Read-Write Component Access Patterns**

The current `View` class returns non-const references. For parallel safety, add a `ConstView` variant:

```cpp
template <typename... Components>
[[nodiscard]] ConstView<Components...> constView() const;
```

This would return `const T&` for all components and prevent accidental writes. Systems in a parallel group that only read a component type should use `constView`.

File changes: `engine/ecs/View.h` (add ConstView), `engine/ecs/Registry.h` (add constView method).

**4d. bgfx Encoder API for Multi-Threaded Draw Call Recording**

The codebase already demonstrates `bgfx::Encoder` usage in `UiRenderSystem.cpp` and `InstanceBufferBuildSystem.cpp`. To parallelize DrawCallBuildSystem:

- Split entity iteration across N threads
- Each thread gets its own `bgfx::Encoder* enc = bgfx::begin()`
- Replace all `bgfx::setTransform/setVertexBuffer/setIndexBuffer/setState/submit` with `enc->setTransform/...`
- Call `bgfx::end(enc)` when done

This enables shadow draw calls for different cascades to be recorded in parallel, and static vs skinned PBR passes to be recorded in parallel.

File changes: `engine/rendering/systems/DrawCallBuildSystem.cpp` (use Encoder), `engine/rendering/systems/DrawCallBuildSystem.h` (update signatures).

**4e. Lock-Free Data Structures**

For the deferred command buffer approach, no lock-free structures are needed because each thread has its own buffer. However, if the parallel cull systems need to produce a shared visibility list instead of modifying tags, a concurrent append-only vector (using atomic size counter + pre-allocated backing array) would work:

```cpp
template<typename T>
class ConcurrentAppendVector {
    std::vector<T> data_;
    std::atomic<uint32_t> size_{0};
public:
    ConcurrentAppendVector(uint32_t capacity) { data_.resize(capacity); }
    void push_back(T val) {
        uint32_t idx = size_.fetch_add(1, std::memory_order_relaxed);
        data_[idx] = val;
    }
};
```

This is useful for collecting cull results as entity ID lists rather than tag components.

### 5. What NOT to Parallelize (and Why)

**TransformSystem** -- The hierarchy traversal is inherently sequential because children depend on parent world matrices. Parallelizing across independent scene graph subtrees is possible but adds complexity for minimal gain (TransformSystem costs 0.05-0.5ms per FRAME_ANATOMY.md). The dirty-flag optimization already skips most of the tree in steady state.

**InputSystem, ImGui begin/end** -- These are <0.1ms combined and touch OS/window state that requires main-thread access (GLFW, ImGui).

**PhysicsSystem sync phases** -- The pre-step kinematic sync and post-step dynamic writeback iterate small entity counts (typically <100 physics bodies) and perform structural changes. The actual heavy computation is `physics.step()` which is already internally parallel via Jolt's thread pool.

**PostProcessSystem** -- Each pass is a single fullscreen triangle draw call. The CPU cost is negligible (<0.1ms). All work is GPU-side.

**LightClusterBuilder at small light counts** -- With fewer than ~16 lights, the cluster build is <0.1ms. The 3456-cluster iteration with 256-light-per-cluster test is O(N*M) but with small N the constant overhead of thread dispatch exceeds the computation.

**DrawCallBuildSystem at small entity counts** -- Below ~500 visible entities, the per-entity uniform setup is fast enough that thread dispatch overhead (mutex lock + condition variable signal + cache line transfers) dominates. The break-even point for bgfx::Encoder parallelism is roughly 1000+ draw calls.

### 6. Performance Estimates

**Current frame budget**: ~4ms CPU at 60fps for a typical scene (per FRAME_ANATOMY.md timing).

Breakdown from the frame anatomy document:
- Animation: 0.1-1.0ms
- Transform: 0.05-0.5ms
- Physics: 0.5-4.0ms (internally parallel)
- Light Cluster: 0.1-0.5ms
- Frustum + Shadow Cull: 0.05-0.3ms
- Draw call submission: 0.3-1.5ms (shadow + opaque)
- Post-process: 0.1-0.5ms

**100 entities** (simple scene): Total CPU ~1.5ms. Threading overhead for context switches and synchronization barriers adds ~0.1-0.2ms. Expected speedup: ~1.0x (no benefit, possibly slower). Recommendation: stay single-threaded.

**1,000 entities** (moderate scene): Total CPU ~3.0ms. Phase 4 parallel group (FrustumCull + ShadowCull + LightCluster) saves ~0.2ms by overlapping. Phase 1 parallel (Animation + Audio) saves ~0.1ms. Net savings ~0.3ms, reducing to ~2.7ms. Expected speedup: ~1.1x. Marginal benefit.

**10,000 entities** (complex scene): Total CPU ~8-12ms (exceeds 60fps budget). FrustumCull alone costs ~1.5ms, ShadowCull ~1.5ms, DrawCallBuild ~3ms. Phase 4 parallel saves ~1.5ms. Multi-threaded DrawCallBuild via Encoder saves ~1.5ms. Animation with many skinned characters saves ~0.5ms. Net savings ~3.5ms, reducing from ~10ms to ~6.5ms. Expected speedup: ~1.5x. This is where parallelism becomes essential.

**Threading overhead**: Each `ThreadPool::submit()` involves a mutex lock, deque push, and condition variable notify (~0.5-2us on modern x86/ARM). A barrier (`waitAll()`) involves a mutex lock + condition variable wait (~1-5us). With 4-5 barriers per frame, overhead is ~10-25us total. Break-even at ~50us of parallel work per group.

### 7. Implementation Phases

**Phase 1: Thread-Safe Read-Only Component Access (1-2 weeks)**

Goal: Ensure `Registry::get<T>()`, `has<T>()`, and `view<T>()` are provably safe for concurrent reads when no writes occur.

Tasks:
1. Add `static_assert` or runtime assertions that `getOrCreateStore<T>()` is never called during a parallel phase. In practice, all component types are registered during scene setup, so `findStore<T>()` always finds an existing entry.
2. Add a `ConstView<Components...>` to `View.h` that returns `const T&` references.
3. Add `Registry::constView<Components...>()` method.
4. Add a debug-mode `Registry::lockForReading()` / `unlockForReading()` pair that asserts no structural changes happen while locked (debug builds only, zero cost in release).
5. Audit all `SparseSet` methods used by `View` iteration to confirm no hidden mutations (verified above -- `contains()` and `get()` are pure reads).

Files: `engine/ecs/Registry.h`, `engine/ecs/View.h`, `engine/ecs/SparseSet.h`.

**Phase 2: Parallel Cull Systems with Deferred Commands (2-3 weeks)**

Goal: Run FrustumCullSystem + ShadowCullSystem + LightClusterBuilder in parallel.

Tasks:
1. Create `engine/ecs/CommandBuffer.h` with `emplace<T>()`, `remove<T>()`, and `playback(Registry&)`.
2. Modify `FrustumCullSystem::update()` to accept a `CommandBuffer&` and defer all `emplace<VisibleTag>` / `remove<VisibleTag>` calls.
3. Modify `ShadowCullSystem::update()` similarly for `ShadowVisibleTag`.
4. In the frame loop, after the parallel cull phase completes, call `cmds.playback(reg)` on the main thread.
5. LightClusterBuilder already writes only to internal arrays (`lights_[]`, `grid_[]`, `indices_[]`) and GPU textures -- no ECS changes needed. However, `uploadTextures()` calls `bgfx::updateTexture2D()` which must happen on the main thread. Split `update()` into `collectAndBuild()` (parallelizable) and `upload()` (main thread).

Files: New `engine/ecs/CommandBuffer.h`, `engine/rendering/systems/FrustumCullSystem.cpp`, `engine/rendering/systems/ShadowCullSystem.cpp`, `engine/rendering/LightClusterBuilder.h/.cpp`.

**Phase 3: Parallel Animation + Audio (1-2 weeks)**

Goal: Run AnimationSystem and AudioSystem concurrently during the simulation phase.

Tasks:
1. Create `ThreadFrameArenas` class -- one `FrameArena` per worker thread plus main thread. Store as `std::vector<std::unique_ptr<FrameArena>>`.
2. Modify `AnimationSystem::update()` to accept a thread-specific arena.
3. AudioSystem already makes no arena allocations and writes to disjoint components (`AudioSourceComponent`). The only concern is `IAudioEngine` API calls, which must be main-thread-only for SoLoud. Solution: defer audio API calls to a command list, replay on main thread. Or: keep AudioSystem on main thread and only parallelize Animation with other independent work.
4. Revised approach: run AnimationSystem on a worker thread (with its own arena) while the main thread runs AudioSystem. This avoids the SoLoud threading issue entirely.

Files: `engine/memory/FrameArena.h` (add ThreadFrameArenas), `engine/core/Engine.cpp`, `engine/animation/AnimationSystem.cpp`.

**Phase 4: Multi-Threaded Draw Call Recording via bgfx::Encoder (2-3 weeks)**

Goal: Parallelize draw call submission for scenes with >1000 visible entities.

Tasks:
1. Modify `DrawCallBuildSystem` methods to accept `bgfx::Encoder*` instead of using global bgfx functions.
2. Split entity iteration: partition the visible entity list into N chunks (one per worker thread).
3. Each worker calls `bgfx::begin()` to get an Encoder, records its chunk of draw calls, calls `bgfx::end(enc)`.
4. Shadow cascades are naturally independent -- record cascade 0 and cascade 1 on different threads.
5. Static PBR and skinned PBR passes can be recorded in parallel since they iterate disjoint entity sets (skinned entities are skipped in the static pass via `reg.has<SkinComponent>(entity)` check).
6. Verify bgfx single-threaded mode is compatible with Encoder API. The bgfx documentation states that `bgfx::begin()/end()` works regardless of threading mode -- the Encoder just defers to the global state in single-threaded mode. Multi-encoder requires that bgfx was NOT initialized in single-threaded mode. This means removing the `bgfx::renderFrame()` call before `bgfx::init()` in `Renderer::init()`, or using a different initialization path.

Files: `engine/rendering/systems/DrawCallBuildSystem.h/.cpp`, `engine/rendering/Renderer.cpp` (init changes).

**Phase 5: Full DAG Scheduler Integration (3-4 weeks)**

Goal: Replace the hand-coded frame loop with the compile-time DAG scheduler already built in `Schedule.h` and `SystemExecutor.h`.

Tasks:
1. Add `using Reads` and `using Writes` type aliases to all concrete system classes. Currently only the documentation and tests declare these -- the actual system headers (TransformSystem.h, FrustumCullSystem.h, etc.) do not.
2. Unify system `update()` signatures to match `ISystem::update(Registry&, float)`. Currently, systems take wildly different parameters (AnimationSystem takes `AnimationResources&` and `arena`, FrustumCullSystem takes `RenderResources&` and `Frustum&`, etc.). Either: (a) store extra parameters as system member state, or (b) extend ISystem to support a `FrameContext` struct passed to all systems.
3. Instantiate `SystemExecutor<AnimationSystem, TransformSystem, PhysicsSystem, ...>` and call `runFrame()` instead of manually calling each system.
4. Handle the "disjoint entity set" optimization: AnimationSystem and PhysicsSystem both write TransformComponent but to disjoint entities. The compile-time conflict detector in `Schedule.h` would conservatively place them in separate phases. To allow parallel execution, either: (a) split TransformComponent into separate types for animated vs physics entities, or (b) add a runtime "archetype" check that validates disjoint entity sets, or (c) accept the conservative ordering (simplest, minimal perf loss since PhysicsSystem's heavy work is in `physics.step()` which is already parallel).

Files: All system headers (add Reads/Writes), `engine/ecs/SystemExecutor.h` (extend for FrameContext), `engine/core/Engine.cpp` (replace manual loop with executor).

### Critical Files for Implementation
- /Users/shayanj/claude/engine/engine/ecs/Registry.h
- /Users/shayanj/claude/engine/engine/ecs/Schedule.h
- /Users/shayanj/claude/engine/engine/ecs/SystemExecutor.h
- /Users/shayanj/claude/engine/engine/rendering/systems/FrustumCullSystem.cpp
- /Users/shayanj/claude/engine/engine/memory/FrameArena.h