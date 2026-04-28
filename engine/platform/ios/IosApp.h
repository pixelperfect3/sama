#pragma once

#include <TargetConditionals.h>

namespace engine::game
{
class IGame;
}

namespace engine::platform::ios
{

// ---------------------------------------------------------------------------
// IosApp — application bootstrap for the Sama engine on iOS.
//
// Mirrors the role of AndroidApp on Android: a tiny shim that owns the
// platform layer (window, view, touch input, gyro, file system) and runs
// the frame loop until the OS terminates the process.
//
// Usage from the application's main():
//
//   int main(int argc, char** argv)
//   {
//       return engine::platform::ios::runIosApp(argc, argv);
//   }
//
// The game must define a `samaCreateGame()` function returning a
// heap-allocated IGame*; the runner takes ownership and deletes it when
// the application terminates.  Same contract as AndroidApp:
//
//   engine::game::IGame* samaCreateGame()
//   {
//       return new MyGame();
//   }
//
// runIosApp() forwards to UIApplicationMain with the engine's internal
// UIApplicationDelegate, so it never returns under normal operation — the
// process exits when the OS kills it.  The return value is the value
// reported by UIApplicationMain (typically zero on clean exit).
// ---------------------------------------------------------------------------

int runIosApp(int argc, char** argv);

}  // namespace engine::platform::ios

// ---------------------------------------------------------------------------
// Game entry point — one definition per application, in C++ (NOT inside the
// engine::platform::ios namespace).  Mirrors the AndroidApp contract.
//
// Declared here so applications can pick up the prototype with a single
// include of <engine/platform/ios/IosApp.h>.
// ---------------------------------------------------------------------------
extern engine::game::IGame* samaCreateGame();
