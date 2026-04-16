#pragma once

#include <android_native_app_glue.h>

namespace engine::platform
{

/// Runs the Sama engine main loop inside an Android NativeActivity.
/// Call from android_main() — blocks until the activity is destroyed.
void runAndroidApp(struct android_app* app);

}  // namespace engine::platform
