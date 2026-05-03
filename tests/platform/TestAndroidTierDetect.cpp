#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

#include "engine/platform/android/AndroidTierDetect.h"

// ---------------------------------------------------------------------------
// TestAndroidTierDetect — runs on the macOS host build.
//
// All the classification logic is pure (no /proc/meminfo, no Vulkan), so we
// can drive every branch from a desktop test binary.  The on-device
// `androidTotalRamMb()` reads the real /proc/meminfo so we only smoke-test
// it (`returns 0 on macOS host` is the expected behaviour).
// ---------------------------------------------------------------------------

using engine::platform::android::AndroidTier;
using engine::platform::android::androidTierLogName;
using engine::platform::android::androidTierToProjectConfigName;
using engine::platform::android::androidTotalRamMb;
using engine::platform::android::classifyAndroidTier;
using engine::platform::android::classifyGpuName;
using engine::platform::android::detectAndroidTier;
using engine::platform::android::parseMemInfoTotalMb;

namespace
{
// Threshold values chosen to be unambiguous in the RAM heuristic
// (boundaries are 3072 / 5120 MB).
constexpr uint64_t kLowRam = 2048;   // 2 GB  -> Low
constexpr uint64_t kMidRam = 4096;   // 4 GB  -> Mid
constexpr uint64_t kHighRam = 8192;  // 8 GB  -> High
}  // namespace

// ---------------------------------------------------------------------------
// classifyGpuName
// ---------------------------------------------------------------------------

TEST_CASE("classifyGpuName — High tier GPUs map correctly", "[android][tier]")
{
    REQUIRE(classifyGpuName("Adreno 740") == AndroidTier::High);
    REQUIRE(classifyGpuName("Adreno 730") == AndroidTier::High);
    REQUIRE(classifyGpuName("Adreno 750") == AndroidTier::High);
    REQUIRE(classifyGpuName("Mali-G710") == AndroidTier::High);
    REQUIRE(classifyGpuName("Mali-G715-Immortalis") == AndroidTier::High);
    REQUIRE(classifyGpuName("Immortalis-G715") == AndroidTier::High);
    REQUIRE(classifyGpuName("Xclipse 920") == AndroidTier::High);

    // Case-insensitive — vendors are inconsistent about capitalisation.
    REQUIRE(classifyGpuName("adreno 740") == AndroidTier::High);
    REQUIRE(classifyGpuName("ADRENO 740") == AndroidTier::High);
    REQUIRE(classifyGpuName("mali-g710") == AndroidTier::High);
}

TEST_CASE("classifyGpuName — Mid tier GPUs map correctly", "[android][tier]")
{
    REQUIRE(classifyGpuName("Adreno 640") == AndroidTier::Mid);
    REQUIRE(classifyGpuName("Adreno 660") == AndroidTier::Mid);
    REQUIRE(classifyGpuName("Adreno 619") == AndroidTier::Mid);
    REQUIRE(classifyGpuName("Mali-G610") == AndroidTier::Mid);
    REQUIRE(classifyGpuName("Mali-G68") == AndroidTier::Mid);
    REQUIRE(classifyGpuName("Mali-G57") == AndroidTier::Mid);
    REQUIRE(classifyGpuName("Mali-G52") == AndroidTier::Mid);
    REQUIRE(classifyGpuName("PowerVR GX6450") == AndroidTier::Mid);
    REQUIRE(classifyGpuName("PowerVR Rogue GE8320") == AndroidTier::Mid);
}

TEST_CASE("classifyGpuName — Low tier GPUs map correctly", "[android][tier]")
{
    REQUIRE(classifyGpuName("Adreno 540") == AndroidTier::Low);
    REQUIRE(classifyGpuName("Adreno 530") == AndroidTier::Low);
    REQUIRE(classifyGpuName("Adreno 506") == AndroidTier::Low);
    REQUIRE(classifyGpuName("Adreno 430") == AndroidTier::Low);
    REQUIRE(classifyGpuName("Adreno 420") == AndroidTier::Low);
    REQUIRE(classifyGpuName("Mali-G31") == AndroidTier::Low);
    REQUIRE(classifyGpuName("Mali-G310") == AndroidTier::Low);
    REQUIRE(classifyGpuName("Mali-T880") == AndroidTier::Low);
    REQUIRE(classifyGpuName("Mali-T860") == AndroidTier::Low);
}

TEST_CASE("classifyGpuName — empty / unknown returns Unknown", "[android][tier][fallback]")
{
    REQUIRE(classifyGpuName("") == AndroidTier::Unknown);
    REQUIRE(classifyGpuName("Adreno 999") == AndroidTier::Unknown);  // future
    REQUIRE(classifyGpuName("Some Vendor GPU") == AndroidTier::Unknown);
    REQUIRE(classifyGpuName("VideoCore IV") == AndroidTier::Unknown);
    REQUIRE(classifyGpuName("Adreno 8") == AndroidTier::Unknown);  // 8xx not listed yet
}

// ---------------------------------------------------------------------------
// parseMemInfoTotalMb
// ---------------------------------------------------------------------------

TEST_CASE("parseMemInfoTotalMb — pixel 6 sample meminfo", "[android][tier][meminfo]")
{
    // Trimmed copy of /proc/meminfo from a Pixel 6 (mid-tier reference).
    const std::string meminfo =
        "MemTotal:        7733128 kB\n"
        "MemFree:          122336 kB\n"
        "MemAvailable:    4567890 kB\n"
        "Buffers:           48292 kB\n"
        "Cached:          2317644 kB\n";

    // 7733128 kB / 1024 = 7551 MB
    REQUIRE(parseMemInfoTotalMb(meminfo) == 7551);
}

TEST_CASE("parseMemInfoTotalMb — boundary values map to expected tiers", "[android][tier][meminfo]")
{
    // 2 GB device — should classify Low via classifyByRam.
    REQUIRE(parseMemInfoTotalMb("MemTotal:        2097152 kB\n") == 2048);
    REQUIRE(classifyAndroidTier("", parseMemInfoTotalMb("MemTotal: 2097152 kB\n")) ==
            AndroidTier::Low);

    // 3 GB exactly — falls in the >= 3072 < 5120 Mid bucket.
    REQUIRE(parseMemInfoTotalMb("MemTotal:        3145728 kB\n") == 3072);
    REQUIRE(classifyAndroidTier("", 3072) == AndroidTier::Mid);

    // 4 GB — Mid.
    REQUIRE(classifyAndroidTier("", 4096) == AndroidTier::Mid);

    // 5 GB — High threshold (>= 5120).
    REQUIRE(parseMemInfoTotalMb("MemTotal:        5242880 kB\n") == 5120);
    REQUIRE(classifyAndroidTier("", 5120) == AndroidTier::High);

    // 8 GB — High.
    REQUIRE(classifyAndroidTier("", 8192) == AndroidTier::High);
}

TEST_CASE("parseMemInfoTotalMb — malformed inputs return 0", "[android][tier][meminfo]")
{
    REQUIRE(parseMemInfoTotalMb("") == 0);
    REQUIRE(parseMemInfoTotalMb("nothing useful here\n") == 0);
    REQUIRE(parseMemInfoTotalMb("MemFree: 1000 kB\n") == 0);  // wrong key
    REQUIRE(parseMemInfoTotalMb("MemTotal:\n") == 0);         // no value
    REQUIRE(parseMemInfoTotalMb("MemTotal:        not_a_number kB\n") == 0);
    REQUIRE(parseMemInfoTotalMb("MemTotal:        0 kB\n") == 0);
}

TEST_CASE("parseMemInfoTotalMb — MemTotal not on first line", "[android][tier][meminfo]")
{
    // Real /proc/meminfo always puts MemTotal first, but the parser must
    // not depend on that ordering.
    const std::string meminfo =
        "MemFree:          122336 kB\n"
        "MemAvailable:    4567890 kB\n"
        "MemTotal:        4194304 kB\n"
        "Buffers:           48292 kB\n";
    REQUIRE(parseMemInfoTotalMb(meminfo) == 4096);
}

// ---------------------------------------------------------------------------
// classifyAndroidTier — combination logic
// ---------------------------------------------------------------------------

TEST_CASE("classifyAndroidTier — agreeing signals reinforce", "[android][tier][combined]")
{
    REQUIRE(classifyAndroidTier("Adreno 740", kHighRam) == AndroidTier::High);
    REQUIRE(classifyAndroidTier("Mali-G710", kHighRam) == AndroidTier::High);
    REQUIRE(classifyAndroidTier("Adreno 640", kMidRam) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("Mali-G57", kMidRam) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("Adreno 530", kLowRam) == AndroidTier::Low);
    REQUIRE(classifyAndroidTier("Mali-T880", kLowRam) == AndroidTier::Low);
}

TEST_CASE("classifyAndroidTier — disagreeing signals fall back to Mid", "[android][tier][combined]")
{
    // Flagship GPU + budget RAM: maybe a misread, maybe a quirky device.
    // Don't punish either way — Mid is conservative.
    REQUIRE(classifyAndroidTier("Adreno 740", kLowRam) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("Mali-G710", kLowRam) == AndroidTier::Mid);

    // Weak GPU + plenty of RAM: maybe a tablet with old SoC.
    REQUIRE(classifyAndroidTier("Adreno 530", kHighRam) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("Mali-T880", kHighRam) == AndroidTier::Mid);
}

TEST_CASE("classifyAndroidTier — empty GPU uses RAM only", "[android][tier][combined]")
{
    REQUIRE(classifyAndroidTier("", kLowRam) == AndroidTier::Low);
    REQUIRE(classifyAndroidTier("", kMidRam) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("", kHighRam) == AndroidTier::High);

    // RAM threshold edges.
    REQUIRE(classifyAndroidTier("", 3071) == AndroidTier::Low);
    REQUIRE(classifyAndroidTier("", 3072) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("", 5119) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("", 5120) == AndroidTier::High);
}

TEST_CASE("classifyAndroidTier — RAM=0 with known GPU uses GPU class", "[android][tier][combined]")
{
    REQUIRE(classifyAndroidTier("Adreno 740", 0) == AndroidTier::High);
    REQUIRE(classifyAndroidTier("Adreno 640", 0) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("Adreno 530", 0) == AndroidTier::Low);
    REQUIRE(classifyAndroidTier("Mali-G710", 0) == AndroidTier::High);
    REQUIRE(classifyAndroidTier("Mali-G57", 0) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("Mali-T880", 0) == AndroidTier::Low);
}

TEST_CASE("classifyAndroidTier — no signal returns Unknown", "[android][tier][combined]")
{
    // Empty GPU + RAM=0 — caller maps Unknown -> "mid" via
    // androidTierToProjectConfigName.
    REQUIRE(classifyAndroidTier("", 0) == AndroidTier::Unknown);
    REQUIRE(classifyAndroidTier("Some Future GPU", 0) == AndroidTier::Unknown);
}

TEST_CASE("classifyAndroidTier — unknown GPU uses RAM bucket", "[android][tier][combined]")
{
    REQUIRE(classifyAndroidTier("Adreno 999", kLowRam) == AndroidTier::Low);
    REQUIRE(classifyAndroidTier("Adreno 999", kMidRam) == AndroidTier::Mid);
    REQUIRE(classifyAndroidTier("Adreno 999", kHighRam) == AndroidTier::High);
    REQUIRE(classifyAndroidTier("VideoCore IV", kHighRam) == AndroidTier::High);
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

TEST_CASE("androidTierLogName — covers all enum values", "[android][tier][strings]")
{
    REQUIRE(std::string(androidTierLogName(AndroidTier::Low)) == "Low");
    REQUIRE(std::string(androidTierLogName(AndroidTier::Mid)) == "Mid");
    REQUIRE(std::string(androidTierLogName(AndroidTier::High)) == "High");
    REQUIRE(std::string(androidTierLogName(AndroidTier::Unknown)) == "Unknown");
}

TEST_CASE("androidTierToProjectConfigName — Unknown maps to mid", "[android][tier][projectconfig]")
{
    REQUIRE(std::string(androidTierToProjectConfigName(AndroidTier::Low)) == "low");
    REQUIRE(std::string(androidTierToProjectConfigName(AndroidTier::Mid)) == "mid");
    REQUIRE(std::string(androidTierToProjectConfigName(AndroidTier::High)) == "high");
    // Unknown -> "mid" (safe default; documented in NOTES.md).
    REQUIRE(std::string(androidTierToProjectConfigName(AndroidTier::Unknown)) == "mid");
}

// ---------------------------------------------------------------------------
// Smoke test on host
// ---------------------------------------------------------------------------

TEST_CASE("detectAndroidTier / androidTotalRamMb — host smoke test", "[android][tier][host]")
{
    // On macOS host /proc/meminfo doesn't exist, so RAM lookup returns 0
    // and the empty-default GPU name returns Unknown.  The detector must
    // still complete cleanly and return a valid enum value.
    const AndroidTier tier = detectAndroidTier();
    REQUIRE((tier == AndroidTier::Low || tier == AndroidTier::Mid || tier == AndroidTier::High ||
             tier == AndroidTier::Unknown));

    // androidTotalRamMb on macOS host: /proc/meminfo doesn't exist -> 0.
    // (We can't REQUIRE == 0 because someone might mount a fake procfs.)
    const uint64_t ram = androidTotalRamMb();
    REQUIRE(ram >= 0);  // tautology — just exercises the call path
}
