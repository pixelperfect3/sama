// iOS test bootstrap.
//
// Hands control to engine::platform::ios::runIosApp, which in turn calls
// UIApplicationMain with the engine's internal UIApplicationDelegate.  All
// game state lives in the IGame returned from samaCreateGame() (defined in
// IosTestGame.mm); this file exists solely to satisfy the linker's need for
// a main() entry point inside the executable target.
//
// Mirrors the Android stub entry pattern: the app's translation unit just
// instantiates the IGame, while the platform layer owns the lifecycle.

#include "engine/platform/ios/IosApp.h"

int main(int argc, char* argv[])
{
    return engine::platform::ios::runIosApp(argc, argv);
}
