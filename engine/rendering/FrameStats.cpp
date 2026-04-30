#include "engine/rendering/FrameStats.h"

#include <bgfx/bgfx.h>

#include <array>

namespace engine::rendering
{

namespace
{

// Reusable buffer for per-pass stats.  Sized at bgfx's BGFX_CONFIG_MAX_VIEWS
// upper bound (256) so we never overflow even if every view is populated.
// Thread-local so two threads sampling on different bgfx contexts (rare in
// our setup, but possible in tests) do not race on the buffer.
thread_local std::array<PassStats, 256> tlsPassBuffer{};

// Track the last requested profiler state so enableGpuProfiler() is a no-op
// when nothing changed.  Initial value sentinel: -1 means "never set".
int lastProfilerState = -1;

constexpr float ticksToMs(int64_t delta, int64_t freq)
{
    if (freq <= 0)
        return 0.0f;
    return 1000.0f * static_cast<float>(delta) / static_cast<float>(freq);
}

constexpr unsigned bytesToMB(int64_t bytes)
{
    if (bytes < 0)
        return 0u;
    return static_cast<unsigned>(bytes / (1024 * 1024));
}

}  // namespace

void enableGpuProfiler(bool on)
{
    const int desired = on ? 1 : 0;
    if (lastProfilerState == desired)
        return;
    lastProfilerState = desired;
    bgfx::setDebug(on ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
}

FrameStats sampleFrameStats()
{
    FrameStats out{};

    const bgfx::Stats* s = bgfx::getStats();
    if (s == nullptr)
    {
        out.passes = std::span<const PassStats>(tlsPassBuffer.data(), 0);
        return out;
    }

    const int64_t cpuFreq = s->cpuTimerFreq;
    const int64_t gpuFreq = s->gpuTimerFreq;

    out.cpuMs = ticksToMs(s->cpuTimeEnd - s->cpuTimeBegin, cpuFreq);
    out.gpuMs = ticksToMs(s->gpuTimeEnd - s->gpuTimeBegin, gpuFreq);
    out.numDraw = static_cast<unsigned>(s->numDraw);
    out.numPrims = static_cast<unsigned>(s->numPrims[0]);  // triangles
    out.textureMemoryMB = bytesToMB(s->textureMemoryUsed);
    out.rtMemoryMB = bytesToMB(s->rtMemoryUsed);

    const uint16_t numViews = s->numViews;
    size_t outIdx = 0;
    for (uint16_t i = 0; i < numViews && outIdx < tlsPassBuffer.size(); ++i)
    {
        const bgfx::ViewStats& v = s->viewStats[i];

        // Skip uninitialised / unlabelled views — matches existing game-side
        // perf-overlay behaviour and keeps the output focused on real passes.
        if (v.name[0] == '\0')
            continue;

        const float passCpuMs = ticksToMs(v.cpuTimeEnd - v.cpuTimeBegin, cpuFreq);
        const float passGpuMs = ticksToMs(v.gpuTimeEnd - v.gpuTimeBegin, gpuFreq);

        // Heuristic: when a view's framebuffer is reconfigured mid-frame
        // (e.g. Renderer::beginFrameDirect swapping kViewOpaque between
        // scene-FB and backbuffer) bgfx's begin/end timestamps come from
        // inconsistent contexts and produce nonsense readings.  Negative
        // or absurdly-large values are flagged invalid so consumers can
        // skip them rather than printing garbage.
        // TODO: tunable threshold — 100ms was the empirical cutoff observed
        // in sample_game's perf overlay before this API existed.
        const bool gpuValid = passGpuMs >= 0.0f && passGpuMs < 100.0f;

        tlsPassBuffer[outIdx] = PassStats{
            std::string_view(v.name),
            passCpuMs,
            passGpuMs,
            gpuValid,
        };
        ++outIdx;
    }

    out.passes = std::span<const PassStats>(tlsPassBuffer.data(), outIdx);
    return out;
}

}  // namespace engine::rendering
