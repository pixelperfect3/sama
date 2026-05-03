#include "engine/platform/android/AndroidTierDetect.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// AndroidTierDetect.cpp — implementation.
//
// Why RAM + GPU substring rather than (a) a JNI bridge to
// `ActivityManager.getMemoryInfo` or (b) a full PCI-id table:
//
//   * `/proc/meminfo` is readable from any Android process without JNI or
//     manifest permissions.  Going through ActivityManager would require
//     plumbing a JNIEnv* through to the platform layer just to read a
//     number that the kernel already prints for us.  The tradeoff is we
//     report total RAM (kernel + reserved) rather than "available to apps"
//     — but every device-tier reference in the wild uses total RAM as the
//     bucket boundary, so this is the right number anyway.
//   * A PCI-id table for Android GPUs would be enormous and stale within
//     months — there is no equivalent of Apple's chip→model map.  A
//     substring match against the device name catches whole GPU families
//     in a few entries, and the RAM signal saves us when a brand-new
//     Adreno generation ships.
//
// GPU lookup table:
//
//   | GPU substring (case-insensitive) | Tier | Rationale                    |
//   |----------------------------------|------|------------------------------|
//   | Adreno 7  (Adreno 7xx)           | High | flagship Snapdragon 8 Gen 1+ |
//   | Adreno 6  (Adreno 6xx)           | Mid  | mainstream 2018-2021         |
//   | Adreno 5  (Adreno 5xx)           | Low  | older mid-range              |
//   | Adreno 4  (Adreno 4xx)           | Low  | older flagships, now budget  |
//   | Mali-G7   (G710, Immortalis G715)| High | Pixel 7+, Galaxy S22+        |
//   | Immortalis                       | High | Mali-Immortalis line, 2022+  |
//   | Mali-G6   (G610, G68, G615)      | Mid  | mainstream 2021+             |
//   | Mali-G5   (G57, G52, G51)        | Mid  | mainstream 2019-2020         |
//   | Mali-G3   (G31, G310, G320)      | Low  | budget Mali                  |
//   | Mali-T8   (T880, T860)           | Low  | older Midgard architecture   |
//   | PowerVR                          | Mid  | rare on modern Android,      |
//   |                                  |      | conservative default         |
//   | Xclipse 9 (Xclipse 920+)         | High | Galaxy S22+ (RDNA-based)     |
//
// Notable families intentionally NOT mapped (RAM heuristic decides):
//
//   * Adreno 8xx — too new at the time of writing, will route through RAM
//     and most likely land High by 6+ GB threshold.
//   * Mali-G3xx (some variants) — wide range from low-end IoT to entry
//     phones; RAM is a better signal than the family name.
//   * VideoCore (Broadcom) — not used on phones.
//
// Combination logic — fall-through table at end of `classifyAndroidTier`.
// ---------------------------------------------------------------------------

namespace engine::platform::android
{

namespace
{

struct GpuEntry
{
    const char* needle;  // case-insensitive substring
    AndroidTier tier;
};

// Order matters when one needle is a prefix of another (e.g. "Adreno 7"
// matches both "Adreno 740" and accidentally "Adreno 720").  We only have
// distinct family roots in the table so prefix collisions are not an
// issue, but we keep the longer / more-specific needles earlier as a
// matter of habit.
// clang-format off
constexpr GpuEntry kGpuTable[] = {
    // ---------- HIGH ---------------------------------------------------
    {"adreno 7",   AndroidTier::High},  // Adreno 7xx — Snapdragon 8 Gen 1+
    {"mali-g7",    AndroidTier::High},  // Mali-G710 / G715 — Pixel 7+, S22+
    {"immortalis", AndroidTier::High},  // Mali-Immortalis G715+ family
    {"xclipse 9",  AndroidTier::High},  // Samsung Xclipse 9xx (RDNA)

    // ---------- MID ----------------------------------------------------
    {"adreno 6",   AndroidTier::Mid},   // Adreno 6xx — mainstream 2018-2021
    {"mali-g6",    AndroidTier::Mid},   // Mali-G610 / G615 / G68 — mainstream
    {"mali-g5",    AndroidTier::Mid},   // Mali-G57 / G52 / G51
    {"powervr",    AndroidTier::Mid},   // rare on modern Android — conservative

    // ---------- LOW ----------------------------------------------------
    {"adreno 5",   AndroidTier::Low},   // Adreno 5xx
    {"adreno 4",   AndroidTier::Low},   // Adreno 4xx
    {"mali-g3",    AndroidTier::Low},   // Mali-G3xx — budget Mali
    {"mali-t8",    AndroidTier::Low},   // Mali-T8xx — older Midgard
};
// clang-format on

// Tier thresholds.  The Low cutoff matches iOS (< 3 GB), but the High
// cutoff is 5 GB on Android vs 6 GB on iOS: most Android flagships ship
// 6/8/12 GB and most mid-range devices ship 4 GB, so a 5 GB boundary
// cleanly separates the two without grouping 6 GB devices into Mid.
// (iOS uses 6 GB because the device-list path catches every shipping
// SKU first, leaving the RAM heuristic only as a fallback for unknown
// future devices.)
constexpr uint64_t kRamLowMaxMb = 3072;   // < 3 GB  -> Low
constexpr uint64_t kRamHighMinMb = 5120;  // >= 5 GB -> High

std::string toLowerCopy(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

AndroidTier classifyByRam(uint64_t ramMb)
{
    if (ramMb == 0)
    {
        // No signal — caller decides whether to substitute Mid.
        return AndroidTier::Unknown;
    }
    if (ramMb < kRamLowMaxMb)
        return AndroidTier::Low;
    if (ramMb >= kRamHighMinMb)
        return AndroidTier::High;
    return AndroidTier::Mid;
}

}  // namespace

// ---------------------------------------------------------------------------
// classifyGpuName — pure substring matcher.
// ---------------------------------------------------------------------------

AndroidTier classifyGpuName(const std::string& gpuName)
{
    if (gpuName.empty())
        return AndroidTier::Unknown;

    const std::string lowered = toLowerCopy(gpuName);
    for (const auto& entry : kGpuTable)
    {
        if (lowered.find(entry.needle) != std::string::npos)
            return entry.tier;
    }
    return AndroidTier::Unknown;
}

// ---------------------------------------------------------------------------
// classifyAndroidTier — combination logic.
// ---------------------------------------------------------------------------

AndroidTier classifyAndroidTier(const std::string& gpuName, uint64_t ramMb)
{
    const AndroidTier gpuTier = classifyGpuName(gpuName);
    const AndroidTier ramTier = classifyByRam(ramMb);

    // Both signals available — agree or disagree?
    if (gpuTier != AndroidTier::Unknown && ramTier != AndroidTier::Unknown)
    {
        if (gpuTier == ramTier)
            return gpuTier;
        // Mismatched signals (e.g. flagship GPU paired with little RAM, or
        // weak GPU in a tablet with lots of RAM).  Land on Mid — neither
        // extreme is safe to assume.
        return AndroidTier::Mid;
    }

    // Only GPU is known — trust it.
    if (gpuTier != AndroidTier::Unknown)
        return gpuTier;

    // Only RAM is known — bucket by RAM.
    if (ramTier != AndroidTier::Unknown)
        return ramTier;

    // No signal at all.  Caller maps Unknown -> "mid" via
    // `androidTierToProjectConfigName`.
    return AndroidTier::Unknown;
}

// ---------------------------------------------------------------------------
// parseMemInfoTotalMb — testable seam for /proc/meminfo parsing.
//
// The kernel format is one key/value per line with a unit suffix:
//
//   MemTotal:        3973128 kB
//   MemFree:          122336 kB
//   ...
//
// We accept either "kB" / "KB" / "kb" or no unit (treat as kB — that's the
// only unit the kernel ever emits for these fields).
// ---------------------------------------------------------------------------

uint64_t parseMemInfoTotalMb(const std::string& content)
{
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line))
    {
        // Look for "MemTotal:" prefix.
        constexpr const char* kKey = "MemTotal:";
        if (line.compare(0, std::strlen(kKey), kKey) != 0)
            continue;

        // Extract the numeric token after the key.
        std::istringstream ls(line.substr(std::strlen(kKey)));
        uint64_t kb = 0;
        if (!(ls >> kb) || kb == 0)
            return 0;

        // Convert kB -> MB (rounding down — close enough for tier
        // bucketing where the cuts are 1024 MB apart).
        return kb / 1024ull;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// androidTotalRamMb — read /proc/meminfo from the filesystem.
// ---------------------------------------------------------------------------

uint64_t androidTotalRamMb()
{
    std::ifstream f("/proc/meminfo");
    if (!f.is_open())
        return 0;

    std::ostringstream ss;
    ss << f.rdbuf();
    return parseMemInfoTotalMb(ss.str());
}

// ---------------------------------------------------------------------------
// detectAndroidTier — public entry point.
// ---------------------------------------------------------------------------

AndroidTier detectAndroidTier(const std::string& gpuName)
{
    const uint64_t ramMb = androidTotalRamMb();
    return classifyAndroidTier(gpuName, ramMb);
}

// ---------------------------------------------------------------------------
// String helpers — keep the mappings in one place so log lines and
// ProjectConfig keys cannot drift out of sync.
// ---------------------------------------------------------------------------

const char* androidTierLogName(AndroidTier tier)
{
    switch (tier)
    {
        case AndroidTier::Low:
            return "Low";
        case AndroidTier::Mid:
            return "Mid";
        case AndroidTier::High:
            return "High";
        case AndroidTier::Unknown:
        default:
            return "Unknown";
    }
}

const char* androidTierToProjectConfigName(AndroidTier tier)
{
    // Unknown -> "mid": same rationale as iOS.  Picking "high" risks
    // overheating an unidentified low-end device; picking "low" leaves
    // modern hardware visibly under-utilised.  "mid" exercises shadows
    // + IBL + bloom but skips the heaviest features.  Documented in
    // docs/NOTES.md.
    switch (tier)
    {
        case AndroidTier::Low:
            return "low";
        case AndroidTier::High:
            return "high";
        case AndroidTier::Mid:
        case AndroidTier::Unknown:
        default:
            return "mid";
    }
}

}  // namespace engine::platform::android
