#pragma once

#include <cstdint>

namespace engine::core
{
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
// ---------------------------------------------------------------------------

class GameRunner
{
public:
    explicit GameRunner(IGame& game);
    ~GameRunner();

    // Run the full lifecycle: init -> loop -> shutdown.
    // Returns the process exit code (0 on clean exit).
    int run(const core::EngineDesc& desc);

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
};

}  // namespace engine::game
