#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// IosTierDetect — runtime device-tier classification for iOS.
//
// The asset pipeline ships three quality tiers (low / mid / high — see
// `docs/IOS_SUPPORT.md` and `engine::game::TierConfig`).  This header exposes
// the runtime side: at app launch we read the device's hardware identifier
// (`sysctlbyname("hw.machine", ...)`) plus its physical RAM and decide which
// tier to use.
//
// Detection is reliable on iOS because Apple ships a small, well-known set
// of chips and never lies about the device model.  A static lookup table
// covers the devices listed in IOS_SUPPORT.md plus the obvious "future" iPhone
// chip classes; anything missing falls back to a RAM-based heuristic so a
// freshly-shipped device still picks a sensible default.
//
// The implementation lives in IosTierDetect.mm.  The lookup logic itself is
// platform-agnostic (`classifyIosTier`) so the unit test can drive it on a
// macOS build host.
//
// TODO(integration): wire `detectIosTier()` into the iOS app launch path so
// the result feeds `engine::game::ProjectConfig::activeTier`.  The natural
// call site is `_SamaAppDelegate::application:didFinishLaunchingWithOptions:`
// in `engine/platform/ios/IosApp.mm`, immediately after the file system is
// constructed and before `GameRunner::runIos` is invoked (so the engine sees
// the chosen tier before init).
// ---------------------------------------------------------------------------

namespace engine::platform::ios
{

enum class IosTier
{
    Unknown,
    Low,
    Mid,
    High,
};

// Returns the device tier based on chip identifier + physical RAM.
// Reads `[NSProcessInfo processInfo] physicalMemory` and the machine
// identifier via sysctlbyname("hw.machine", ...).  On the simulator
// (machine starts with "x86_64" or "arm64"), returns High so developers
// always exercise the full feature set in the simulator regardless of
// the host Mac's chip — see classifyIosTier() for the rationale.
[[nodiscard]] IosTier detectIosTier();

// Human-readable chip identifier for HUD / log purposes.
// Examples: "iPhone15,2", "iPad13,16", or "arm64" / "x86_64" on the
// simulator.  The pointer is owned by the implementation and remains
// valid for the process lifetime; callers should not free it.
[[nodiscard]] const char* iosMachineIdentifier();

// ---------------------------------------------------------------------------
// Testable seam — pure function so unit tests can run on macOS desktop
// without going through `sysctlbyname`.
//
// `machineIdentifier` must be NUL-terminated (e.g. "iPhone15,2").  May be
// nullptr or empty, in which case the RAM heuristic alone is consulted.
//
// `physicalMemoryBytes` is the value reported by NSProcessInfo (bytes,
// not MB).  Pass 0 if unknown — the function falls back to Mid in that
// case rather than picking based on a misleading number.
//
// Classification rules:
//   1. If the identifier matches a known iPhone/iPad/iPod model in the
//      table, return that tier.  The table is the source of truth for
//      the devices called out in IOS_SUPPORT.md.
//   2. If the identifier looks like a simulator ("x86_64" or "arm64"),
//      return High — the simulator runs on the host Mac which is always
//      flagship-class for the chips we support, and devs should see the
//      full pipeline by default.  Override via the env var or a future
//      project.json knob if needed.
//   3. Otherwise (unknown/future device): use RAM:
//        <  3 GB  -> Low
//        3..<6 GB -> Mid
//        >= 6 GB  -> High
// ---------------------------------------------------------------------------
[[nodiscard]] IosTier classifyIosTier(const char* machineIdentifier, uint64_t physicalMemoryBytes);

}  // namespace engine::platform::ios
