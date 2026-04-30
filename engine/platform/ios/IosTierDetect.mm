#include "engine/platform/ios/IosTierDetect.h"

#include <sys/sysctl.h>
#include <sys/types.h>

#include <cstring>
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && defined(__OBJC__)
#import <Foundation/Foundation.h>
#endif

namespace engine::platform::ios
{

namespace
{

// Tier classification table.
//
// Sourced from the device list in `docs/IOS_SUPPORT.md` plus public chip→
// model mappings (Apple's developer.apple.com/documentation/iphoneos and
// theiphonewiki.com).  Variants of the same model share a tier so we list
// every machine identifier rather than deriving it.
//
// We deliberately do NOT enumerate every iOS device ever shipped — older
// devices below A10 are below the engine's iOS 15 minimum, and devices
// added after the table was written get caught by the RAM heuristic in
// classifyIosTier().
struct TierEntry
{
    const char* identifier;  // exact match against `hw.machine` output
    IosTier tier;
};

// clang-format off
constexpr TierEntry kTierTable[] = {
    // ---------- LOW ----------------------------------------------------
    // A10 Fusion family — iPhone 7 / 7 Plus, iPad 6th gen, iPod touch 7.
    // Per IOS_SUPPORT.md: 30 FPS, 512px textures, no SSAO/IBL.
    {"iPhone9,1", IosTier::Low},   // iPhone 7   (GSM)
    {"iPhone9,2", IosTier::Low},   // iPhone 7 Plus (GSM)
    {"iPhone9,3", IosTier::Low},   // iPhone 7   (Global)
    {"iPhone9,4", IosTier::Low},   // iPhone 7 Plus (Global)
    {"iPad7,5",   IosTier::Low},   // iPad 6th gen (Wi-Fi)
    {"iPad7,6",   IosTier::Low},   // iPad 6th gen (Cellular)
    {"iPod9,1",   IosTier::Low},   // iPod touch 7th gen
    // iPhone SE 2nd gen — A13 chip but small thermal envelope; doc puts it Low.
    {"iPhone12,8", IosTier::Low},  // iPhone SE 2nd gen

    // ---------- MID ----------------------------------------------------
    // A13 in larger devices (iPhone 11 family, iPad 9th gen).
    {"iPhone12,1", IosTier::Mid},  // iPhone 11
    {"iPhone12,3", IosTier::Mid},  // iPhone 11 Pro
    {"iPhone12,5", IosTier::Mid},  // iPhone 11 Pro Max
    {"iPad12,1",   IosTier::Mid},  // iPad 9th gen (Wi-Fi)
    {"iPad12,2",   IosTier::Mid},  // iPad 9th gen (Cellular)

    // A14 — iPhone 12 family, iPad Air 4th gen.
    {"iPhone13,1", IosTier::Mid},  // iPhone 12 mini
    {"iPhone13,2", IosTier::Mid},  // iPhone 12
    {"iPhone13,3", IosTier::Mid},  // iPhone 12 Pro
    {"iPhone13,4", IosTier::Mid},  // iPhone 12 Pro Max
    {"iPad13,1",   IosTier::Mid},  // iPad Air 4th gen (Wi-Fi)
    {"iPad13,2",   IosTier::Mid},  // iPad Air 4th gen (Cellular)

    // A15 — iPhone 13 family, iPhone SE 3, iPhone 14 (non-Pro).  Per docs
    // SE 3 (A15) is Mid; the full A15 line follows the same tier.
    {"iPhone14,4", IosTier::Mid},  // iPhone 13 mini
    {"iPhone14,5", IosTier::Mid},  // iPhone 13
    {"iPhone14,2", IosTier::Mid},  // iPhone 13 Pro
    {"iPhone14,3", IosTier::Mid},  // iPhone 13 Pro Max
    {"iPhone14,6", IosTier::Mid},  // iPhone SE 3rd gen
    {"iPhone14,7", IosTier::Mid},  // iPhone 14
    {"iPhone14,8", IosTier::Mid},  // iPhone 14 Plus

    // ---------- HIGH ---------------------------------------------------
    // A16 — iPhone 14 Pro / 14 Pro Max, iPhone 15 / 15 Plus.
    {"iPhone15,2", IosTier::High}, // iPhone 14 Pro
    {"iPhone15,3", IosTier::High}, // iPhone 14 Pro Max
    {"iPhone15,4", IosTier::High}, // iPhone 15
    {"iPhone15,5", IosTier::High}, // iPhone 15 Plus

    // A17 Pro — iPhone 15 Pro / 15 Pro Max, iPad mini 7th gen.
    {"iPhone16,1", IosTier::High}, // iPhone 15 Pro
    {"iPhone16,2", IosTier::High}, // iPhone 15 Pro Max
    {"iPad16,1",   IosTier::High}, // iPad mini 7th gen (Wi-Fi)
    {"iPad16,2",   IosTier::High}, // iPad mini 7th gen (Cellular)

    // A18 / A18 Pro — iPhone 16 family.  Identifiers from public Apple data.
    {"iPhone17,1", IosTier::High}, // iPhone 16 Pro
    {"iPhone17,2", IosTier::High}, // iPhone 16 Pro Max
    {"iPhone17,3", IosTier::High}, // iPhone 16
    {"iPhone17,4", IosTier::High}, // iPhone 16 Plus
    {"iPhone17,5", IosTier::High}, // iPhone 16e (A18, single-camera)

    // M-series iPad Pro / Air.  Apple uses a wide range of iPad13/iPad14
    // identifiers for these — list the M1/M2/M4 generations explicitly.
    // M1 iPad Pro (2021) — 11" and 12.9".
    {"iPad13,4",   IosTier::High}, // iPad Pro 11" M1 (Wi-Fi)
    {"iPad13,5",   IosTier::High}, // iPad Pro 11" M1 (Wi-Fi+Cell)
    {"iPad13,6",   IosTier::High}, // iPad Pro 11" M1 (Cell, China)
    {"iPad13,7",   IosTier::High}, // iPad Pro 11" M1 (Cell, Global)
    {"iPad13,8",   IosTier::High}, // iPad Pro 12.9" M1 (Wi-Fi)
    {"iPad13,9",   IosTier::High}, // iPad Pro 12.9" M1 (Wi-Fi+Cell)
    {"iPad13,10",  IosTier::High}, // iPad Pro 12.9" M1 (Cell, China)
    {"iPad13,11",  IosTier::High}, // iPad Pro 12.9" M1 (Cell, Global)
    // M1 iPad Air 5th gen.
    {"iPad13,16",  IosTier::High}, // iPad Air 5th gen (Wi-Fi)
    {"iPad13,17",  IosTier::High}, // iPad Air 5th gen (Cellular)
    // M2 iPad Pro (2022) — 11" and 12.9".
    {"iPad14,3",   IosTier::High}, // iPad Pro 11" M2 (Wi-Fi)
    {"iPad14,4",   IosTier::High}, // iPad Pro 11" M2 (Cellular)
    {"iPad14,5",   IosTier::High}, // iPad Pro 12.9" M2 (Wi-Fi)
    {"iPad14,6",   IosTier::High}, // iPad Pro 12.9" M2 (Cellular)
    // M2 iPad Air (2024) — 11" and 13".
    {"iPad14,8",   IosTier::High}, // iPad Air 11" M2 (Wi-Fi)
    {"iPad14,9",   IosTier::High}, // iPad Air 11" M2 (Cellular)
    {"iPad14,10",  IosTier::High}, // iPad Air 13" M2 (Wi-Fi)
    {"iPad14,11",  IosTier::High}, // iPad Air 13" M2 (Cellular)
    // M4 iPad Pro (2024) — 11" and 13".  Apple uses iPad16,3/16,4 for the
    // 11" (Wi-Fi / Cellular) and iPad16,5/16,6 for the 13" (Wi-Fi /
    // Cellular).  The mini-7 entries above use 16,1/16,2 so there is no
    // overlap.
    {"iPad16,3",   IosTier::High}, // iPad Pro 11" M4 (Wi-Fi)
    {"iPad16,4",   IosTier::High}, // iPad Pro 11" M4 (Cellular)
    {"iPad16,5",   IosTier::High}, // iPad Pro 13" M4 (Wi-Fi)
    {"iPad16,6",   IosTier::High}, // iPad Pro 13" M4 (Cellular)
};
// clang-format on

constexpr uint64_t kOneGB = 1024ull * 1024ull * 1024ull;

bool isSimulatorIdentifier(const char* ident)
{
    if (ident == nullptr || ident[0] == '\0')
        return false;

    // The iOS simulator reports the host Mac's CPU architecture rather
    // than a real device identifier:
    //   - Intel Macs:           "x86_64"
    //   - Apple Silicon Macs:   "arm64"
    // Real iOS devices always start with "iPhone", "iPad", or "iPod"
    // followed by a version number, so a leading non-iDevice prefix is
    // an unambiguous simulator (or unit-test host) signal.
    if (std::strncmp(ident, "x86_64", 6) == 0)
        return true;
    if (std::strncmp(ident, "arm64", 5) == 0)
        return true;
    return false;
}

IosTier classifyByRam(uint64_t physicalMemoryBytes)
{
    if (physicalMemoryBytes == 0)
    {
        // We have no signal at all — pick the safe middle so the engine
        // still boots with a sensible feature set.  Better than guessing
        // High and dropping frames on a real Low device.
        return IosTier::Mid;
    }

    if (physicalMemoryBytes < 3 * kOneGB)
        return IosTier::Low;
    if (physicalMemoryBytes < 6 * kOneGB)
        return IosTier::Mid;
    return IosTier::High;
}

}  // namespace

// ---------------------------------------------------------------------------
// classifyIosTier — the testable seam.
// ---------------------------------------------------------------------------

IosTier classifyIosTier(const char* machineIdentifier, uint64_t physicalMemoryBytes)
{
    if (machineIdentifier != nullptr && machineIdentifier[0] != '\0')
    {
        // 1) Simulator / unit-test host: pin to High so we always exercise
        //    the full pipeline.  See header for rationale.
        if (isSimulatorIdentifier(machineIdentifier))
            return IosTier::High;

        // 2) Exact match against the lookup table.
        for (const auto& entry : kTierTable)
        {
            if (std::strcmp(entry.identifier, machineIdentifier) == 0)
                return entry.tier;
        }
    }

    // 3) Unknown device — fall back to RAM heuristic.
    return classifyByRam(physicalMemoryBytes);
}

// ---------------------------------------------------------------------------
// iosMachineIdentifier — read once, cache for the lifetime of the process.
// ---------------------------------------------------------------------------

const char* iosMachineIdentifier()
{
    // Static storage so the returned pointer is stable.  sysctlbyname is
    // cheap but we still avoid re-reading it on every HUD draw.
    static std::string cached;
    static bool initialised = false;
    if (initialised)
        return cached.c_str();

    initialised = true;

    // Query length first (sysctlbyname with NULL out-buf returns required
    // size in `size`).
    size_t size = 0;
    if (sysctlbyname("hw.machine", nullptr, &size, nullptr, 0) != 0 || size == 0)
    {
        cached.clear();
        return cached.c_str();
    }

    cached.resize(size);
    if (sysctlbyname("hw.machine", cached.data(), &size, nullptr, 0) != 0)
    {
        cached.clear();
        return cached.c_str();
    }

    // sysctlbyname includes the trailing NUL in the byte count; strip it
    // so std::string::size() / .c_str() agree.
    if (!cached.empty() && cached.back() == '\0')
        cached.pop_back();

    return cached.c_str();
}

// ---------------------------------------------------------------------------
// detectIosTier — public entry point.
// ---------------------------------------------------------------------------

IosTier detectIosTier()
{
    const char* ident = iosMachineIdentifier();

    uint64_t physicalMemoryBytes = 0;

#if defined(__APPLE__) && defined(__OBJC__)
    // NSProcessInfo is available on both macOS and iOS, so this branch
    // works for the desktop unit-test build as well as on-device.
    @autoreleasepool
    {
        physicalMemoryBytes = static_cast<uint64_t>([[NSProcessInfo processInfo] physicalMemory]);
    }
#else
    // Pure-C++ build (no Objective-C runtime): fall back to sysctl
    // hw.memsize.  This branch exists so the file can compile under a
    // hypothetical non-ObjC test harness; the standard build path uses
    // NSProcessInfo above.
    {
        int64_t memBytes = 0;
        size_t memLen = sizeof(memBytes);
        if (sysctlbyname("hw.memsize", &memBytes, &memLen, nullptr, 0) == 0 && memBytes > 0)
        {
            physicalMemoryBytes = static_cast<uint64_t>(memBytes);
        }
    }
#endif

    return classifyIosTier(ident, physicalMemoryBytes);
}

// ---------------------------------------------------------------------------
// String helpers — keep the mappings in one place so log lines and
// ProjectConfig keys cannot drift out of sync.
// ---------------------------------------------------------------------------

const char* iosTierLogName(IosTier tier)
{
    switch (tier)
    {
        case IosTier::Low:
            return "Low";
        case IosTier::Mid:
            return "Mid";
        case IosTier::High:
            return "High";
        case IosTier::Unknown:
        default:
            return "Unknown";
    }
}

const char* tierToProjectConfigName(IosTier tier)
{
    // Unknown -> "mid": safe default for unidentified hardware.  Picking
    // "high" risks overheating a low-end device we haven't classified;
    // picking "low" leaves modern hardware visibly under-utilised.  "mid"
    // exercises shadows + IBL + bloom but skips the heaviest features
    // (SSAO, 2K shadows, 60fps target).  Documented in docs/NOTES.md.
    switch (tier)
    {
        case IosTier::Low:
            return "low";
        case IosTier::High:
            return "high";
        case IosTier::Mid:
        case IosTier::Unknown:
        default:
            return "mid";
    }
}

}  // namespace engine::platform::ios
