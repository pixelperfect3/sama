#pragma once

#include "engine/ecs/Entity.h"
#include "engine/memory/InlinedVector.h"

namespace engine::ecs
{
class Registry;
}

namespace engine::scene
{

// Set the parent of `child` to `newParent`.  If `newParent` is INVALID_ENTITY,
// the child is detached (becomes a root).  Performs cycle detection: returns
// false if `newParent` is a descendant of `child` (which would create a cycle).
bool setParent(ecs::Registry& reg, ecs::EntityID child, ecs::EntityID newParent);

// Remove `child` from its current parent, making it a root entity.
void detach(ecs::Registry& reg, ecs::EntityID child);

// Recursively destroy `root` and all of its descendants (depth-first).
void destroyHierarchy(ecs::Registry& reg, ecs::EntityID root);

// Return the parent of `entity`, or INVALID_ENTITY if it has none.
[[nodiscard]] ecs::EntityID getParent(const ecs::Registry& reg, ecs::EntityID entity);

// Return a pointer to the children list, or nullptr if the entity has no
// ChildrenComponent.
[[nodiscard]] const memory::InlinedVector<ecs::EntityID, 8>* getChildren(const ecs::Registry& reg,
                                                                         ecs::EntityID entity);

// Return true if `ancestor` is an ancestor of `descendant` (walks up the
// hierarchy via getParent).
[[nodiscard]] bool isAncestor(const ecs::Registry& reg, ecs::EntityID ancestor,
                              ecs::EntityID descendant);

}  // namespace engine::scene
