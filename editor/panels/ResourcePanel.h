#pragma once

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>

#include "engine/memory/InlinedVector.h"

namespace engine::memory
{
class FrameArena;
}

namespace engine::ecs
{
class Registry;
}

namespace engine::editor
{

// ---------------------------------------------------------------------------
// ResourceStats -- collected once per frame, consumed by the resource panel.
// ---------------------------------------------------------------------------

struct ResourceStats
{
    // CPU
    float frameTimeMs = 0.0f;
    float fps = 0.0f;

    // GPU (from bgfx::getStats)
    uint32_t drawCalls = 0;
    uint32_t numTriangles = 0;
    int64_t textureMemUsed = 0;
    int64_t rtMemUsed = 0;
    int64_t numPrograms = 0;
    int64_t numTextures = 0;

    // Memory
    size_t arenaUsed = 0;
    size_t arenaCapacity = 0;

    // ECS
    uint32_t entityCount = 0;
};

// ---------------------------------------------------------------------------
// ResourcePanel -- collects and stores rolling history of engine stats.
//
// Ring buffers hold 120 samples (2 seconds at 60fps).
// ---------------------------------------------------------------------------

class ResourcePanel
{
public:
    static constexpr size_t kHistorySize = 120;

    ResourcePanel() = default;
    ~ResourcePanel() = default;

    // Collect stats for this frame.
    void update(float dt, const ecs::Registry& registry, const memory::FrameArena* arena);

    // Latest snapshot.
    [[nodiscard]] const ResourceStats& currentStats() const
    {
        return current_;
    }

    // Rolling frame time history.
    [[nodiscard]] const memory::InlinedVector<float, kHistorySize>& frameTimeHistory() const
    {
        return frameTimeHistory_;
    }

    [[nodiscard]] const memory::InlinedVector<float, kHistorySize>& drawCallHistory() const
    {
        return drawCallHistory_;
    }

private:
    ResourceStats current_{};

    // Ring buffers -- push_back and rotate when full.
    memory::InlinedVector<float, kHistorySize> frameTimeHistory_;
    memory::InlinedVector<float, kHistorySize> drawCallHistory_;

    void pushHistory(memory::InlinedVector<float, kHistorySize>& buf, float value);
};

}  // namespace engine::editor
