#pragma once

#include <android_native_app_glue.h>

namespace engine::platform
{

/// Runs the Sama engine main loop inside an Android NativeActivity.
/// Call from android_main() — blocks until the activity is destroyed.
///
/// The game must define a `samaCreateGame()` function (see AndroidApp.cpp)
/// that returns a heap-allocated IGame instance.  The runner takes ownership
/// and deletes it on exit.
void runAndroidApp(struct android_app* app);

}  // namespace engine::platform
