#pragma once

#include <memory_resource>

#include "engine/animation/AnimationResources.h"
#include "engine/ecs/Registry.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// IkSystem -- per-frame IK post-process on FK poses.
//
// Reads:  SkeletonComponent, IkChainsComponent, IkTargetsComponent, PoseComponent
// Writes: PoseComponent (modifies joint rotations in-place)
//
// Must run AFTER AnimationSystem::updatePoses() and BEFORE
// AnimationSystem::computeBoneMatrices().
// ---------------------------------------------------------------------------

class IkSystem
{
public:
    void update(ecs::Registry& reg, const AnimationResources& animRes,
                std::pmr::memory_resource* arena);
};

}  // namespace engine::animation
