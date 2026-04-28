#pragma once

// Process-wide singleton state for the iOS platform layer.
//
// Mirrors AndroidGlobals.{h,cpp}: a small set of free functions that hold
// pointers shared across translation units (e.g. between the platform
// bootstrap and shader / asset loaders).
//
// All functions are safe to call from the main thread only.  The setters
// are typically invoked exactly once, during application launch.

#include <TargetConditionals.h>

namespace engine::platform::ios
{

// ---------------------------------------------------------------------------
// Resource bundle
//
// On Android the equivalent is the AAssetManager pointer, which is needed by
// the shader loader to resolve compiled .bin files inside the APK.  On iOS we
// keep a non-owning C-string with the absolute path to the application's
// resources directory.  The path is owned by the iOS runtime ([NSBundle
// mainBundle].resourcePath) and is valid for the lifetime of the process.
//
// Pass nullptr to clear (used by tests / teardown).
void setResourceBundlePath(const char* path);

// Returns nullptr if setResourceBundlePath() was never called.
const char* getResourceBundlePath();

// ---------------------------------------------------------------------------
// Game view
//
// Holds the UIView* that hosts the CAMetalLayer and receives touch events.
// Stored as a void* so this header stays plain C++ and can be included from
// non-ObjC translation units.  Internally the implementation does an
// __unsafe_unretained bridge — the application owns the view's lifetime
// (the IosWindow / view controller).
//
// Pass nullptr to clear.
void setGameView(void* uiViewPtr);

// Returns nullptr if setGameView() was never called or the view was cleared.
void* getGameView();

}  // namespace engine::platform::ios
