#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::animation
{

// FNV-1a hash for parameter names.
inline uint32_t fnv1aHash(std::string_view str)
{
    uint32_t hash = 2166136261u;
    for (char c : str)
    {
        hash ^= static_cast<uint32_t>(static_cast<uint8_t>(c));
        hash *= 16777619u;
    }
    return hash;
}

// A condition that can trigger a transition.
struct TransitionCondition
{
    uint32_t paramHash = 0;  // FNV-1a hash of parameter name
    std::string paramName;   // human-readable parameter name (for serialization)

    enum class Compare : uint8_t
    {
        Greater,
        Less,
        Equal,
        NotEqual,
        BoolTrue,
        BoolFalse
    };
    Compare compare = Compare::BoolTrue;
    float threshold = 0.0f;  // for numeric comparisons
};

// A transition between two states.
struct StateTransition
{
    uint32_t targetState = 0;    // index into states array
    float blendDuration = 0.2f;  // crossfade time in seconds
    float exitTime = 0.0f;       // normalized time (0..1) when transition is allowed
    bool hasExitTime = false;    // if true, wait for exitTime before transitioning
    std::vector<TransitionCondition> conditions;  // ALL must be true to transition
};

// A single state in the state machine.
struct AnimState
{
    std::string name;
    uint32_t nameHash = 0;                     // FNV-1a hash
    uint32_t clipId = UINT32_MAX;              // animation clip to play
    float speed = 1.0f;                        // playback speed for this state
    bool loop = true;                          // whether this state loops
    std::vector<StateTransition> transitions;  // evaluated in order, first match wins
};

// The full state machine definition (shared, not per-entity).
struct AnimStateMachine
{
    std::vector<AnimState> states;
    uint32_t defaultState = 0;  // index of initial state

    // Parameter name registry: hash -> original name string.
    // Populated automatically by addTransition() so the editor can display
    // human-readable parameter names.
    std::unordered_map<uint32_t, std::string> paramNames;

    // Builder API
    uint32_t addState(const std::string& name, uint32_t clipId, bool loop = true,
                      float speed = 1.0f);

    void addTransition(uint32_t fromState, uint32_t toState, float blendDuration = 0.2f);

    void addTransition(uint32_t fromState, uint32_t toState, float blendDuration,
                       const std::string& param, TransitionCondition::Compare compare,
                       float threshold = 0.0f);

    // Transition with exit time
    void addTransitionWithExitTime(uint32_t fromState, uint32_t toState, float blendDuration,
                                   float exitTime);
};

// Per-entity runtime state.
struct AnimStateMachineComponent
{
    const AnimStateMachine* machine = nullptr;  // shared definition (not owned)
    uint32_t currentState = 0;

    // Parameter storage — flat map for speed.
    // Game code sets parameters; the system evaluates conditions against them.
    std::unordered_map<uint32_t, float> params;  // paramHash -> value

    void setFloat(std::string_view name, float value);
    void setBool(std::string_view name, bool value);  // stored as 1.0/0.0
    float getFloat(std::string_view name) const;
    bool getBool(std::string_view name) const;
};

}  // namespace engine::animation
