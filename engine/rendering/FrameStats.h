#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// FrameStats — per-frame CPU/GPU performance counters, bgfx-free.
//
// Wraps bgfx::getStats() so games and debug overlays can render a perf HUD
// without depending on <bgfx/bgfx.h>.  All units are converted at the
// boundary: timer ticks → milliseconds, bytes → megabytes.
// ---------------------------------------------------------------------------

struct PassStats
{
    std::string_view name;  // bgfx view label set via RenderPass::name()
    float cpuMs;
    float gpuMs;
    bool gpuValid;  // false when the backend reports a wraparound / negative
                    // reading (e.g. a view's framebuffer was reconfigured
                    // mid-frame and the begin/end timestamps are inconsistent).
};

struct FrameStats
{
    float cpuMs;        // whole-frame CPU submit cost
    float gpuMs;        // whole-frame GPU cost
    unsigned numDraw;   // total draw calls
    unsigned numPrims;  // total triangles
    unsigned textureMemoryMB;
    unsigned rtMemoryMB;

    // Per-view stats.  Lifetime: valid only until the next sampleFrameStats()
    // call on the same thread — the span aliases an internal thread-local
    // buffer that is reused on every sample.
    std::span<const PassStats> passes;
};

// Enable bgfx's internal per-view profiler.  Per-view stats
// (PassStats::cpuMs / gpuMs) are not collected until this is called.
//
// Idempotent: cheap to call repeatedly; the underlying bgfx::setDebug call
// is suppressed when the requested state matches the last-set state.
void enableGpuProfiler(bool on = true);

// Sample the current frame's stats.  Wraps bgfx::getStats() and converts
// timer ticks → milliseconds, bytes → megabytes.  Does NOT allocate; the
// returned PassStats span aliases an internal thread-local buffer that is
// overwritten on every call.
//
// Pass entries with empty names (uninitialised views) are filtered out.
[[nodiscard]] FrameStats sampleFrameStats();

}  // namespace engine::rendering
