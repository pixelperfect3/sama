#pragma once

#include <string>

namespace engine::animation
{

class AnimationResources;
struct AnimStateMachine;

// Save all clip events to a JSON file.
// Format: { "clips": [ { "name": "Walk", "events": [...] }, ... ] }
bool saveEvents(const AnimationResources& res, const std::string& path);

// Load events from a JSON file and apply them to matching clips by name.
bool loadEvents(AnimationResources& res, const std::string& path);

// Save a state machine definition to JSON.
// AnimationResources is needed to resolve clipId -> clip name.
bool saveStateMachine(const AnimStateMachine& machine, const AnimationResources& res,
                      const std::string& path);

// Load a state machine definition from JSON.
// Clip IDs are resolved by matching clip names against AnimationResources.
bool loadStateMachine(AnimStateMachine& machine, const AnimationResources& res,
                      const std::string& path);

}  // namespace engine::animation
