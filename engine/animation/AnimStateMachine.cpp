#include "engine/animation/AnimStateMachine.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// AnimStateMachine builder API
// ---------------------------------------------------------------------------

uint32_t AnimStateMachine::addState(const std::string& name, uint32_t clipId, bool loop,
                                    float speed)
{
    uint32_t idx = static_cast<uint32_t>(states.size());
    AnimState state;
    state.name = name;
    state.nameHash = fnv1aHash(name);
    state.clipId = clipId;
    state.speed = speed;
    state.loop = loop;
    states.push_back(std::move(state));
    return idx;
}

void AnimStateMachine::addTransition(uint32_t fromState, uint32_t toState, float blendDuration)
{
    if (fromState >= states.size())
        return;

    StateTransition t;
    t.targetState = toState;
    t.blendDuration = blendDuration;
    states[fromState].transitions.push_back(std::move(t));
}

void AnimStateMachine::addTransition(uint32_t fromState, uint32_t toState, float blendDuration,
                                     const std::string& param, TransitionCondition::Compare compare,
                                     float threshold)
{
    if (fromState >= states.size())
        return;

    TransitionCondition cond;
    cond.paramHash = fnv1aHash(param);
    cond.compare = compare;
    cond.threshold = threshold;

    StateTransition t;
    t.targetState = toState;
    t.blendDuration = blendDuration;
    t.conditions.push_back(cond);
    states[fromState].transitions.push_back(std::move(t));
}

void AnimStateMachine::addTransitionWithExitTime(uint32_t fromState, uint32_t toState,
                                                 float blendDuration, float exitTime)
{
    if (fromState >= states.size())
        return;

    StateTransition t;
    t.targetState = toState;
    t.blendDuration = blendDuration;
    t.exitTime = exitTime;
    t.hasExitTime = true;
    states[fromState].transitions.push_back(std::move(t));
}

// ---------------------------------------------------------------------------
// AnimStateMachineComponent parameter accessors
// ---------------------------------------------------------------------------

void AnimStateMachineComponent::setFloat(const std::string& name, float value)
{
    params[fnv1aHash(name)] = value;
}

void AnimStateMachineComponent::setBool(const std::string& name, bool value)
{
    params[fnv1aHash(name)] = value ? 1.0f : 0.0f;
}

float AnimStateMachineComponent::getFloat(const std::string& name) const
{
    auto it = params.find(fnv1aHash(name));
    if (it != params.end())
        return it->second;
    return 0.0f;
}

bool AnimStateMachineComponent::getBool(const std::string& name) const
{
    auto it = params.find(fnv1aHash(name));
    if (it != params.end())
        return it->second != 0.0f;
    return false;
}

}  // namespace engine::animation
