#include "engine/platform/android/AndroidApp.h"

#include <android/log.h>
#include <android_native_app_glue.h>

#include "engine/game/GameRunner.h"
#include "engine/game/IGame.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SamaEngine", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SamaEngine", __VA_ARGS__)

// ---------------------------------------------------------------------------
// Each Android game must define this function, returning a heap-allocated
// IGame instance.  The runner takes ownership and deletes it on exit.
//
// Example (in your game's .cpp):
//
//   engine::game::IGame* samaCreateGame()
//   {
//       return new MyGame();
//   }
// ---------------------------------------------------------------------------

extern engine::game::IGame* samaCreateGame();

namespace engine::platform
{

void runAndroidApp(struct android_app* app)
{
    LOGI("Sama Engine — Android bootstrap starting");

    engine::game::IGame* game = samaCreateGame();
    if (!game)
    {
        LOGE("samaCreateGame() returned null — exiting");
        return;
    }

    // Wire the ProjectConfig template bundled in apps/<game>/project.json
    // through the Android asset reader.  Falls back to defaults if the file
    // is missing inside the APK.  See GameRunner::runAndroid(configPath) for
    // the read path (AndroidFileSystem -> ProjectConfig::loadFromString).
    engine::game::GameRunner runner(*game);
    int result = runner.runAndroid(app, "project.json");

    delete game;

    LOGI("Sama Engine — Android bootstrap exiting (code %d)", result);
}

}  // namespace engine::platform

// ---------------------------------------------------------------------------
// NativeActivity entry point — called by android_native_app_glue.
// ---------------------------------------------------------------------------
void android_main(struct android_app* app)
{
    engine::platform::runAndroidApp(app);
}
