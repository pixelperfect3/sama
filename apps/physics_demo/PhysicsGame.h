#pragma once

#include <random>

#include "engine/core/OrbitCamera.h"
#include "engine/game/IGame.h"
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsSystem.h"
#include "engine/rendering/IblResources.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"

namespace engine::ecs
{
using EntityID = uint64_t;
}

class PhysicsGame : public engine::game::IGame
{
public:
    void onInit(engine::core::Engine& engine, engine::ecs::Registry& registry) override;
    void onFixedUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry,
                       float fixedDt) override;
    void onUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry, float dt) override;
    void onRender(engine::core::Engine& engine) override;
    void onShutdown(engine::core::Engine& engine, engine::ecs::Registry& registry) override;

private:
    void resetCubes(engine::ecs::Registry& registry);
    void respawnFallenCubes(engine::ecs::Registry& registry);

    engine::physics::JoltPhysicsEngine physics_;
    engine::physics::PhysicsSystem physicsSys_;
    engine::rendering::DrawCallBuildSystem drawCallSys_;
    engine::rendering::IblResources ibl_;
    engine::core::OrbitCamera cam_;

    // Entity IDs
    static constexpr int kCubeCount = 17;
    engine::ecs::EntityID groundEntity_ = 0;
    engine::ecs::EntityID cubeEntities_[kCubeCount] = {};
    engine::ecs::EntityID lightIndicator_ = 0;

    // Ground plane tilt state
    float planePitch_ = 0.0f;
    float planeRoll_ = 0.0f;

    // Camera interaction
    bool rightDragging_ = false;
    double prevMouseX_ = 0.0;
    double prevMouseY_ = 0.0;

    // Rendering
    float renderMs_ = 0.0f;

    // Registry pointer (set during onInit, valid for the runner's lifetime)
    engine::ecs::Registry* registry_ = nullptr;

    // Random
    std::mt19937 rng_{42};
    float randomFloat(float lo, float hi);
    glm::vec3 randomSpawnPos();
};
