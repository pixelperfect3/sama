#pragma once

#include "engine/ecs/Registry.h"
#include "engine/physics/IPhysicsEngine.h"

namespace engine::physics
{

class PhysicsSystem
{
public:
    void update(ecs::Registry& reg, IPhysicsEngine& physics, float deltaTime);

private:
    void registerNewBodies(ecs::Registry& reg, IPhysicsEngine& physics);
    void syncKinematicBodies(ecs::Registry& reg, IPhysicsEngine& physics, float deltaTime);
    void syncDynamicBodies(ecs::Registry& reg, IPhysicsEngine& physics);
    void cleanupDestroyedBodies(ecs::Registry& reg, IPhysicsEngine& physics);
};

}  // namespace engine::physics
