#include "engine/game/GameRunner.h"

#include <algorithm>

#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/game/IGame.h"
#include "engine/game/ProjectConfig.h"
#include "engine/scene/TransformSystem.h"

#if defined(__APPLE__) && TARGET_OS_IPHONE
#include <memory>
#endif

namespace engine::game
{

#if defined(__APPLE__) && TARGET_OS_IPHONE
// Persistent state for iOS — UIKit drives the frame loop, so the Engine and
// the per-frame book-keeping must outlive runIos() rather than living on its
// stack the way runLoop() does on desktop / Android.
struct GameRunner::IosState
{
    core::Engine engine;
    ecs::Registry registry;
    scene::TransformSystem transformSys;
    float accumulator = 0.0f;
    bool gameInitialised = false;
    bool aborted = false;
};
#endif

GameRunner::GameRunner(IGame& game) : game_(game) {}

GameRunner::~GameRunner()
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    shutdownIos();
#endif
}

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

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)

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

#elif defined(__ANDROID__)  // __ANDROID__

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

#else  // iOS

// ===========================================================================
// iOS implementation
//
// UIKit owns the run loop, so we cannot block in runIos() the way Android
// does.  Instead the lifecycle splits across three calls invoked from the
// IosApp delegate:
//
//   runIos()      — once at didFinishLaunching: bring up the Engine + game.
//   tickIos()     — per CADisplayLink callback: one logical frame.
//   shutdownIos() — once at applicationWillTerminate: tear everything down.
//
// State is held in an IosState struct on the heap (allocated in runIos,
// freed in shutdownIos) so the Engine outlives a single function call.
// ===========================================================================

int GameRunner::runIos(platform::ios::IosWindow* window, platform::ios::IosTouchInput* touch,
                       platform::ios::IosGyro* gyro, platform::ios::IosFileSystem* fs,
                       const core::EngineDesc& desc)
{
    if (iosState_)
    {
        // runIos was already called — drop the previous state.  Defensive;
        // not expected in normal IosApp flow.
        shutdownIos();
    }

    iosState_ = new IosState();
    if (!iosState_->engine.initIos(window, touch, gyro, fs, desc))
    {
        iosState_->aborted = true;
        return 1;
    }

    game_.onInit(iosState_->engine, iosState_->registry);
    iosState_->gameInitialised = true;
    iosState_->accumulator = 0.0f;
    return 0;
}

bool GameRunner::tickIos()
{
    if (!iosState_ || iosState_->aborted)
        return false;

    float dt = 0.0f;
    if (!iosState_->engine.beginFrame(dt))
    {
        // Engine signalled exit — propagate to the IosApp delegate so it
        // stops the display link.
        return false;
    }

    if (iosState_->engine.fbWidth() == 0 || iosState_->engine.fbHeight() == 0)
    {
        iosState_->engine.endFrame();
        return true;
    }

    // Fixed timestep accumulator (mirror desktop / Android).
    iosState_->accumulator += dt;
    if (iosState_->accumulator > maxAccumulator_)
        iosState_->accumulator = maxAccumulator_;
    while (iosState_->accumulator >= fixedTimestep_)
    {
        game_.onFixedUpdate(iosState_->engine, iosState_->registry, fixedTimestep_);
        iosState_->accumulator -= fixedTimestep_;
    }

    game_.onUpdate(iosState_->engine, iosState_->registry, dt);
    iosState_->transformSys.update(iosState_->registry);
    game_.onRender(iosState_->engine);

    iosState_->engine.endFrame();
    return true;
}

void GameRunner::shutdownIos()
{
    if (!iosState_)
        return;

    if (iosState_->gameInitialised)
    {
        game_.onShutdown(iosState_->engine, iosState_->registry);
        iosState_->gameInitialised = false;
    }
    iosState_->engine.shutdown();

    delete iosState_;
    iosState_ = nullptr;
}

#endif  // platform branch

}  // namespace engine::game
