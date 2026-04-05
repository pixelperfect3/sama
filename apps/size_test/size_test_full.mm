// Minimal binary that links against sama (full umbrella).
// Used only to measure the binary size footprint of the umbrella target.
#include <cstdio>

#include "engine/animation/AnimationSystem.h"
#include "engine/ecs/Registry.h"
#include "engine/input/InputState.h"
#include "engine/io/Json.h"
#include "engine/threading/ThreadPool.h"

int main()
{
    engine::ecs::Registry reg;
    engine::animation::AnimationSystem anim;
    engine::threading::ThreadPool pool(2);
    auto e = reg.createEntity();
    std::printf("sama full size test: entity=%u bones=%u\n", static_cast<unsigned>(e),
                anim.boneBufferSize());
    return 0;
}
