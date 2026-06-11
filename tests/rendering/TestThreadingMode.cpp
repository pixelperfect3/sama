// Threading-mode plumbing tests.
//
// Covers the API contract added when bgfx default mode was flipped from
// single-threaded to multi-threaded:
//
//   * EngineDesc::singleThreaded defaults to false (multi-threaded).
//   * RendererDesc::singleThreaded defaults to false (multi-threaded).
//   * Renderer::init accepts both values in headless mode (the only mode
//     unit tests can exercise — Noop renderer skips the bgfx::renderFrame
//     guard regardless).
//
// What we can NOT unit-test: the actual bgfx::renderFrame() call itself.
// That requires a real GPU context and would conflict with the per-process
// init/shutdown contract of BgfxContext.  The integration coverage lives
// in apps/perf_smoke (see apps/perf_smoke/run_both.sh): comparing the
// bgfx::frame() row between the two modes confirms the flag actually flips
// the threading model.

#include <catch2/catch_test_macros.hpp>

#include "engine/core/Engine.h"
#include "engine/rendering/Renderer.h"

TEST_CASE("EngineDesc::singleThreaded defaults to false", "[threading]")
{
    // Multi-threaded by default — game code does not have to know about
    // the bgfx threading model to get the lower-latency game thread.
    engine::core::EngineDesc desc{};
    REQUIRE(desc.singleThreaded == false);
}

TEST_CASE("RendererDesc::singleThreaded defaults to false", "[threading]")
{
    engine::rendering::RendererDesc desc{};
    REQUIRE(desc.singleThreaded == false);
}

TEST_CASE("Renderer headless init succeeds with singleThreaded=true", "[threading]")
{
    // Headless / Noop renderer ignores the threading flag (the
    // bgfx::renderFrame() pre-init call is gated on !desc.headless), so
    // both values must be acceptable on this path — this is what lets
    // BgfxContext stay single-threaded while the per-process default flips.
    engine::rendering::Renderer r;
    engine::rendering::RendererDesc desc{};
    desc.headless = true;
    desc.width = 320;
    desc.height = 240;
    desc.singleThreaded = true;
    REQUIRE(r.init(desc));
    r.beginFrame();
    r.endFrame();
    r.shutdown();
}

TEST_CASE("Renderer headless init succeeds with singleThreaded=false", "[threading]")
{
    engine::rendering::Renderer r;
    engine::rendering::RendererDesc desc{};
    desc.headless = true;
    desc.width = 320;
    desc.height = 240;
    desc.singleThreaded = false;
    REQUIRE(r.init(desc));
    r.beginFrame();
    r.endFrame();
    r.shutdown();
}

// ---------------------------------------------------------------------------
// EngineDesc::enableGyro opt-in plumbing.  See docs/PERF_AUDIT_2026-05-25.md
// item #P1.  The gyro / accelerometer pair burns ~5-10 mW continuously even
// when the game never reads it, so the default has to be off and games must
// opt in explicitly.  These tests pin the default and the writable contract
// so a future refactor that flipped the default back to true would fail
// loudly here instead of silently regressing battery on low-tier phones.
//
// The actual sensor-enable path lives in Engine::initAndroid (gated on
// EngineDesc::enableGyro) and APP_CMD_RESUME (gated on the latched
// gyroOptedIn_ member).  We can't exercise that path from a desktop unit
// test because AndroidGyro is compiled out via #ifdef __ANDROID__ — the
// integration coverage lives in apps/android_test, which sets the flag
// explicitly.
// ---------------------------------------------------------------------------

TEST_CASE("EngineDesc::enableGyro defaults to false (opt-in)", "[gyro][engine-desc]")
{
    engine::core::EngineDesc desc{};
    REQUIRE(desc.enableGyro == false);
}

TEST_CASE("EngineDesc::enableGyro can be set to true", "[gyro][engine-desc]")
{
    engine::core::EngineDesc desc{};
    desc.enableGyro = true;
    REQUIRE(desc.enableGyro == true);
}
