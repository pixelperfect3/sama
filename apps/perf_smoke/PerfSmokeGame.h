// perf_smoke — per-system CPU budget regression test.
//
// Purpose: spin up a representative Sama scene (dynamic Jolt bodies + static
// walls + lights + a PBR cube), drive it for a fixed number of frames at a
// fixed timestep, and assert each engine system stays under a per-system
// wall-clock budget.  Exits 0 on success, 1 if any budget is exceeded.
//
// This is a regression net, not a profiler.  Numbers come from
// std::chrono::steady_clock; nothing fancy.  Run it manually:
//   build/perf_smoke
// or wire it into CI.  If a future PR slows down PhysicsSystem::update or
// DrawCallBuildSystem::update past the budgets in PerfBudgets, the run fails.
//
// Scene composition (deterministic, mt19937 seed = 42):
//   - 200 dynamic Jolt boxes scattered above a ground plane
//   - 500 static "wall" boxes laid out in a grid
//   - 16 point lights at fixed positions (latent — fs_pbr doesn't yet bind
//     them per draw, but LightClusterBuilder will run regardless)
//   - 1 PBR cube standing in for the helmet (no glTF dependency)
//   - 1 ground plane
//
// Total: ~702 entities, all visible, all shadow-casting.
//
// Not a unit test (no Catch2): the binary itself is the harness so it can be
// run on real hardware with a real GPU, which is the whole point — the
// numbers tell us about the platform, not just the algorithm.

#pragma once

#include <array>
#include <chrono>
#include <glm/glm.hpp>
#include <random>
#include <vector>

#include "engine/ecs/Entity.h"
#include "engine/game/IGame.h"
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsSystem.h"
#include "engine/rendering/LightClusterBuilder.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/rendering/systems/FrustumCullSystem.h"

namespace perf_smoke
{

// Per-system CPU budgets (mean ms / frame).  Sized at ~3x measured-good on
// a 2024-era M-series Mac with the scene below.  3x leaves headroom for
// noise but still catches genuine regressions.  Android rough multiplier
// vs. desktop is 2-4x — halve these if you want to enforce phone budgets.
struct PerfBudgets
{
    float physicsMeanMs = 0.80f;       // measured ~0.24 ms / frame
    float transformMeanMs = 0.20f;     // measured ~0.05 ms / frame
    float frustumCullMeanMs = 0.15f;   // measured ~0.02 ms / frame
    float drawCallMeanMs = 0.50f;      // measured ~0.12 ms / frame
    float shadowSubmitMeanMs = 0.20f;  // measured ~0.04 ms / frame
    float lightClusterMeanMs = 0.50f;  // measured ~0.13 ms / frame
    float frameP99Ms = 4.0f;           // measured p99 ~0.45 ms (3x guard)
};

class PerfSmokeGame : public engine::game::IGame
{
public:
    // singleThreaded labels the budget table heading so the two modes are
    // distinguishable when run back-to-back.  It does NOT itself flip
    // EngineDesc::singleThreaded — main.mm owns that decision and the label
    // is informational only.  Default matches EngineDesc::singleThreaded
    // (false = multi-threaded) so callers that omit the arg get a label
    // consistent with the engine default.
    explicit PerfSmokeGame(int framesToRun, const PerfBudgets& budgets,
                           bool singleThreaded = false);

    void onInit(engine::core::Engine& engine, engine::ecs::Registry& reg) override;
    void onFixedUpdate(engine::core::Engine& engine, engine::ecs::Registry& reg,
                       float fixedDt) override;
    void onUpdate(engine::core::Engine& engine, engine::ecs::Registry& reg, float dt) override;
    void onRender(engine::core::Engine& engine) override;
    void onShutdown(engine::core::Engine& engine, engine::ecs::Registry& reg) override;

    // True after the configured frame count is reached.  main.mm polls this
    // and posts a window-close request so the GameRunner loop terminates.
    [[nodiscard]] bool done() const
    {
        return done_;
    }

    // Exit code for main() to return: 0 = all budgets met, 1 = at least one
    // budget was exceeded.  Only valid after done() is true.
    [[nodiscard]] int exitCode() const
    {
        return exitCode_;
    }

private:
    void spawnScene(engine::core::Engine& engine, engine::ecs::Registry& reg);
    void reportAndCheckBudgets();

    int framesToRun_;
    PerfBudgets budgets_;
    bool singleThreaded_ = true;  // label only; see ctor comment.
    int frameIndex_ = 0;
    bool done_ = false;
    int exitCode_ = 0;

    // Engine subsystems we drive directly (rather than via the sample-game
    // path) so we can time each in isolation.
    engine::physics::JoltPhysicsEngine physics_;
    engine::physics::PhysicsSystem physicsSys_;
    engine::rendering::DrawCallBuildSystem drawSys_;
    engine::rendering::FrustumCullSystem cullSys_;
    engine::rendering::LightClusterBuilder lightCluster_;

    // Resource IDs.
    uint32_t cubeMeshId_ = 0;
    uint32_t dynMatId_ = 0;
    uint32_t wallMatId_ = 0;
    uint32_t groundMatId_ = 0;
    uint32_t helmetMatId_ = 0;

    // Per-frame timing samples (in ms).  We pre-reserve so push_back never
    // allocates inside the measurement loop.
    struct Samples
    {
        std::vector<float> physics;
        std::vector<float> transform;
        std::vector<float> cull;
        std::vector<float> draw;
        std::vector<float> shadow;
        std::vector<float> lightCluster;
        std::vector<float> frame;

        // Engine-reported timings sampled from Engine::frameStats() at the
        // start of each frame (so they describe the PREVIOUS frame, which
        // is fine for aggregate mean/p99 stats).  The bgfx::frame() value
        // is the headline number for comparing single- vs multi-threaded
        // mode: it drops from ~10 ms (single) to ~0.1 ms (multi) when the
        // render thread takes over the vsync / GPU wait.
        std::vector<float> bgfxFrame;
        std::vector<float> postProcess;
        std::vector<float> endFrame;
    } samples_;

    std::mt19937 rng_{42};
    std::chrono::steady_clock::time_point frameStart_{};

    // Stashed during onInit so onRender can drive systems without going
    // through engine accessors.  Lifetime: bound to GameRunner::runLoop.
    engine::ecs::Registry* registry_ = nullptr;
};

}  // namespace perf_smoke
