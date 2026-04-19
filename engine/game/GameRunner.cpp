#include "engine/game/GameRunner.h"

#include <algorithm>

#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/game/IGame.h"
#include "engine/game/ProjectConfig.h"
#include "engine/scene/TransformSystem.h"

namespace engine::game
{

GameRunner::GameRunner(IGame& game) : game_(game) {}

GameRunner::~GameRunner() = default;

// ---------------------------------------------------------------------------
// Shared frame loop — identical on desktop and Android.
// ---------------------------------------------------------------------------

int GameRunner::runLoop(core::Engine& engine)
{
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

// ===========================================================================
// Desktop implementation
// ===========================================================================

#ifndef __ANDROID__

int GameRunner::run(const core::EngineDesc& desc)
{
    core::Engine engine;
    if (!engine.init(desc))
        return 1;

    return runLoop(engine);
}

int GameRunner::run(const char* configPath)
{
    ProjectConfig config;
    if (configPath)
    {
        config.loadFromFile(configPath);
    }

    // Apply physics config to runner.
    fixedTimestep_ = config.physics.fixedTimestep;

    return run(config.toEngineDesc());
}

#else  // __ANDROID__

// ===========================================================================
// Android implementation
// ===========================================================================

int GameRunner::runAndroid(struct android_app* app, const core::EngineDesc& desc)
{
    core::Engine engine;
    if (!engine.initAndroid(app, desc))
        return 1;

    return runLoop(engine);
}

int GameRunner::runAndroid(struct android_app* app, const char* configPath)
{
    ProjectConfig config;
    if (configPath)
    {
        config.loadFromFile(configPath);
    }

    fixedTimestep_ = config.physics.fixedTimestep;

    return runAndroid(app, config.toEngineDesc());
}

#endif  // __ANDROID__

}  // namespace engine::game
