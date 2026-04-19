#pragma once

#include <cstdint>

#ifdef __ANDROID__
struct android_app;
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

#ifndef __ANDROID__
    // Run the full lifecycle: init -> loop -> shutdown.
    // Returns the process exit code (0 on clean exit).
    int run(const core::EngineDesc& desc);

    // Run using a ProjectConfig JSON file for configuration.
    // If configPath is null or the file is missing, uses defaults.
    int run(const char* configPath = nullptr);
#else
    // Run on Android: init -> loop -> shutdown using ANativeWindow.
    // Returns the process exit code (0 on clean exit).
    int runAndroid(struct android_app* app, const core::EngineDesc& desc);

    // Run on Android using a ProjectConfig for configuration.
    // If configPath is null, uses defaults.
    int runAndroid(struct android_app* app, const char* configPath = nullptr);
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
};

}  // namespace engine::game
