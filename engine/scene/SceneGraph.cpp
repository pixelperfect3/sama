#include "engine/scene/SceneGraph.h"

#include <algorithm>

#include "engine/ecs/Registry.h"
#include "engine/scene/HierarchyComponents.h"

namespace engine::scene
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Remove `child` from `parent`'s ChildrenComponent.  If the children list
// becomes empty the component is removed entirely.
static void removeFromParentChildren(ecs::Registry& reg, ecs::EntityID parent, ecs::EntityID child)
{
    auto* cc = reg.get<ChildrenComponent>(parent);
    if (!cc)
        return;

    auto& vec = cc->children;
    auto it = std::find(vec.begin(), vec.end(), child);
    if (it != vec.end())
    {
        vec.erase(it);
    }

    if (vec.empty())
    {
        reg.remove<ChildrenComponent>(parent);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool setParent(ecs::Registry& reg, ecs::EntityID child, ecs::EntityID newParent)
{
    // Detach case.
    if (newParent == ecs::INVALID_ENTITY)
    {
        detach(reg, child);
        return true;
    }

    // Validate both entities.
    if (!reg.isValid(child) || !reg.isValid(newParent))
        return false;

    // Cannot parent to self.
    if (child == newParent)
        return false;

    // Cycle detection: walk from newParent up -- if we hit child, reject.
    if (isAncestor(reg, child, newParent))
        return false;

    // Remove from old parent if any.
    auto* hc = reg.get<HierarchyComponent>(child);
    if (hc)
    {
        if (hc->parent == newParent)
            return true;  // already parented there
        removeFromParentChildren(reg, hc->parent, child);
        hc->parent = newParent;
    }
    else
    {
        reg.emplace<HierarchyComponent>(child, newParent);
    }

    // Add to new parent's ChildrenComponent.
    auto* parentCC = reg.get<ChildrenComponent>(newParent);
    if (parentCC)
    {
        parentCC->children.push_back(child);
    }
    else
    {
        ChildrenComponent cc;
        cc.children.push_back(child);
        reg.emplace<ChildrenComponent>(newParent, std::move(cc));
    }

    return true;
}

void detach(ecs::Registry& reg, ecs::EntityID child)
{
    auto* hc = reg.get<HierarchyComponent>(child);
    if (!hc)
        return;

    ecs::EntityID parent = hc->parent;
    if (parent != ecs::INVALID_ENTITY)
    {
        removeFromParentChildren(reg, parent, child);
    }

    reg.remove<HierarchyComponent>(child);
}

void destroyHierarchy(ecs::Registry& reg, ecs::EntityID root)
{
    // Depth-first: destroy children before the root.
    auto* cc = reg.get<ChildrenComponent>(root);
    if (cc)
    {
        // Copy the list because destroying children mutates it.
        auto childrenCopy = cc->children;
        for (ecs::EntityID child : childrenCopy)
        {
            destroyHierarchy(reg, child);
        }
    }

    // Detach from own parent before destroying.
    detach(reg, root);

    reg.destroyEntity(root);
}

ecs::EntityID getParent(const ecs::Registry& reg, ecs::EntityID entity)
{
    const auto* hc = reg.get<HierarchyComponent>(entity);
    if (!hc)
        return ecs::INVALID_ENTITY;
    return hc->parent;
}

const memory::InlinedVector<ecs::EntityID, 8>* getChildren(const ecs::Registry& reg,
                                                           ecs::EntityID entity)
{
    const auto* cc = reg.get<ChildrenComponent>(entity);
    if (!cc)
        return nullptr;
    return &cc->children;
}

bool isAncestor(const ecs::Registry& reg, ecs::EntityID ancestor, ecs::EntityID descendant)
{
    constexpr int kMaxDepth = 1024;
    ecs::EntityID current = getParent(reg, descendant);
    for (int depth = 0; current != ecs::INVALID_ENTITY && depth < kMaxDepth; ++depth)
    {
        if (current == ancestor)
            return true;
        current = getParent(reg, current);
    }
    return false;
}

}  // namespace engine::scene
