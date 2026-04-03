# Physics Architecture

## 1. Overview

This document defines the architecture for integrating physics simulation into the Sama engine. The physics backend is Jolt Physics, accessed exclusively through an `IPhysicsEngine` interface so the backend can be replaced (e.g., with PhysX) without touching game code or other engine systems.

The design follows the same conventions as the existing scene graph: hierarchy is encoded in ECS components, systems operate on views of those components, and ground truth flows in one direction per frame phase.

## 2. Integration Approach

### 2.1 Build Integration

Jolt is added via `FetchContent` in `CMakeLists.txt`, following the exact pattern used for Catch2, glm, glfw, bgfx, and cgltf.

```cmake
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0   # pin to a release tag
)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)
```

A new static library target `engine_physics` is created:

```cmake
add_library(engine_physics STATIC
    engine/physics/JoltPhysicsEngine.cpp
    engine/physics/PhysicsSystem.cpp
    engine/physics/PhysicsConversions.h  # header-only glm<->Jolt
)
target_include_directories(engine_physics PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_physics PUBLIC engine_ecs engine_scene Jolt glm::glm)
```

### 2.2 Namespace

All physics code lives under `engine::physics`. Jolt-specific implementation details are confined to `.cpp` files and never leak into public headers (except `JoltPhysicsEngine.h`, which is the concrete backend and only included by the bootstrap/init code).

### 2.3 Initialization and Shutdown Lifecycle

Jolt requires one-time global initialization (`JPH::RegisterDefaultAllocator()`, `JPH::Factory::sInstance`, type registration) and a per-world `PhysicsSystem` plus `JobSystemThreadPool`. The engine manages this as follows:

1. **Application startup**: Call `IPhysicsEngine::init()`. The Jolt implementation (`JoltPhysicsEngine`) calls `JPH::RegisterDefaultAllocator()`, creates `JPH::Factory`, registers all Jolt types, and creates the `JPH::PhysicsSystem` and `JPH::JobSystemThreadPool`.
2. **Per-frame**: Call `IPhysicsEngine::step(float deltaTime)` from `PhysicsSystem::update()`.
3. **Application shutdown**: Call `IPhysicsEngine::shutdown()`. Jolt cleans up in reverse order: destroy all bodies, then the physics system, then the job system, then `JPH::Factory`.

The `IPhysicsEngine` is owned by the application (or a top-level `Engine` object), not by the ECS Registry. It is passed to `PhysicsSystem` by pointer.

## 3. ECS Components

### 3.1 RigidBodyComponent

```cpp
namespace engine::physics
{

enum class BodyType : uint8_t
{
    Static,     // never moves (floors, walls)
    Dynamic,    // moved by physics simulation
    Kinematic   // moved by game code, pushes dynamic bodies
};

struct RigidBodyComponent                // offset  size
{
    uint32_t bodyID = ~0u;               //  0       4   Jolt BodyID, set by PhysicsSystem
    float mass = 1.0f;                   //  4       4
    float linearDamping = 0.05f;         //  8       4
    float angularDamping = 0.05f;        // 12       4
    float friction = 0.5f;              // 16       4
    float restitution = 0.3f;           // 20       4
    BodyType type = BodyType::Dynamic;   // 24       1
    uint8_t layer = 0;                   // 25       1   collision layer index
    uint8_t _pad[2] = {};               // 26       2
};  // total: 28 bytes
```

`bodyID` is an opaque handle managed by the physics engine. Game code sets `type`, `mass`, and material properties; `PhysicsSystem` fills in `bodyID` when the body is registered with Jolt.

### 3.2 ColliderComponent

```cpp
enum class ColliderShape : uint8_t
{
    Box,
    Sphere,
    Capsule,
    Mesh       // triangle mesh, static only
};

struct ColliderComponent                   // offset  size
{
    math::Vec3 offset{0.0f};              //  0      12   local offset from entity origin
    math::Vec3 halfExtents{0.5f};         // 12      12   box half-extents, or (radius, halfHeight, 0) for capsule
    float radius = 0.5f;                  // 24       4   sphere/capsule radius
    ColliderShape shape = ColliderShape::Box; // 28    1
    uint8_t _pad[3] = {};                 // 29       3
};  // total: 32 bytes
```

For `ColliderShape::Mesh`, the triangle data is sourced from the entity's `MeshComponent` at body creation time. The half-extents field is repurposed per shape type:
- **Box**: `halfExtents` = (halfX, halfY, halfZ)
- **Sphere**: `radius` used; `halfExtents` ignored
- **Capsule**: `radius` + `halfExtents.y` = half-height
- **Mesh**: both ignored (geometry comes from mesh data)

### 3.3 PhysicsBodyCreatedTag

```cpp
struct PhysicsBodyCreatedTag {};
```

A zero-size tag component added by `PhysicsSystem` once a Jolt body has been created for an entity. This lets the system distinguish "needs body creation" (has `RigidBodyComponent` + `ColliderComponent` but no `PhysicsBodyCreatedTag`) from "already registered."

### 3.4 Relationship to TransformComponent and WorldTransformComponent

The existing transform pipeline is:

```
Game code writes TransformComponent (local TRS)
  -> TransformSystem composes WorldTransformComponent (world Mat4)
     -> Render systems read WorldTransformComponent
```

Physics adds a new phase between game code and TransformSystem:

```
PhysicsSystem reads dynamic body transforms from Jolt
  -> writes TransformComponent (position, rotation) for Dynamic bodies
     -> TransformSystem composes WorldTransformComponent
        -> Render systems read WorldTransformComponent
```

For Kinematic bodies, the flow reverses before the physics step:

```
Game code writes TransformComponent
  -> PhysicsSystem reads TransformComponent, pushes to Jolt via MoveKinematic()
     -> Jolt step resolves collisions
```

### 3.5 Ground Truth Ownership

| Body Type | Ground Truth Owner | Direction |
|---|---|---|
| Dynamic | Jolt Physics | Jolt -> TransformComponent -> WorldTransformComponent |
| Static | Scene graph (initial placement) | TransformComponent -> Jolt (once, at creation) |
| Kinematic | Game code | TransformComponent -> Jolt (each frame) |

**Key rule**: For dynamic bodies, `PhysicsSystem` is the only writer of `TransformComponent.position` and `TransformComponent.rotation`. Game code must not write these directly for dynamic bodies -- instead, use `IPhysicsEngine::applyForce()`, `applyImpulse()`, or `setLinearVelocity()`.

## 4. PhysicsSystem

### 4.1 System Scheduling

`PhysicsSystem` declares its read/write sets for the compile-time DAG scheduler:

```cpp
class PhysicsSystem
{
public:
    using Reads = ecs::TypeList<ColliderComponent, scene::HierarchyComponent>;
    using Writes = ecs::TypeList<rendering::TransformComponent, RigidBodyComponent>;

    void update(ecs::Registry& reg, IPhysicsEngine& physics, float deltaTime);
};
```

Execution order within a frame:

1. **Game logic systems** (write TransformComponent for kinematic bodies, apply forces/impulses)
2. **PhysicsSystem::update()** (sync kinematic transforms to Jolt, step simulation, write back dynamic transforms)
3. **TransformSystem::update()** (compose world matrices from local TRS)
4. **Culling and rendering systems** (read WorldTransformComponent)

PhysicsSystem must run before TransformSystem and after game logic. This is enforced by the DAG: PhysicsSystem writes `TransformComponent`, TransformSystem reads it.

### 4.2 Fixed Timestep

Physics uses a fixed timestep with accumulation, following the standard semi-fixed approach:

```cpp
void PhysicsSystem::update(ecs::Registry& reg, IPhysicsEngine& physics, float deltaTime)
{
    constexpr float kFixedDt = 1.0f / 60.0f;
    constexpr int kMaxSubSteps = 4;

    // 1. Register new bodies (entities with RigidBodyComponent + ColliderComponent but no PhysicsBodyCreatedTag)
    registerNewBodies(reg, physics);

    // 2. Sync kinematic body transforms: read TransformComponent, push to Jolt
    syncKinematicBodies(reg, physics);

    // 3. Step physics with fixed timestep
    //    Jolt handles sub-stepping internally via its collision_steps parameter
    physics.step(deltaTime, kMaxSubSteps);

    // 4. Write back: read Jolt transforms for dynamic bodies, write to TransformComponent
    syncDynamicBodies(reg, physics);

    // 5. Remove bodies for destroyed entities
    cleanupDestroyedBodies(reg, physics);
}
```

Jolt's `PhysicsSystem::Update()` accepts a delta time and a `collisionSteps` count. It internally divides the delta into sub-steps. We pass through the frame's actual delta time and let Jolt handle sub-stepping, clamped to `kMaxSubSteps` to prevent spiral-of-death on lag spikes.

### 4.3 Body Registration

When `PhysicsSystem` encounters an entity that has `RigidBodyComponent` + `ColliderComponent` but lacks `PhysicsBodyCreatedTag`:

1. Read `ColliderComponent` to build a `JPH::Shape`.
2. Read `TransformComponent` (or `WorldTransformComponent` if the entity has a parent) for initial position/rotation.
3. Call `IPhysicsEngine::addBody()`, which creates the Jolt body and returns a body ID.
4. Store the body ID in `RigidBodyComponent::bodyID`.
5. Add `PhysicsBodyCreatedTag` to the entity.

### 4.4 Transform Sync Details

**Dynamic body write-back** (`syncDynamicBodies`):

```
For each entity with RigidBodyComponent(type=Dynamic) + PhysicsBodyCreatedTag:
    (position, rotation) = physics.getBodyTransform(bodyID)
    tc.position = position
    tc.rotation = rotation
    // scale is NOT touched -- physics does not affect scale
```

For entities in a hierarchy (has `HierarchyComponent`), the physics position is in world space but `TransformComponent` is local. The system must compute the local transform:

```
worldPos, worldRot = physics.getBodyTransform(bodyID)
parentWorld = registry.get<WorldTransformComponent>(parent).matrix
localPos = inverse(parentWorld) * vec4(worldPos, 1.0)
localRot = inverse(parentRotation) * worldRot
tc.position = localPos
tc.rotation = localRot
```

**Kinematic body push** (`syncKinematicBodies`):

```
For each entity with RigidBodyComponent(type=Kinematic) + PhysicsBodyCreatedTag:
    worldPos = WorldTransformComponent.matrix[3]  // translation column
    worldRot = decompose rotation from WorldTransformComponent.matrix
    physics.moveKinematic(bodyID, worldPos, worldRot, fixedDt)
```

Note: kinematic sync uses `WorldTransformComponent` (computed last frame by TransformSystem) so that hierarchy is accounted for. This introduces one frame of latency for kinematic bodies in hierarchies, which is acceptable and standard practice.

## 5. IPhysicsEngine Interface

```cpp
namespace engine::physics
{

struct RayHit
{
    ecs::EntityID entity = ecs::INVALID_ENTITY;
    math::Vec3 point{0.0f};
    math::Vec3 normal{0.0f};
    float fraction = 0.0f;  // [0,1] along ray
};

struct BodyDesc
{
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    ColliderShape shape = ColliderShape::Box;
    math::Vec3 halfExtents{0.5f};
    float radius = 0.5f;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    BodyType type = BodyType::Dynamic;
    uint8_t layer = 0;
    ecs::EntityID entity = ecs::INVALID_ENTITY;  // back-reference for callbacks
};

class IPhysicsEngine
{
public:
    virtual ~IPhysicsEngine() = default;

    // Lifecycle
    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // Simulation
    virtual void step(float deltaTime, int maxSubSteps = 4) = 0;

    // Body management
    virtual uint32_t addBody(const BodyDesc& desc) = 0;
    virtual void removeBody(uint32_t bodyID) = 0;

    // Transform queries (world-space)
    virtual void getBodyTransform(uint32_t bodyID, math::Vec3& outPos, math::Quat& outRot) const = 0;
    virtual void moveKinematic(uint32_t bodyID, const math::Vec3& targetPos,
                               const math::Quat& targetRot, float deltaTime) = 0;

    // Forces and impulses
    virtual void applyForce(uint32_t bodyID, const math::Vec3& force) = 0;
    virtual void applyImpulse(uint32_t bodyID, const math::Vec3& impulse) = 0;
    virtual void setLinearVelocity(uint32_t bodyID, const math::Vec3& velocity) = 0;
    virtual void setAngularVelocity(uint32_t bodyID, const math::Vec3& velocity) = 0;
    virtual math::Vec3 getLinearVelocity(uint32_t bodyID) const = 0;

    // Raycasting
    virtual bool rayCastClosest(const math::Vec3& origin, const math::Vec3& direction,
                                float maxDistance, RayHit& outHit) const = 0;
    virtual std::vector<RayHit> rayCastAll(const math::Vec3& origin, const math::Vec3& direction,
                                           float maxDistance) const = 0;

    // Collision event polling (filled during step, cleared at next step)
    struct ContactEvent
    {
        ecs::EntityID entityA = ecs::INVALID_ENTITY;
        ecs::EntityID entityB = ecs::INVALID_ENTITY;
        math::Vec3 contactPoint{0.0f};
        math::Vec3 contactNormal{0.0f};
        float penetrationDepth = 0.0f;
    };

    virtual const std::vector<ContactEvent>& getContactBeginEvents() const = 0;
    virtual const std::vector<ContactEvent>& getContactEndEvents() const = 0;
};

}  // namespace engine::physics
```

The interface uses only engine math types (`math::Vec3`, `math::Quat`) and `ecs::EntityID`. No Jolt types appear in any public header.

## 6. Collision Callbacks

### 6.1 Architecture

Jolt reports collisions via `ContactListener`. The `JoltPhysicsEngine` implementation installs a `ContactListener` that:

1. On `OnContactAdded`: appends to an internal `contactBeginEvents_` vector.
2. On `OnContactRemoved`: appends to an internal `contactEndEvents_` vector.
3. On `OnContactPersisted`: optionally tracked (deferred for v1; most games only need begin/end).

Events are populated during `step()` and remain valid until the next `step()` call, at which point they are cleared. Game code reads them after `PhysicsSystem::update()` returns.

### 6.2 Entity Mapping

Each Jolt body stores its `ecs::EntityID` in the body's user data field (`JPH::Body::SetUserData(uint64_t)`). When a contact event fires, the listener retrieves the entity IDs from both bodies' user data and populates `ContactEvent::entityA` and `ContactEvent::entityB`.

### 6.3 Game Code Usage

```cpp
// In game system, after PhysicsSystem has run:
const auto& begins = physics->getContactBeginEvents();
for (const auto& event : begins)
{
    if (event.entityA == playerEntity || event.entityB == playerEntity)
    {
        // handle player collision
    }
}
```

### 6.4 Collision Layers and Filtering

`RigidBodyComponent::layer` maps to Jolt's object layers. The `JoltPhysicsEngine` defines a `BroadPhaseLayerInterface` and `ObjectVsObjectLayerFilter` that encode which layers collide. Initial layer definitions:

| Layer | Index | Collides With |
|---|---|---|
| Static | 0 | Dynamic, Kinematic |
| Dynamic | 1 | Static, Dynamic, Kinematic |
| Kinematic | 2 | Dynamic |
| Trigger | 3 | Dynamic, Kinematic |
| Debris | 4 | Static |

This table is configurable at init time. For v1, a simple hardcoded table suffices.

## 7. Raycasting API

### 7.1 Closest Hit

```cpp
RayHit hit;
if (physics->rayCastClosest(cameraPos, cameraForward, 100.0f, hit))
{
    // hit.entity is the EntityID
    // hit.point is the world-space contact point
    // hit.normal is the surface normal
    // hit.fraction is [0,1] along the ray
}
```

Internally, `JoltPhysicsEngine::rayCastClosest` constructs a `JPH::RRayCast` and calls `JPH::PhysicsSystem::GetNarrowPhaseQuery().CastRay()` with a `ClosestHitCollisionCollector`.

### 7.2 All Hits

```cpp
auto hits = physics->rayCastAll(origin, direction, maxDist);
// sorted by fraction (nearest first)
```

Uses `AllHitCollisionCollector`, then sorts results by `fraction`.

### 7.3 Use Cases

- **Picking**: cast from camera through mouse position into scene.
- **Ground detection**: cast downward from character feet.
- **Line of sight**: cast between two points, check if any hit fraction < 1.0.
- **Audio occlusion**: cast between sound source and listener (documented in NOTES.md).

## 8. Debug Visualization

Deferred to a later milestone. When implemented:

- `PhysicsDebugRenderer` implements Jolt's `JPH::DebugRenderer` interface.
- Draws collision shapes as wireframes using bgfx debug draw lines.
- Toggled via an ImGui checkbox in the debug panel.
- Renders after the main scene pass, before post-processing.
- Color-coded: green = static, blue = dynamic (awake), gray = dynamic (sleeping), yellow = kinematic, red = trigger.

For v1, collision shapes can be verified via unit tests and raycasting. Debug drawing is a quality-of-life feature, not a blocker.

## 9. File Layout

```
engine/physics/
    IPhysicsEngine.h           // abstract interface + RayHit, BodyDesc, ContactEvent structs
    PhysicsComponents.h        // RigidBodyComponent, ColliderComponent, PhysicsBodyCreatedTag
    PhysicsSystem.h            // PhysicsSystem class declaration
    PhysicsSystem.cpp          // update loop: register, sync, step, write-back
    PhysicsConversions.h       // inline glm<->Jolt conversion helpers (header-only)
    JoltPhysicsEngine.h        // concrete Jolt implementation of IPhysicsEngine
    JoltPhysicsEngine.cpp      // Jolt init/shutdown/step/raycast/body management
    JoltContactListener.h      // Jolt ContactListener implementation (internal)
    JoltContactListener.cpp
    JoltLayerConfig.h          // BroadPhaseLayerInterface, ObjectLayerPairFilter (internal)
    JoltLayerConfig.cpp
tests/
    physics/
        TestPhysicsComponents.cpp   // component sizes, defaults
        TestPhysicsSystem.cpp       // integration: bodies created, transforms synced
        TestRaycasting.cpp          // ray queries against known geometry
        TestCollisionEvents.cpp     // contact begin/end events
        TestKinematicSync.cpp       // kinematic body follows TransformComponent
```

### 9.1 PhysicsConversions.h

Inline conversion functions, used only inside `engine/physics/*.cpp`:

```cpp
#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include "engine/math/Types.h"

namespace engine::physics
{

inline JPH::Vec3 toJolt(const math::Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::Quat toJolt(const math::Quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }

inline math::Vec3 fromJolt(const JPH::Vec3& v) { return math::Vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline math::Quat fromJolt(const JPH::Quat& q) { return math::Quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

}
```

Note: `glm::quat` constructor order is `(w, x, y, z)` while `JPH::Quat` constructor order is `(x, y, z, w)`. These conversion functions handle this difference.

## 10. Integration with Scene Graph

### 10.1 Dynamic Body Moved by Physics

When Jolt moves a dynamic body during `step()`:

1. `PhysicsSystem::syncDynamicBodies()` calls `physics.getBodyTransform(bodyID, pos, rot)`.
2. If the entity has no `HierarchyComponent` (is a root): write `pos` and `rot` directly to `TransformComponent`.
3. If the entity has a `HierarchyComponent` (is a child): compute local position/rotation by inverse-transforming through the parent's `WorldTransformComponent` matrix. Write the resulting local TRS to `TransformComponent`.
4. `TransformSystem::update()` then composes the correct `WorldTransformComponent` for this entity and its children.

### 10.2 Game Code Moves a Kinematic Body

When game code updates `TransformComponent` for a kinematic entity:

1. Game code writes `TransformComponent.position` and/or `TransformComponent.rotation`.
2. `PhysicsSystem::syncKinematicBodies()` reads the entity's `WorldTransformComponent` (from the previous frame) and calls `physics.moveKinematic(bodyID, worldPos, worldRot, dt)`.
3. Jolt interpolates the kinematic body to the target position during `step()`, properly pushing dynamic bodies out of the way.

One-frame latency is inherent because `WorldTransformComponent` is computed after `PhysicsSystem` runs. For v1 this is acceptable. If needed, `PhysicsSystem` could compose the world matrix inline for kinematic bodies to eliminate the latency at the cost of duplicating the transform composition logic.

### 10.3 Entity Destruction

When `scene::destroyHierarchy()` destroys an entity that has a `RigidBodyComponent`:

1. `PhysicsSystem::cleanupDestroyedBodies()` detects orphaned body IDs (bodies whose entity is no longer valid in the Registry).
2. Calls `physics.removeBody(bodyID)` to remove from Jolt.

Implementation: `JoltPhysicsEngine` maintains a `std::unordered_map<uint32_t, ecs::EntityID>` mapping body IDs to entities. During cleanup, iterate this map and check `registry.isValid(entityID)`.

### 10.4 Re-parenting

When an entity with a dynamic body is re-parented via `scene::setParent()`:

- The physics body's world-space position does not change (Jolt does not know about hierarchy).
- `TransformComponent` must be recomputed to be local relative to the new parent. This is the responsibility of `setParent()` or game code, not the physics system.
- On the next frame, `PhysicsSystem` writes back Jolt's world-space position, which gets correctly inverse-transformed through the new parent.

## 11. Test Plan

### 11.1 TestPhysicsComponents.cpp

- `sizeof(RigidBodyComponent)` matches documented size (28 bytes).
- `sizeof(ColliderComponent)` matches documented size (32 bytes).
- Default values are correct (mass=1, type=Dynamic, shape=Box).

### 11.2 TestPhysicsSystem.cpp

- Entity with `RigidBodyComponent` + `ColliderComponent` gets `PhysicsBodyCreatedTag` after first update.
- `RigidBodyComponent::bodyID` is set to a valid (non-~0u) value after registration.
- Dynamic body under gravity: after N steps, `TransformComponent.position.y` has decreased.
- Static body does not move after N steps.
- Two dynamic bodies colliding: positions are resolved (no overlap).
- Entity destroyed: body is removed from Jolt (verified by body count query).

### 11.3 TestRaycasting.cpp

- Ray hits a box collider: correct entity, point, normal, fraction.
- Ray misses: returns false.
- `rayCastAll` returns multiple hits sorted by fraction.
- Ray does not hit bodies on filtered layers.

### 11.4 TestCollisionEvents.cpp

- Two dynamic bodies dropped onto each other produce a `contactBegin` event.
- After separation, a `contactEnd` event is produced.
- `ContactEvent` contains correct entity IDs for both bodies.
- Events are cleared at the start of the next `step()`.

### 11.5 TestKinematicSync.cpp

- Kinematic body tracks `TransformComponent` changes: after writing a new position and stepping, Jolt's body is at the new position.
- Kinematic body pushes a dynamic body: the dynamic body moves away.
- Kinematic body in a hierarchy: world-space position reflects the composed hierarchy transform.

### 11.6 Build and CMake

All physics tests are added to `engine_tests`:

```cmake
# In CMakeLists.txt, added to engine_tests sources:
tests/physics/TestPhysicsComponents.cpp
tests/physics/TestPhysicsSystem.cpp
tests/physics/TestRaycasting.cpp
tests/physics/TestCollisionEvents.cpp
tests/physics/TestKinematicSync.cpp
```

`engine_tests` links `engine_physics`:

```cmake
target_link_libraries(engine_tests PRIVATE ... engine_physics ...)
```

## 12. Implementation Sequence

| Phase | Work | Depends On |
|---|---|---|
| 1 | Add Jolt to CMake, create `engine_physics` target, build verification | Nothing |
| 2 | `IPhysicsEngine.h`, `PhysicsComponents.h`, `PhysicsConversions.h` | Phase 1 |
| 3 | `JoltPhysicsEngine` init/shutdown/addBody/removeBody/step | Phase 2 |
| 4 | `PhysicsSystem` body registration + dynamic body write-back | Phase 3 |
| 5 | Kinematic sync, hierarchy-aware local transform computation | Phase 4 |
| 6 | Raycasting | Phase 3 |
| 7 | Collision callbacks | Phase 3 |
| 8 | Tests for all above | Phases 4-7 |
| 9 | Debug visualization (deferred) | Phase 8 + bgfx debug draw |

## 13. Open Questions

1. **Compound shapes**: Should `ColliderComponent` support compound shapes (multiple colliders per entity), or should compound bodies be modeled as child entities each with their own collider? Recommendation: use child entities for v1, add compound shape support later if perf requires it.

2. **Triggers / sensors**: Bodies that detect overlap but do not produce physical response. The `Trigger` layer handles this at the broad-phase level, but Jolt also supports per-body sensor flags. Decide during implementation.

3. **Sleep/wake management**: Jolt handles sleep automatically. Expose `IPhysicsEngine::setActive(bodyID, bool)` if game code needs explicit control.

4. **Mesh collider source**: For `ColliderShape::Mesh`, the triangle data comes from the render mesh via `MeshComponent`. This couples physics to the render mesh format. Consider a separate `PhysicsMeshComponent` if physics needs simplified collision geometry.

---

### Critical Files for Implementation

- `/Users/shayanj/claude/engine/CMakeLists.txt` -- add Jolt FetchContent, `engine_physics` target, and test sources
- `/Users/shayanj/claude/engine/engine/rendering/EcsComponents.h` -- reference for TransformComponent and WorldTransformComponent layouts that physics must read/write
- `/Users/shayanj/claude/engine/engine/scene/TransformSystem.cpp` -- the transform composition logic that runs after PhysicsSystem; physics write-back must produce compatible local TRS values
- `/Users/shayanj/claude/engine/engine/scene/SceneGraph.h` -- hierarchy mutation API; physics must handle entity destruction and re-parenting correctly
- `/Users/shayanj/claude/engine/engine/ecs/Registry.h` -- ECS API used by PhysicsSystem to query/write components, create tags, and iterate views