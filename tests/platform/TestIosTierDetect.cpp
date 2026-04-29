#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "engine/platform/ios/IosTierDetect.h"

// ---------------------------------------------------------------------------
// TestIosTierDetect — runs on the macOS host build.
//
// We exercise the testable seam `classifyIosTier(identifier, ramBytes)` so
// the test does not depend on the actual `sysctlbyname` value of the
// machine running the test.  The on-device public functions
// (`detectIosTier()`, `iosMachineIdentifier()`) are smoke-checked at the
// end — they must compile and return something sensible on the host.
// ---------------------------------------------------------------------------

using engine::platform::ios::classifyIosTier;
using engine::platform::ios::detectIosTier;
using engine::platform::ios::iosMachineIdentifier;
using engine::platform::ios::IosTier;

namespace
{
constexpr uint64_t kOneGB = 1024ull * 1024ull * 1024ull;

// A plausible amount of RAM for each tier — passed alongside identifier
// lookups so the RAM heuristic does not silently rescue a misclassified
// table entry.  These are chosen to be unambiguous in the heuristic
// (< 3 GB, in [3,6) GB, >= 6 GB).
constexpr uint64_t kLowRam = 2 * kOneGB;
constexpr uint64_t kMidRam = 4 * kOneGB;
constexpr uint64_t kHighRam = 8 * kOneGB;
}  // namespace

TEST_CASE("classifyIosTier — Low tier devices map correctly", "[ios][tier]")
{
    // A10 family (iPhone 7, iPad 6, iPod touch 7).
    REQUIRE(classifyIosTier("iPhone9,1", kLowRam) == IosTier::Low);
    REQUIRE(classifyIosTier("iPhone9,2", kLowRam) == IosTier::Low);
    REQUIRE(classifyIosTier("iPhone9,3", kLowRam) == IosTier::Low);
    REQUIRE(classifyIosTier("iPhone9,4", kLowRam) == IosTier::Low);
    REQUIRE(classifyIosTier("iPad7,5", kLowRam) == IosTier::Low);
    REQUIRE(classifyIosTier("iPad7,6", kLowRam) == IosTier::Low);
    REQUIRE(classifyIosTier("iPod9,1", kLowRam) == IosTier::Low);

    // iPhone SE 2nd gen — A13 chip but small body, doc puts it Low.
    REQUIRE(classifyIosTier("iPhone12,8", kLowRam) == IosTier::Low);
}

TEST_CASE("classifyIosTier — Mid tier devices map correctly", "[ios][tier]")
{
    // iPhone 11 family (A13).
    REQUIRE(classifyIosTier("iPhone12,1", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone12,3", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone12,5", kMidRam) == IosTier::Mid);

    // iPhone 12 family (A14).
    REQUIRE(classifyIosTier("iPhone13,1", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone13,2", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone13,3", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone13,4", kMidRam) == IosTier::Mid);

    // iPhone 13 / 14 family (A15).
    REQUIRE(classifyIosTier("iPhone14,4", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone14,5", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone14,2", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone14,3", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone14,6", kMidRam) == IosTier::Mid);  // SE 3
    REQUIRE(classifyIosTier("iPhone14,7", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone14,8", kMidRam) == IosTier::Mid);

    // iPad Air 4 (A14), iPad 9 (A13).
    REQUIRE(classifyIosTier("iPad13,1", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPad13,2", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPad12,1", kMidRam) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPad12,2", kMidRam) == IosTier::Mid);
}

TEST_CASE("classifyIosTier — High tier devices map correctly", "[ios][tier]")
{
    // A16 — iPhone 14 Pro family + iPhone 15 / 15 Plus.
    REQUIRE(classifyIosTier("iPhone15,2", kHighRam) == IosTier::High);
    REQUIRE(classifyIosTier("iPhone15,3", kHighRam) == IosTier::High);
    REQUIRE(classifyIosTier("iPhone15,4", kHighRam) == IosTier::High);
    REQUIRE(classifyIosTier("iPhone15,5", kHighRam) == IosTier::High);

    // A17 Pro — iPhone 15 Pro / Pro Max, iPad mini 7.
    REQUIRE(classifyIosTier("iPhone16,1", kHighRam) == IosTier::High);
    REQUIRE(classifyIosTier("iPhone16,2", kHighRam) == IosTier::High);
    REQUIRE(classifyIosTier("iPad16,1", kHighRam) == IosTier::High);
    REQUIRE(classifyIosTier("iPad16,2", kHighRam) == IosTier::High);

    // A18 / A18 Pro — iPhone 16 family.
    REQUIRE(classifyIosTier("iPhone17,1", kHighRam) == IosTier::High);
    REQUIRE(classifyIosTier("iPhone17,2", kHighRam) == IosTier::High);
    REQUIRE(classifyIosTier("iPhone17,3", kHighRam) == IosTier::High);
    REQUIRE(classifyIosTier("iPhone17,4", kHighRam) == IosTier::High);

    // M-series iPads.
    REQUIRE(classifyIosTier("iPad13,8", kHighRam) == IosTier::High);   // M1 Pro 12.9
    REQUIRE(classifyIosTier("iPad13,16", kHighRam) == IosTier::High);  // M1 Air
    REQUIRE(classifyIosTier("iPad14,3", kHighRam) == IosTier::High);   // M2 Pro 11
    REQUIRE(classifyIosTier("iPad14,8", kHighRam) == IosTier::High);   // M2 Air 11
    REQUIRE(classifyIosTier("iPad16,5", kHighRam) == IosTier::High);   // M4 Pro 13
}

TEST_CASE("classifyIosTier — table beats RAM signal", "[ios][tier]")
{
    // Lookup must take precedence over the RAM heuristic — even if a
    // pranked simulator reports unrealistic RAM, the device tier comes
    // from the table.  This is the regression we'd see if the
    // implementation accidentally fell through to the RAM branch.
    REQUIRE(classifyIosTier("iPhone9,1", kHighRam) == IosTier::Low);
    REQUIRE(classifyIosTier("iPhone15,2", kLowRam) == IosTier::High);
}

TEST_CASE("classifyIosTier — unknown identifier falls back to RAM heuristic",
          "[ios][tier][fallback]")
{
    // Future iPhone 18 / chip we don't know about — RAM tells us where
    // it lives.  These exercise the < 3 / 3-6 / >= 6 GB branches.
    REQUIRE(classifyIosTier("iPhone99,1", 1 * kOneGB) == IosTier::Low);
    REQUIRE(classifyIosTier("iPhone99,1", 2 * kOneGB) == IosTier::Low);
    REQUIRE(classifyIosTier("iPhone99,1", 3 * kOneGB) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone99,1", 4 * kOneGB) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhone99,1", 6 * kOneGB) == IosTier::High);
    REQUIRE(classifyIosTier("iPhone99,1", 16 * kOneGB) == IosTier::High);

    // Same fallback for a future iPad family.
    REQUIRE(classifyIosTier("iPad99,1", 8 * kOneGB) == IosTier::High);
}

TEST_CASE("classifyIosTier — empty / null identifier uses RAM only", "[ios][tier][fallback]")
{
    REQUIRE(classifyIosTier(nullptr, 2 * kOneGB) == IosTier::Low);
    REQUIRE(classifyIosTier(nullptr, 4 * kOneGB) == IosTier::Mid);
    REQUIRE(classifyIosTier(nullptr, 8 * kOneGB) == IosTier::High);

    REQUIRE(classifyIosTier("", 2 * kOneGB) == IosTier::Low);
    REQUIRE(classifyIosTier("", 8 * kOneGB) == IosTier::High);
}

TEST_CASE("classifyIosTier — zero RAM with unknown identifier defaults to Mid",
          "[ios][tier][fallback]")
{
    // Worst-case "no signal at all" — pick the safe middle so the engine
    // boots with reasonable defaults rather than guessing High and then
    // dropping frames on what might really be a Low device.
    REQUIRE(classifyIosTier(nullptr, 0) == IosTier::Mid);
    REQUIRE(classifyIosTier("iPhoneFromTheFuture", 0) == IosTier::Mid);
}

TEST_CASE("classifyIosTier — simulator identifiers map to High", "[ios][tier][simulator]")
{
    // Apple Silicon Mac running Simulator → "arm64".
    REQUIRE(classifyIosTier("arm64", 0) == IosTier::High);
    REQUIRE(classifyIosTier("arm64", 16 * kOneGB) == IosTier::High);

    // Intel Mac running Simulator → "x86_64".
    REQUIRE(classifyIosTier("x86_64", 0) == IosTier::High);
    REQUIRE(classifyIosTier("x86_64", 8 * kOneGB) == IosTier::High);

    // Long-form variants ("arm64e" — used on real A12+ in some kernel
    // configurations, but bare "arm64" is the Simulator value).  The
    // prefix-match accepts both, and that's by design — running on a
    // future arm64-variant Mac shouldn't accidentally drop the dev to
    // Low/Mid.
    REQUIRE(classifyIosTier("arm64e", 0) == IosTier::High);
}

TEST_CASE("detectIosTier / iosMachineIdentifier — smoke test on host", "[ios][tier][host]")
{
    // We can't predict the host's machine identifier (it could be a Mac
    // running tests in a CI box), but we can require the API doesn't
    // crash and returns something usable.
    const char* ident = iosMachineIdentifier();
    REQUIRE(ident != nullptr);  // contract: never returns NULL

    const IosTier tier = detectIosTier();
    // On a real Mac host (arm64 or x86_64) the simulator branch fires
    // and we get High.  On the off chance someone runs this test under a
    // foreign environment the value must still be a valid enum member.
    REQUIRE((tier == IosTier::Low || tier == IosTier::Mid || tier == IosTier::High ||
             tier == IosTier::Unknown));
}
