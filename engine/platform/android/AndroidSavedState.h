#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::platform
{

// ---------------------------------------------------------------------------
// AndroidSavedState — file-based persistence under the activity's
// externalDataPath.
//
// NativeActivity does NOT surface `onSaveInstanceState` /
// `onRestoreInstanceState` to native code (they're Java-only callbacks on
// `android.app.Activity`).  The Bundle-based mechanism is therefore not
// usable from a pure-native game.  The supported workaround — also the path
// Google's NativeActivity sample uses — is to read/write a small state file
// under `android_app::activity->externalDataPath`, which maps to
// `Context.getExternalFilesDir(null)` and is automatically created by
// Android (no `WRITE_EXTERNAL_STORAGE` permission required for this path).
//
// Lifecycle integration: the Engine wires a callback into
// `APP_CMD_SAVE_STATE` so games can persist their own state synchronously
// without polling.  See `Engine::registerSaveStateCallback` in
// `engine/core/Engine.h`.
//
// All helpers in this header are host-buildable (no `<android/...>` includes)
// so unit tests can exercise the read/write/path-substitution paths on the
// dev Mac without an AVD.
// ---------------------------------------------------------------------------

/// Returns the per-app `externalDataPath` previously cached via
/// `setAndroidExternalDataPath`.  Empty string if not yet set (not on
/// Android, or `initAndroid()` has not yet run).
std::string androidExternalDataPath();

/// Cache the activity's `externalDataPath` for later lookup.  Called from
/// `Engine::initAndroid()` once `android_app::activity` is valid.  Pass an
/// arbitrary directory in unit tests to redirect read/write to a tmp dir.
void setAndroidExternalDataPath(std::string_view path);

/// Read a small byte blob from `<externalDataPath>/<fileName>`.  Returns an
/// empty vector if `externalDataPath` is unset, the file is missing, or the
/// read fails.
std::vector<uint8_t> readSavedState(const char* fileName);

/// Write `bytes` to `<externalDataPath>/<fileName>`.  Returns false if
/// `externalDataPath` is unset or the write fails (e.g. directory missing /
/// disk full).  Overwrites any existing content.  The write is best-effort
/// atomic: a sibling `*.tmp` file is written first and then renamed over the
/// target, so a crash mid-write leaves the previous snapshot intact.
bool writeSavedState(const char* fileName, std::span<const uint8_t> bytes);

}  // namespace engine::platform
