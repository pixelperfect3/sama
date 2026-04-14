#pragma once

#include "engine/animation/AnimationResources.h"
#include "engine/ecs/Registry.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// AnimStateMachineSystem -- evaluates state machine transitions and drives
// AnimatorComponent accordingly.
//
// Call BEFORE AnimationSystem::update() each frame.
// ---------------------------------------------------------------------------

class AnimStateMachineSystem
{
public:
    // Evaluate transitions and drive AnimatorComponent.
    void update(ecs::Registry& reg, float dt, const AnimationResources& animRes);
};

}  // namespace engine::animation
