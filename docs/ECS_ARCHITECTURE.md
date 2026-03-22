# ECS Architecture

## Overview

The ECS is structured as three layers: **identity** (entities), **storage** (sparse sets), and **orchestration** (registry, views, systems).

---

## Core Structure

```mermaid
graph TD
    subgraph Identity
        EID["EntityID (uint64_t)\nupper 32 bits: generation\nlower 32 bits: index"]
        INVALID["INVALID_ENTITY = 0"]
    end

    subgraph Registry
        REG["Registry\n---\ngenerations_: vector&lt;uint32&gt;\nfreeList_: vector&lt;uint32&gt;\ncomponentStores_: map&lt;type_index, ISparseSetBase&gt;"]
    end

    subgraph Storage["Component Storage (SparseSet&lt;T&gt;)"]
        ISS["ISparseSetBase\n---\n+ removeEntity(EntityID)"]
        SS["SparseSet&lt;T&gt;\n---\nsparse_: vector&lt;uint32&gt;  entity index → dense index\ndense_: vector&lt;T&gt;        packed components\ndenseEntities_: vector&lt;EntityID&gt;"]
        ISS -->|implements| SS
    end

    subgraph Query
        VIEW["View&lt;Components...&gt;\n---\nholds SparseSet* per component type\niterates smallest set\nfilters by remaining sets"]
        ITER["ViewIterator\n---\nyields tuple&lt;EntityID, Components&...&gt;"]
        VIEW --> ITER
    end

    subgraph Systems
        ISYS["ISystem\n---\n+ update(Registry&, float dt)"]
        USYS["UserSystem : ISystem\ne.g. MovementSystem\nPhysicsSystem\nRenderSystem"]
        ISYS -->|extends| USYS
    end

    REG -->|"createEntity() / destroyEntity()"| EID
    REG -->|"stores one per component type"| ISS
    REG -->|"view[T...]() returns"| VIEW
    USYS -->|"calls reg.view[T...]().each(...)"| VIEW
```

---

## Entity Lifecycle

```mermaid
sequenceDiagram
    participant G as Game Code
    participant R as Registry
    participant F as freeList_
    participant Ge as generations_

    G->>R: createEntity()
    alt free index available
        R->>F: pop index
        R->>Ge: read generation[index]
    else no free index
        R->>Ge: push generation = 1
        note over Ge: new index allocated
    end
    R-->>G: EntityID {index | generation}

    G->>R: emplace[Position](entity, ...)
    R->>R: getOrCreateStore[Position]()
    R->>R: SparseSet[Position].insert(entity, value)

    G->>R: destroyEntity(entity)
    R->>R: isValid(entity)?
    R->>R: removeEntity from all stores
    R->>Ge: ++generation[index]
    note over Ge: stale EntityID copies now invalid
    alt generation != 0 (no overflow)
        R->>F: push index back to freeList_
    else generation wrapped to 0
        note over F: index retired permanently
    end
```

---

## Sparse Set Layout (SoA)

Each component type has its own `SparseSet<T>` — this is the SoA (Struct of Arrays) layout. The sparse and dense arrays work together to maintain O(1) insert, remove, and lookup while keeping component data packed for cache-friendly iteration.

```
Entity indices:    0     1     2     3     4     5
                   ┌─────┬─────┬─────┬─────┬─────┬─────┐
sparse_:           │  2  │  ∅  │  0  │  1  │  ∅  │  3  │   entity index → dense index
                   └─────┴─────┴─────┴─────┴─────┴─────┘
                     ↓              ↓    ↓              ↓
Dense index:         2              0    1              3
                   ┌─────┬─────┬─────┬─────┐
dense_:            │ C2  │ C0  │ C3  │ C5  │   packed component values (T)
denseEntities_:    │ E2  │ E0  │ E3  │ E5  │   parallel: which entity owns each slot
                   └─────┴─────┴─────┴─────┘

Each component type (Position, Velocity, Health...) has its own independent SparseSet.
Iteration over components_ is a tight loop over a packed array — no pointer chasing.
```

**Remove (swap-and-pop):** When removing entity E3 (dense index 1), the last element (E5, dense index 3) is swapped into slot 1, and `sparse_[5]` is updated to 1. The arrays shrink by one. O(1).

---

## View Query (Multi-Component Iteration)

```mermaid
graph LR
    subgraph "reg.view&lt;Position, Velocity&gt;().each(...)"
        SS_P["SparseSet&lt;Position&gt;\nsize: 5000"]
        SS_V["SparseSet&lt;Velocity&gt;\nsize: 800"]
        SS_H["SparseSet&lt;Health&gt;\nnot queried"]

        VIEW2["View\n1. Find smallest set → Velocity (800)\n2. Iterate Velocity.denseEntities_\n3. For each entity: check Position.contains(e)\n4. If yes → call func(e, pos, vel)"]

        SS_V -->|"smallest set — outer loop"| VIEW2
        SS_P -->|"filter check"| VIEW2
    end
```

A null store pointer (component type never used) is treated as size 0 — the view immediately yields zero entities with no side effects.

---

## System Execution Model

```mermaid
graph TD
    LOOP["Game Loop\nupdate(dt)"]
    LOOP --> S1["MovementSystem::update(reg, dt)\nreg.view&lt;Position, Velocity&gt;().each(...)"]
    LOOP --> S2["RenderSystem::update(reg, dt)\nreg.view&lt;Position, Mesh&gt;().each(...)"]
    LOOP --> S3["AudioSystem::update(reg, dt)\nreg.view&lt;AudioSource, Position&gt;().each(...)"]

    note1["Systems run sequentially today.\nFuture: systems declare read/write\ncomponent sets to enable parallel scheduling."]
```

Systems are independent units that query the registry via views. The current model runs them sequentially. The design anticipates future system-level parallelism by keeping systems stateless with respect to each other — they only communicate through components in the registry.

---

## File Structure

```
engine/ecs/
├── Entity.h          EntityID type, index/generation packing utilities
├── SparseSet.h       ISparseSetBase + SparseSet<T> template
├── Registry.h        Registry class (entity lifecycle, component management, view factory)
├── Registry.cpp      createEntity, destroyEntity, isValid implementations
├── View.h            View<Components...> + ViewIterator templates
└── System.h          ISystem abstract base

tests/ecs/
├── TestEntity.cpp    Entity ID packing/unpacking, edge cases
├── TestSparseSet.cpp Insert, remove, swap-and-pop, spans, clear, stress
├── TestRegistry.cpp  Entity lifecycle, component CRUD, generation safety, stress
└── TestView.cpp      Single/multi-component iteration, mutations, range-for, 10k stress
```

---

## Test Coverage

**57 test cases, 3423 assertions.** All tests use [Catch2 v3](https://github.com/catchorg/Catch2).

### TestEntity.cpp — 6 tests
Covers the `EntityID` bit-packing primitives:
- `INVALID_ENTITY` equals 0
- `makeEntityID` / `entityIndex` / `entityGeneration` roundtrip for normal, zero, and max (`0xFFFFFFFF`) values
- Distinct index+generation pairs always produce unique IDs
- All functions are usable as `constexpr`

### TestSparseSet.cpp — 14 tests
Covers `SparseSet<T>` in isolation:
- Insert a component and retrieve it via `get`
- `contains` returns correct true/false
- `get` returns `nullptr` for an entity that was never inserted
- Remove a component, verify it is no longer present
- **Swap-and-pop correctness** — insert A, B, C; remove B; verify A and C are still accessible and the dense array remains packed
- `components()` span reflects mutations correctly
- `entities()` span stays in sync with the dense array
- `size()` tracks insertions and removals accurately
- `clear()` empties all three internal arrays
- Double-insert on the same entity overwrites the existing value
- Zero-size (`Tag`) component works correctly
- Stress test: 200 entities with alternating removals, verify full consistency throughout

### TestRegistry.cpp — 19 tests
Covers the `Registry` end-to-end:
- `createEntity` returns a valid entity; `isValid` returns true
- `destroyEntity` invalidates the entity; `isValid` returns false
- **Generation safety** — a destroyed entity ID is invalid even if its index is later recycled by a new entity
- `emplace` and `get` roundtrip for a component
- `get` returns `nullptr` for a component the entity does not have
- `has` returns the correct boolean
- `remove<T>` removes only the specified component, leaves others intact
- `destroyEntity` removes all components from every store
- Stress: create 1000 entities, destroy half, create 300 more — verify all IDs and components are consistent
- **Stale ID cycle test** — 50 destroy/create cycles on the same index, verify old IDs never become valid again

### TestView.cpp — 18 tests
Covers `View<Components...>` iteration:
- Single-component `each` visits exactly the right entities
- Two-component `each` visits only entities that have **both** components
- Three-component `each` filters correctly across all three stores
- `each` with no matching entities iterates zero times
- Mutations inside `each` persist after the loop
- **Range-based `for`** produces identical results to `each`
- Adding a component between two view calls includes the new entity
- Removing a component between two view calls excludes it
- Destroying an entity between two view calls excludes it
- Zero-size (`Tag`) component views work correctly
- **10k entity simulation** — create 10,000 entities with `Position` and `Velocity`, run a movement step via `view<Position, Velocity>`, verify all positions updated correctly
- **10k half-coverage count** — 10,000 entities where only half have both components, verify exact count via view iteration

---

## DAG Scheduler

Systems run in parallel using a compile-time dependency graph. The scheduler is built entirely from type information — no runtime overhead.

### How Conflicts Are Detected

Two systems conflict if one **writes** a component the other **reads or writes**. Concurrent reads are always safe.

| System A | System B | Relationship |
|---|---|---|
| writes Position | reads Position | A must finish before B |
| writes Position | writes Velocity | No conflict — parallel safe |
| reads Position | reads Position | No conflict — reads are safe |

### Compile-Time Schedule

Each system declares its data dependencies as type aliases:

```cpp
struct MovementSystem : ISystem {
    using Reads  = TypeList<Velocity>;
    using Writes = TypeList<Position>;
    void update(Registry&, float dt) override;
};
```

`buildSchedule<Systems...>()` is a `constexpr` function that:
1. Builds an N×N conflict matrix from all system Reads/Writes pairs
2. Computes dependency ordering (topological sort)
3. Groups systems into **phases** — within a phase, all systems are conflict-free and run in parallel
4. Returns a `constexpr Schedule` — a plain array baked into the binary

```
Given: InputSystem, PhysicsSystem, MovementSystem, AudioSystem, RenderSystem

Phase 0: [ InputSystem ]
Phase 1: [ PhysicsSystem, MovementSystem ]   ← parallel, both only depend on Input
Phase 2: [ AudioSystem, RenderSystem ]       ← parallel, read-only after Phase 1
```

Binary size impact: ~100–500 bytes for the phase array. No `std::unordered_map`, no heap allocation, no RTTI in the scheduler.

### Execution Model

```mermaid
graph TD
    FRAME["runFrame(reg, dt)"]
    P0["Phase 0\nInputSystem"]
    P1A["Phase 1 — Thread 1\nPhysicsSystem"]
    P1B["Phase 1 — Thread 2\nMovementSystem"]
    WAIT1["waitAll()"]
    P2A["Phase 2 — Thread 1\nAudioSystem"]
    P2B["Phase 2 — Thread 2\nRenderSystem"]
    WAIT2["waitAll()"]

    FRAME --> P0
    P0 --> P1A
    P0 --> P1B
    P1A --> WAIT1
    P1B --> WAIT1
    WAIT1 --> P2A
    WAIT1 --> P2B
    P2A --> WAIT2
    P2B --> WAIT2
```

`SystemExecutor` owns the thread pool and drives the phase loop. The thread pool is fixed-size, configured at startup per platform (e.g. fewer threads on mobile).

### Constraints
- All systems must be known at **compile time** — no runtime-registered systems in this layer
- Write conflicts between systems are serialized (no parallelism) — acceptable for now, revisit if profiling shows a bottleneck
- Explicit ordering without data deps (e.g. `dependsOn(InputSystem)`) not yet supported — future consideration

### New File Structure

```
engine/ecs/
├── TypeList.h         Compile-time type list: Contains, Intersects utilities
├── Schedule.h         constexpr buildSchedule<>(), Phase/Schedule structs
├── System.h           Updated: Reads/Writes type aliases + SystemType concept
├── SystemExecutor.h   Drives phase loop against ThreadPool + Registry
└── SystemExecutor.cpp

engine/threading/
├── ThreadPool.h       Fixed-size worker thread pool
└── ThreadPool.cpp

tests/ecs/
└── TestSchedule.cpp   DAG construction, phase grouping, conflict detection

tests/threading/
└── TestThreadPool.cpp Submit, waitAll, multi-batch correctness
```

---

## Key Design Properties

| Property | How it's achieved |
|---|---|
| O(1) component insert/remove | Sparse set swap-and-pop |
| Cache-friendly iteration | Packed dense arrays per component type (SoA) |
| Stale ID detection | Generation counter in upper 32 bits of EntityID |
| Generation overflow safety | Index retired permanently if generation wraps to 0 |
| No empty-store side effects | `view()` passes null for unused component types; View handles null as empty |
| Type safety | `std::type_index` keyed map, static_cast inside Registry |
| Zero dependencies | Pure C++20 stdlib only |
| Zero-cost parallel scheduling | Compile-time DAG — phase array baked into binary, no runtime graph traversal |
| Safe parallelism | Conflict detection via Reads/Writes type aliases — data races impossible by construction |
