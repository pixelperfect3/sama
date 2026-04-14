#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace engine::animation
{

// A single fired event record, stored per-entity each frame.
struct AnimationEventRecord
{
    uint32_t nameHash;  // FNV-1a hash of the event name
    float time;         // clip-local time at which the event was placed
};

// Per-entity queue of animation events that fired this frame.
// AnimationSystem fills this each frame; game code polls and clears it.
struct AnimationEventQueue
{
    std::vector<AnimationEventRecord> events;

    // Check whether an event with the given name hash fired this frame.
    [[nodiscard]] bool has(uint32_t nameHash) const
    {
        return std::any_of(events.begin(), events.end(), [nameHash](const AnimationEventRecord& r)
                           { return r.nameHash == nameHash; });
    }

    void clear()
    {
        events.clear();
    }
};

}  // namespace engine::animation
