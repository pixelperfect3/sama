// Minimal binary that links against sama_minimal.
// Used only to measure the binary size footprint of the umbrella target.
#include <cstdio>

#include "engine/ecs/Registry.h"

int main()
{
    engine::ecs::Registry reg;
    auto e = reg.createEntity();
    std::printf("sama_minimal size test: entity=%u\n", static_cast<unsigned>(e));
    return 0;
}
