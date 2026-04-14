#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/animation/Hash.h"
#include "engine/math/Types.h"

namespace engine::animation
{

// One keyframe: timestamp + value.
template <typename T>
struct Keyframe
{
    float time;  // seconds
    T value;
};

// Per-joint channel data. Empty vectors mean the joint is not animated on that channel.
struct JointChannel
{
    uint32_t jointIndex;
    std::vector<Keyframe<math::Vec3>> positions;
    std::vector<Keyframe<math::Quat>> rotations;
    std::vector<Keyframe<math::Vec3>> scales;
};

// A named event marker within an animation clip.
struct AnimationEvent
{
    float time = 0.0f;      // timestamp in seconds
    uint32_t nameHash = 0;  // FNV-1a hash of event name for fast comparison
    std::string name;       // human-readable name (e.g., "footstep_left", "spawn_projectile")
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;  // total length in seconds
    std::vector<JointChannel> channels;

    // Event markers, sorted by time. glTF does not natively support animation
    // events, so these are added programmatically or via a future editor tool.
    std::vector<AnimationEvent> events;

    // Insert an event in sorted order by time and compute its name hash.
    void addEvent(float time, const std::string& eventName)
    {
        AnimationEvent evt;
        evt.time = time;
        evt.nameHash = fnv1a(eventName.c_str());
        evt.name = eventName;

        auto it = std::lower_bound(events.begin(), events.end(), time,
                                   [](const AnimationEvent& e, float t) { return e.time < t; });
        events.insert(it, std::move(evt));
    }
};

}  // namespace engine::animation
