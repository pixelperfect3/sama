#include <catch2/catch_test_macros.hpp>

#include "engine/rendering/FrameStats.h"
#include "engine/rendering/Renderer.h"

// ---------------------------------------------------------------------------
// FrameStats — smoke tests against the headless (Noop) renderer.
//
// The Noop backend cannot produce realistic GPU timings (everything is 0),
// but it does expose a valid bgfx::Stats* with sane CPU counters and the
// view-name table populated by Renderer::setupDefaultViewNames().  That is
// enough to verify the wrapper does not crash, converts units sanely, and
// returns the same internal-buffer span on repeated calls.
// ---------------------------------------------------------------------------

namespace
{

struct HeadlessRenderer
{
    engine::rendering::Renderer r;

    HeadlessRenderer()
    {
        engine::rendering::RendererDesc desc{};
        desc.headless = true;
        desc.width = 320;
        desc.height = 240;
        REQUIRE(r.init(desc));
    }

    ~HeadlessRenderer()
    {
        r.shutdown();
    }

    void pumpFrame()
    {
        r.beginFrame();
        r.endFrame();
    }
};

}  // namespace

TEST_CASE("FrameStats: enableGpuProfiler is idempotent", "[frame_stats]")
{
    HeadlessRenderer h;

    // Calling repeatedly with the same value should not crash or
    // observably change behaviour (the implementation no-ops the
    // underlying bgfx::setDebug).
    engine::rendering::enableGpuProfiler(true);
    engine::rendering::enableGpuProfiler(true);
    engine::rendering::enableGpuProfiler(true);
    engine::rendering::enableGpuProfiler(false);
    engine::rendering::enableGpuProfiler(false);
    engine::rendering::enableGpuProfiler(true);

    h.pumpFrame();

    const auto fs = engine::rendering::sampleFrameStats();
    // Sanity: passes span fits in our 256-entry tls buffer.
    REQUIRE(fs.passes.size() <= 256);
}

TEST_CASE("FrameStats: sampleFrameStats returns sane values", "[frame_stats]")
{
    HeadlessRenderer h;

    engine::rendering::enableGpuProfiler(true);

    // Pump a few frames so bgfx has populated stats (and view names from
    // Renderer::setupDefaultViewNames have propagated through the
    // single-threaded command buffer).
    for (int i = 0; i < 3; ++i)
        h.pumpFrame();

    const auto fs = engine::rendering::sampleFrameStats();

    // CPU stats should be non-negative (Noop backend reports tiny but
    // valid counters).  numDraw is unsigned so just sanity-check the type
    // with an inequality that always holds — what we really care about is
    // that sampling does not crash and does not return an absurd value.
    REQUIRE(fs.cpuMs >= 0.0f);
    REQUIRE(fs.gpuMs >= 0.0f);
    REQUIRE(fs.numDraw < 100000u);  // sanity bound

    // Pass span must fit in the internal buffer.
    REQUIRE(fs.passes.size() <= 256);

    // No pass should exceed 100ms on the Noop backend (it does no real GPU
    // work).  This is the same threshold the gpuValid heuristic uses.
    for (const auto& p : fs.passes)
    {
        REQUIRE(p.cpuMs < 100.0f);
        REQUIRE(!p.name.empty());  // empty-name passes are filtered out
    }
}

TEST_CASE("FrameStats: passes span aliases stable internal buffer", "[frame_stats]")
{
    HeadlessRenderer h;

    engine::rendering::enableGpuProfiler(true);
    h.pumpFrame();

    const auto fs1 = engine::rendering::sampleFrameStats();
    const auto fs2 = engine::rendering::sampleFrameStats();

    // The span data pointer must be identical across calls — the contract
    // is that the buffer is reused (and overwritten) on every sample.
    REQUIRE(fs1.passes.data() == fs2.passes.data());
}
