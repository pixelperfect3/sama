# Scene Graph Architecture

## Overview

The scene graph encodes parent-child hierarchy directly in ECS components — no separate tree object. A `TransformSystem` walks the hierarchy each frame, composing local TRS into cached world matrices that all downstream render systems read.

## Components

### HierarchyComponent (8 bytes)

```cpp
struct HierarchyComponent
{
    EntityID parent = INVALID_ENTITY;  // 8 bytes
};
```

Stored on every entity that has a parent. Root entities do NOT have this component — absence means "I am a root." This makes root queries trivial: iterate `TransformComponent` entities that lack `HierarchyComponent`.

### ChildrenComponent (24 bytes, heap-allocated list)

```cpp
struct ChildrenComponent
{
    std::vector<EntityID> children;  // typically 24 bytes (ptr + size + cap)
};
```

Stored on every entity that has at least one child. The vector is sorted by EntityID for deterministic traversal. When the last child is removed, the component is removed from the entity.

### TransformComponent (existing, 44 bytes)

Already defined in `EcsComponents.h`. Local position/rotation/scale with a dirty flag. Game code writes this; `TransformSystem` reads it.

### WorldTransformComponent (existing, 64 bytes)

Already defined. Cached world matrix. Written by `TransformSystem`, read by all render systems.

## TransformSystem

Runs once per frame, before culling/rendering. Reads `TransformComponent` + `HierarchyComponent` + `ChildrenComponent`, writes `WorldTransformComponent`.

### Algorithm

1. **Find roots**: iterate all entities with `TransformComponent` but without `HierarchyComponent`. These are the hierarchy roots.
2. **Top-down traversal**: for each root, recursively visit children via `ChildrenComponent`:
   ```
   updateNode(entity, parentWorldMatrix):
       local = composeMatrix(transform.position, transform.rotation, transform.scale)
       world = parentWorldMatrix * local
       worldTransform.matrix = world
       for child in children:
           updateNode(child, world)
   ```
3. **Dirty propagation** (optimization, deferred): if `TransformComponent::flags & dirty == 0` and no ancestor is dirty, skip recomputation. For v1, recompute every frame unconditionally — 50k mat4 multiplications is <1ms on modern hardware.

### Standalone entities

Entities with `TransformComponent` but no `HierarchyComponent` and no `ChildrenComponent` are standalone — their world matrix is simply `compose(position, rotation, scale)`. No parent lookup needed.

## Hierarchy Mutation API

Free functions in `engine/scene/SceneGraph.h` that operate on a `Registry`:

```cpp
namespace engine::scene
{
    // Set parent of child to newParent. Removes from old parent if any.
    // newParent = INVALID_ENTITY detaches (makes root).
    void setParent(ecs::Registry& reg, ecs::EntityID child, ecs::EntityID newParent);

    // Detach child from its parent (becomes a root).
    void detach(ecs::Registry& reg, ecs::EntityID child);

    // Destroy entity and all descendants recursively.
    void destroyHierarchy(ecs::Registry& reg, ecs::EntityID root);

    // Query: get parent, children, or root of an entity.
    ecs::EntityID getParent(const ecs::Registry& reg, ecs::EntityID entity);
    const std::vector<ecs::EntityID>* getChildren(const ecs::Registry& reg, ecs::EntityID entity);
    bool isAncestor(const ecs::Registry& reg, ecs::EntityID ancestor, ecs::EntityID descendant);
}
```

### setParent rules

1. Validate both entities are alive.
2. Prevent cycles: walk from `newParent` up to root; if `child` is encountered, reject.
3. Remove `child` from old parent's `ChildrenComponent` (if any).
4. If old parent's children list becomes empty, remove `ChildrenComponent` from old parent.
5. Add `HierarchyComponent{newParent}` to child (or update existing).
6. Add child to newParent's `ChildrenComponent` (create if absent).
7. If `newParent == INVALID_ENTITY`, remove `HierarchyComponent` from child (becomes root).

## Integration with GltfSceneSpawner

Update `GltfSceneSpawner::spawnNode` to:
1. Create entities with `TransformComponent` (local TRS from glTF node) instead of baked `WorldTransformComponent`.
2. Call `setParent(reg, childEntity, parentEntity)` to establish hierarchy.
3. Let `TransformSystem` compute `WorldTransformComponent` on the next frame.

The spawner currently bakes `parentWorld * node.localTransform` — after this change, the local transform is stored and the system composes it.

## File Layout

```
engine/scene/
    SceneGraph.h         // setParent, detach, destroyHierarchy, queries
    SceneGraph.cpp
    HierarchyComponents.h // HierarchyComponent, ChildrenComponent
    TransformSystem.h
    TransformSystem.cpp
tests/
    scene/
        TestSceneGraph.cpp      // hierarchy mutation tests
        TestTransformSystem.cpp // world matrix computation tests
```

## Test Plan

### TestSceneGraph.cpp — hierarchy mutation

- `setParent` creates HierarchyComponent + ChildrenComponent
- `setParent` to INVALID_ENTITY detaches (becomes root)
- `setParent` re-parent moves child from old parent to new
- `setParent` rejects cycle (child cannot become its own ancestor)
- `detach` removes HierarchyComponent, cleans up parent's ChildrenComponent
- `destroyHierarchy` destroys entity + all descendants
- `destroyHierarchy` on leaf entity destroys only that entity
- `getParent` returns INVALID_ENTITY for root entities
- `getChildren` returns nullptr for leaf entities
- `isAncestor` walks up the chain correctly

### TestTransformSystem.cpp — world matrix computation

- Root entity: world = compose(local TRS)
- Child entity: world = parent_world * compose(child local TRS)
- Three-level hierarchy: grandchild gets correct composed matrix
- Standalone entity (TransformComponent, no hierarchy): world = compose(local)
- Entity with WorldTransformComponent but no TransformComponent: left unchanged
- After re-parenting, next TransformSystem update produces correct world matrix
- Scale/rotation/translation all compose correctly through hierarchy

## Performance Notes

- **50k nodes, flat recompute**: ~0.5ms (50k mat4 muls). Acceptable for v1.
- **Dirty flag optimization**: deferred to when profiling shows need. Adds complexity (dirty propagation down the tree) for marginal gain at current scale.
- **ChildrenComponent uses vector**: small overhead for insert/remove (linear scan). At typical child counts (1-10), this is faster than a set. For entities with 100+ children, consider sorted insert.
