// perf_smoke — entry point.  Drives PerfSmokeGame for N frames and exits
// with 0 on success, 1 if any per-system budget was exceeded.
//
// Run: build/perf_smoke              (default 600 frames, ~10s @ 60 fps)
//      build/perf_smoke 1200         (override frame count)
//
// This binary intentionally opens a real GLFW + bgfx window so the
// timings include actual driver work, not just CPU-side logic — that's the
// point of a regression test on a desktop dev machine.  On Android the
// equivalent harness is a logcat trace; not in scope here.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "PerfSmokeGame.h"
#include "engine/core/Engine.h"
#include "engine/game/GameRunner.h"

int main(int argc, char** argv)
{
    int frames = 600;
    if (argc > 1)
    {
        frames = std::atoi(argv[1]);
        if (frames <= 0)
            frames = 600;
    }

    engine::core::EngineDesc desc;
    desc.windowWidth = 1280;
    desc.windowHeight = 720;
    desc.windowTitle = "Sama perf_smoke";

    perf_smoke::PerfBudgets budgets;  // defaults — see PerfSmokeGame.h
    perf_smoke::PerfSmokeGame game(frames, budgets);

    engine::game::GameRunner runner(game);
    const int runResult = runner.run(desc);
    if (runResult != 0)
    {
        std::fprintf(stderr, "perf_smoke: engine init failed\n");
        return runResult;
    }

    // GameRunner::runLoop returns 0 on clean exit; the actual pass/fail of
    // the budget check lives on the game object.
    return game.exitCode();
}
