// Minimal binary that links against sama_3d.
// Used only to measure the binary size footprint of the umbrella target.
#include <cstdio>

#include "engine/animation/AnimationSystem.h"
#include "engine/assets/AssetManager.h"
#include "engine/audio/IAudioEngine.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/IPhysicsEngine.h"

int main()
{
    engine::ecs::Registry reg;
    engine::animation::AnimationSystem anim;
    auto e = reg.createEntity();
    std::printf("sama_3d size test: entity=%u bones=%u\n", static_cast<unsigned>(e),
                anim.boneBufferSize());
    return 0;
}
