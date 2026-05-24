// perf_smoke — entry point.  Drives PerfSmokeGame for N frames and exits
// with 0 on success, 1 if any per-system budget was exceeded.
//
// Run: build/perf_smoke                 (default: 600 frames, multi-threaded)
//      build/perf_smoke 1200            (override frame count)
//      build/perf_smoke 600 --single    (single-threaded bgfx)
//      build/perf_smoke 600 --multi     (multi-threaded bgfx, the default)
//
// To compare both modes in one go, run the wrapper script:
//      apps/perf_smoke/run_both.sh
// which invokes the binary twice (the bgfx::init / bgfx::shutdown contract
// in this codebase is one cycle per process — see ScreenshotFixture's
// BgfxContext for the same constraint).
//
// This binary intentionally opens a real GLFW + bgfx window so the
// timings include actual driver work, not just CPU-side logic — that's the
// point of a regression test on a desktop dev machine.  On Android the
// equivalent harness is a logcat trace; see the #ifdef __ANDROID__ block
// in PerfSmokeGame.cpp.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "PerfSmokeGame.h"
#include "engine/core/Engine.h"
#include "engine/game/GameRunner.h"

int main(int argc, char** argv)
{
    int frames = 600;
    bool singleThreaded = false;  // multi-threaded is the new default.

    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--single") == 0)
        {
            singleThreaded = true;
        }
        else if (std::strcmp(arg, "--multi") == 0)
        {
            singleThreaded = false;
        }
        else
        {
            // Positional integer: frame count.
            const int parsed = std::atoi(arg);
            if (parsed > 0)
                frames = parsed;
        }
    }

    engine::core::EngineDesc desc;
    desc.windowWidth = 1280;
    desc.windowHeight = 720;
    desc.windowTitle = singleThreaded ? "Sama perf_smoke [single]" : "Sama perf_smoke [multi]";
    desc.singleThreaded = singleThreaded;

    perf_smoke::PerfBudgets budgets;  // defaults — see PerfSmokeGame.h
    perf_smoke::PerfSmokeGame game(frames, budgets, singleThreaded);

    engine::game::GameRunner runner(game);
    const int runResult = runner.run(desc);
    if (runResult != 0)
    {
        std::fprintf(stderr, "perf_smoke: engine init failed (mode=%s)\n",
                     singleThreaded ? "single" : "multi");
        return runResult;
    }

    // GameRunner::runLoop returns 0 on clean exit; the actual pass/fail of
    // the budget check lives on the game object.
    return game.exitCode();
}
