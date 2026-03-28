#pragma once

#include <vector>

#include "engine/ecs/Entity.h"

namespace engine::scene
{

struct HierarchyComponent
{
    ecs::EntityID parent = ecs::INVALID_ENTITY;
};
static_assert(sizeof(HierarchyComponent) == 8);

struct ChildrenComponent
{
    std::vector<ecs::EntityID> children;
};

}  // namespace engine::scene
