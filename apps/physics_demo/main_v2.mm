// Physics Demo v2 — GameRunner entry point
//
// Same physics demo as main.mm, but using the IGame/GameRunner pattern
// instead of a hand-rolled main loop.

#include "PhysicsGame.h"
#include "engine/core/Engine.h"
#include "engine/game/GameRunner.h"

int main()
{
    engine::core::EngineDesc desc;
    desc.windowTitle = "Physics Demo v2";

    PhysicsGame game;
    engine::game::GameRunner runner(game);
    return runner.run(desc);
}
