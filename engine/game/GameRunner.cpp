#include "engine/game/GameRunner.h"

#include <algorithm>

#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/game/IGame.h"
#include "engine/scene/TransformSystem.h"

namespace engine::game
{

GameRunner::GameRunner(IGame& game) : game_(game) {}

GameRunner::~GameRunner() = default;

int GameRunner::run(const core::EngineDesc& desc)
{
    core::Engine engine;
    if (!engine.init(desc))
        return 1;

    ecs::Registry registry;
    game_.onInit(engine, registry);

    scene::TransformSystem transformSys;

    float accumulator = 0.0f;
    float dt = 0.0f;

    while (engine.beginFrame(dt))
    {
        if (engine.fbWidth() == 0 || engine.fbHeight() == 0)
        {
            engine.endFrame();
            continue;
        }

        // --- Fixed update (physics rate) ---
        accumulator += dt;
        if (accumulator > maxAccumulator_)
            accumulator = maxAccumulator_;

        while (accumulator >= fixedTimestep_)
        {
            game_.onFixedUpdate(engine, registry, fixedTimestep_);
            accumulator -= fixedTimestep_;
        }

        // --- Variable update (render rate) ---
        game_.onUpdate(engine, registry, dt);

        // --- Engine systems ---
        transformSys.update(registry);

        // --- Render ---
        game_.onRender(engine);

        engine.endFrame();
    }

    game_.onShutdown(engine, registry);
    engine.shutdown();
    return 0;
}

}  // namespace engine::game
