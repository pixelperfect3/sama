#pragma once

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// AndroidTierDetect — runtime device-tier classification for Android.
//
// Mirrors `engine::platform::ios::IosTierDetect` but for Android.  Where iOS
// can pin a tier exactly via `sysctl hw.machine` (Apple ships a small,
// well-known set of chips), Android has thousands of SKUs and no equivalent
// "device id".  We therefore classify on two signals that ARE reliable:
//
//   1. The Vulkan GPU device name (Adreno 7xx, Mali-G7xx, Xclipse 9xx, ...).
//      bgfx exposes the renderer/vendor IDs but not the human-readable name
//      pre-init; the Engine queries it after `bgfx::init` and passes the
//      string in here.
//   2. Total physical RAM read from `/proc/meminfo` (`MemTotal`).
//
// A third weak signal — CPU core count via `std::thread::hardware_concurrency`
// — is referenced in the design doc but is not load-bearing in the
// classifier (modern budget phones often expose 8 cores too) so we currently
// only use it as supplementary context in logs.
//
// The classifier is split into two pieces:
//   * `classifyAndroidTier(gpu, ramMb)` — pure function, lives in the .cpp,
//     trivially unit-testable on the macOS host build.
//   * `detectAndroidTier(gpuName)` — public entry point that fills in the
//     RAM signal from `/proc/meminfo` and delegates to `classifyAndroidTier`.
//
// Integration (see `Engine::initAndroid` in `engine/core/Engine.cpp`):
//   const auto tier = detectAndroidTier(gpuName);
//   LOGI("Tier detected: %s", androidTierLogName(tier));
//   // Engine forwards `androidTierToProjectConfigName(tier)` into
//   // `ProjectConfig::activeTier` ONLY when the project did not specify a
//   // tier explicitly (or used the new "auto" sentinel).
// ---------------------------------------------------------------------------

namespace engine::platform::android
{

enum class AndroidTier
{
    Unknown,
    Low,
    Mid,
    High,
};

// Detect the current device tier.  Order of evidence:
//   1. GPU device name (passed in — see header note above for why)
//   2. Total RAM via /proc/meminfo (MemTotal)
//   3. CPU core count via std::thread::hardware_concurrency() — weak signal
//
// Returns Unknown if no signal is conclusive.
//
// `gpuName` is intentionally a parameter rather than a global query because
// the Vulkan device isn't created until bgfx::init has run.  Pass the value
// from bgfx (`bgfx::getCaps()->vendorId` is available pre-init but the
// human-readable name is not exposed by bgfx — the v1 wiring passes an
// empty string and falls back to the RAM signal).
[[nodiscard]] AndroidTier detectAndroidTier(const std::string& gpuName = "");

// Friendly log name ("Low" / "Mid" / "High" / "Unknown").
[[nodiscard]] const char* androidTierLogName(AndroidTier tier);

// Map detection result to ProjectConfig tier name ("low" / "mid" / "high").
// Unknown maps to "mid" (sensible default — same convention as iOS).
[[nodiscard]] const char* androidTierToProjectConfigName(AndroidTier tier);

// Total physical RAM in MB.  Reads /proc/meminfo.  Returns 0 on failure
// (file missing, line not found, parse error).
[[nodiscard]] uint64_t androidTotalRamMb();

// ---------------------------------------------------------------------------
// Testable seam — pure function so unit tests can run on macOS desktop
// without going through /proc/meminfo or a real Vulkan device.
//
// `gpuName` may be empty or a vendor-supplied device name; the matcher is
// case-insensitive substring (e.g. "Adreno 740", "Mali-G715-Immortalis").
// `ramMb` is total physical RAM in megabytes; pass 0 when unknown.
//
// Combination logic (see implementation file's header comment for full
// rationale):
//   - GPU High AND RAM High        -> High
//   - GPU Low  AND RAM Low         -> Low
//   - GPU empty AND RAM >= 5120 MB -> High
//   - GPU empty AND RAM <  3072 MB -> Low
//   - RAM == 0  AND GPU known      -> use GPU class
//   - RAM == 0  AND GPU empty      -> Mid (no signal)
//   - Otherwise (mismatched)       -> Mid (conservative)
// ---------------------------------------------------------------------------
[[nodiscard]] AndroidTier classifyAndroidTier(const std::string& gpuName, uint64_t ramMb);

// ---------------------------------------------------------------------------
// Parse a `/proc/meminfo`-formatted string and return MemTotal in MB.
// Returns 0 if the line is absent or malformed.  Exposed so the unit test
// can drive it without touching the filesystem.
// ---------------------------------------------------------------------------
[[nodiscard]] uint64_t parseMemInfoTotalMb(const std::string& content);

// ---------------------------------------------------------------------------
// Classify just the GPU name (no RAM).  Returns Unknown when no substring
// in the lookup table matches.  Exposed for tests; production code should
// use `classifyAndroidTier` so the RAM signal participates in the answer.
// ---------------------------------------------------------------------------
[[nodiscard]] AndroidTier classifyGpuName(const std::string& gpuName);

}  // namespace engine::platform::android
