#include "engine/animation/AnimStateMachineSystem.h"

#include "engine/animation/AnimStateMachine.h"
#include "engine/animation/AnimationComponents.h"

namespace engine::animation
{

namespace
{

bool evaluateCondition(const TransitionCondition& cond,
                       const std::unordered_map<uint32_t, float>& params)
{
    float value = 0.0f;
    auto it = params.find(cond.paramHash);
    if (it != params.end())
        value = it->second;

    switch (cond.compare)
    {
        case TransitionCondition::Compare::Greater:
            return value > cond.threshold;
        case TransitionCondition::Compare::Less:
            return value < cond.threshold;
        case TransitionCondition::Compare::Equal:
            return value == cond.threshold;
        case TransitionCondition::Compare::NotEqual:
            return value != cond.threshold;
        case TransitionCondition::Compare::BoolTrue:
            return value != 0.0f;
        case TransitionCondition::Compare::BoolFalse:
            return value == 0.0f;
    }
    return false;
}

bool evaluateTransition(const StateTransition& transition,
                        const std::unordered_map<uint32_t, float>& params, float playbackTime,
                        float clipDuration)
{
    // Check exit time constraint
    if (transition.hasExitTime && clipDuration > 0.0f)
    {
        float normalizedTime = playbackTime / clipDuration;
        if (normalizedTime < transition.exitTime)
            return false;
    }

    // All conditions must be true (AND logic)
    for (const auto& cond : transition.conditions)
    {
        if (!evaluateCondition(cond, params))
            return false;
    }

    return true;
}

}  // anonymous namespace

void AnimStateMachineSystem::update(ecs::Registry& reg, float dt, const AnimationResources& animRes)
{
    auto view = reg.view<AnimStateMachineComponent, AnimatorComponent>();

    view.each(
        [&](ecs::EntityID /*entity*/, AnimStateMachineComponent& smComp,
            AnimatorComponent& animComp)
        {
            const AnimStateMachine* machine = smComp.machine;
            if (!machine || machine->states.empty())
                return;

            // If a blend just completed (AnimationSystem promoted nextClipId to
            // clipId and cleared kFlagBlending), the currentState was already
            // updated when we initiated the transition, so nothing extra needed.

            // Don't evaluate new transitions while a blend is in progress.
            if (animComp.flags & AnimatorComponent::kFlagBlending)
                return;

            const AnimState& currentState = machine->states[smComp.currentState];

            // Get clip duration for exit time checks
            float clipDuration = 0.0f;
            const AnimationClip* clip = animRes.getClip(currentState.clipId);
            if (clip)
                clipDuration = clip->duration;

            // Evaluate transitions in order; first match wins.
            for (const auto& transition : currentState.transitions)
            {
                if (transition.targetState >= machine->states.size())
                    continue;

                if (!evaluateTransition(transition, smComp.params, animComp.playbackTime,
                                        clipDuration))
                    continue;

                // Transition matched — initiate blend.
                const AnimState& targetState = machine->states[transition.targetState];

                animComp.nextClipId = targetState.clipId;
                animComp.blendDuration = transition.blendDuration;
                animComp.blendElapsed = 0.0f;
                animComp.flags |= AnimatorComponent::kFlagBlending;

                // Update state machine state
                smComp.currentState = transition.targetState;

                // Apply target state properties
                if (targetState.loop)
                    animComp.flags |= AnimatorComponent::kFlagLooping;
                else
                    animComp.flags &= ~AnimatorComponent::kFlagLooping;

                animComp.speed = targetState.speed;

                break;  // first match wins
            }
        });
}

}  // namespace engine::animation
