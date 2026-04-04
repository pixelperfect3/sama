#include "editor/panels/ResourcePanel.h"

#include <bgfx/bgfx.h>

#include "engine/ecs/Registry.h"
#include "engine/memory/FrameArena.h"

namespace engine::editor
{

void ResourcePanel::update(float dt, const ecs::Registry& registry, const memory::FrameArena* arena)
{
    // CPU timing.
    current_.frameTimeMs = dt * 1000.0f;
    current_.fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;

    // GPU stats from bgfx.
    const bgfx::Stats* stats = bgfx::getStats();
    if (stats)
    {
        current_.drawCalls = stats->numDraw;
        current_.numTriangles = stats->numPrims[bgfx::Topology::TriList];
        current_.textureMemUsed = stats->textureMemoryUsed;
        current_.rtMemUsed = stats->rtMemoryUsed;
        current_.numPrograms = stats->numPrograms;
        current_.numTextures = stats->numTextures;
    }

    // Frame arena.
    if (arena)
    {
        current_.arenaUsed = arena->bytesUsed();
        current_.arenaCapacity = arena->capacity();
    }

    // Entity count.
    uint32_t count = 0;
    registry.forEachEntity([&](ecs::EntityID) { ++count; });
    current_.entityCount = count;

    // Update history ring buffers.
    pushHistory(frameTimeHistory_, current_.frameTimeMs);
    pushHistory(drawCallHistory_, static_cast<float>(current_.drawCalls));
}

void ResourcePanel::pushHistory(memory::InlinedVector<float, kHistorySize>& buf, float value)
{
    if (buf.size() >= kHistorySize)
    {
        // Shift left by one.
        for (size_t i = 1; i < buf.size(); ++i)
        {
            buf[i - 1] = buf[i];
        }
        buf[buf.size() - 1] = value;
    }
    else
    {
        buf.push_back(value);
    }
}

}  // namespace engine::editor
