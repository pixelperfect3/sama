#pragma once

#if defined(__APPLE__)
// TargetConditionals is Apple-only.  Guarded so the Android NDK build (no
// such header in Bionic) keeps working.
#include <TargetConditionals.h>
#endif

#include <cstdint>

#ifdef __ANDROID__
struct android_app;
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
namespace engine::platform::ios
{
class IosWindow;
class IosTouchInput;
class IosGyro;
class IosFileSystem;
}  // namespace engine::platform::ios
#endif

namespace engine::core
{
class Engine;
struct EngineDesc;
}  // namespace engine::core

namespace engine::game
{

class IGame;

// ---------------------------------------------------------------------------
// GameRunner -- owns the frame loop and calls IGame at the right points.
//
// Replaces the hand-rolled while(eng.beginFrame(dt)) pattern in each demo.
// The game must outlive the runner (caller owns the IGame on the stack).
//
// On Android, use runAndroid() instead of run().  The frame loop is identical;
// only the Engine initialization differs (ANativeWindow instead of GLFW).
// ---------------------------------------------------------------------------

class GameRunner
{
public:
    explicit GameRunner(IGame& game);
    ~GameRunner();

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    // Run the full lifecycle: init -> loop -> shutdown.
    // Returns the process exit code (0 on clean exit).
    int run(const core::EngineDesc& desc);

    // Run using a ProjectConfig JSON file for configuration.
    // If configPath is null or the file is missing, uses defaults.
    int run(const char* configPath = nullptr);
#elif defined(__ANDROID__)
    // Run on Android: init -> loop -> shutdown using ANativeWindow.
    // Returns the process exit code (0 on clean exit).
    int runAndroid(struct android_app* app, const core::EngineDesc& desc);

    // Run on Android using a ProjectConfig for configuration.
    // If configPath is null, uses defaults.
    int runAndroid(struct android_app* app, const char* configPath = nullptr);
#else  // iOS
    // Run on iOS: init -> hand control to UIKit's run loop.
    //
    // Unlike runAndroid (which spins a poll-loop until the OS terminates the
    // process), this method returns once the engine + game are initialised.
    // The CADisplayLink-driven per-frame tick is invoked through tickIos()
    // and shutdownIos() below; the IosApp delegate drives them from
    // onFrame: / applicationWillTerminate: respectively.
    //
    // Returns 0 on successful init; non-zero on failure (no Metal layer,
    // shader load failure, etc.).
    int runIos(platform::ios::IosWindow* window, platform::ios::IosTouchInput* touch,
               platform::ios::IosGyro* gyro, platform::ios::IosFileSystem* fs,
               const core::EngineDesc& desc);

    // One CADisplayLink tick: beginFrame -> fixed/variable update -> render
    // -> endFrame.  Returns false once the game has signalled shutdown (e.g.
    // the OS killed the process) so the caller can drop the display link.
    bool tickIos();

    // Tear down the runner.  Calls IGame::onShutdown then Engine::shutdown.
    // Idempotent — safe to call from applicationWillTerminate even if
    // tickIos has already failed.
    void shutdownIos();
#endif

    // Configure the fixed timestep (physics/gameplay tick rate).
    // Default 60Hz.
    void setFixedTimestep(float seconds)
    {
        fixedTimestep_ = seconds;
    }
    void setFixedRate(uint32_t hz)
    {
        fixedTimestep_ = 1.0f / static_cast<float>(hz);
    }

private:
    IGame& game_;
    float fixedTimestep_ = 1.0f / 60.0f;
    float maxAccumulator_ = 0.25f;

    // Shared frame-loop logic used by both run() and runAndroid().
    int runLoop(core::Engine& engine);

#if defined(__APPLE__) && TARGET_OS_IPHONE
    // iOS retains its Engine + Registry across CADisplayLink ticks.  The
    // pImpl-style pointer keeps GameRunner.h plain C++ (no need to include
    // Engine.h / Registry.h here just for member layout).
    struct IosState;
    IosState* iosState_ = nullptr;
#endif
};

}  // namespace engine::game
