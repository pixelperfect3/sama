#pragma once

#include "engine/ecs/Registry.h"

namespace engine::scene
{

class TransformSystem
{
public:
    // Walk the hierarchy top-down, composing local TRS into world matrices.
    // Must be called once per frame, before culling/rendering.
    void update(ecs::Registry& reg);
};

}  // namespace engine::scene
