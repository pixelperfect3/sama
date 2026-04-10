// UI Test App — macOS entry point
//
// Comprehensive UI widget test demonstrating all Sama UI system functionality:
// panels, text, buttons, sliders, progress bars, layout, event dispatch,
// toast notifications, and modal dialogs.
//
// Controls:
//   1/2/3/4       — switch between screens (Menu / HUD / Settings / Inventory)
//   Mouse          — interact with buttons and sliders
//   Space (HUD)    — simulate taking damage
//   R     (HUD)    — simulate reload

#include "UiTestApp.h"
#include "engine/core/Engine.h"
#include "engine/game/GameRunner.h"

int main()
{
    engine::core::EngineDesc desc;
    desc.windowTitle = "Sama UI Test";
    desc.windowWidth = 1280;
    desc.windowHeight = 720;

    UiTestApp game;
    engine::game::GameRunner runner(game);
    return runner.run(desc);
}
