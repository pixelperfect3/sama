#pragma once

#include "engine/ecs/Entity.h"
#include "engine/memory/InlinedVector.h"

namespace engine::scene
{

struct HierarchyComponent
{
    ecs::EntityID parent = ecs::INVALID_ENTITY;
};
static_assert(sizeof(HierarchyComponent) == 8);

struct ChildrenComponent
{
    memory::InlinedVector<ecs::EntityID, 8> children;
};

}  // namespace engine::scene
