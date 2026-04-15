#include <catch2/catch_test_macros.hpp>

#include "engine/animation/AnimStateMachine.h"
#include "engine/animation/AnimStateMachineSystem.h"
#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/ecs/Registry.h"

using namespace engine::animation;
using engine::ecs::EntityID;
using engine::ecs::Registry;

namespace
{

// Build minimal animation resources with two clips for testing transitions.
AnimationResources buildTestResources(uint32_t& clipIdleOut, uint32_t& clipRunOut)
{
    AnimationResources res;

    AnimationClip idle;
    idle.name = "Idle";
    idle.duration = 2.0f;
    JointChannel ch;
    ch.jointIndex = 0;
    ch.positions.push_back({0.0f, engine::math::Vec3{0, 0, 0}});
    ch.rotations.push_back({0.0f, engine::math::Quat{1, 0, 0, 0}});
    ch.scales.push_back({0.0f, engine::math::Vec3{1, 1, 1}});
    idle.channels.push_back(ch);
    clipIdleOut = res.addClip(std::move(idle));

    AnimationClip run;
    run.name = "Run";
    run.duration = 1.0f;
    JointChannel ch2;
    ch2.jointIndex = 0;
    ch2.positions.push_back({0.0f, engine::math::Vec3{1, 0, 0}});
    ch2.rotations.push_back({0.0f, engine::math::Quat{1, 0, 0, 0}});
    ch2.scales.push_back({0.0f, engine::math::Vec3{1, 1, 1}});
    run.channels.push_back(ch2);
    clipRunOut = res.addClip(std::move(run));

    return res;
}

// Create an entity with AnimatorComponent and AnimStateMachineComponent.
EntityID createAnimEntity(Registry& reg, const AnimStateMachine& machine, uint32_t clipId)
{
    EntityID e = reg.createEntity();

    AnimatorComponent anim{};
    anim.clipId = clipId;
    anim.nextClipId = UINT32_MAX;
    anim.playbackTime = 0.0f;
    anim.speed = 1.0f;
    anim.blendFactor = 0.0f;
    anim.blendDuration = 0.0f;
    anim.blendElapsed = 0.0f;
    anim.flags = AnimatorComponent::kFlagPlaying | AnimatorComponent::kFlagLooping;
    anim._pad[0] = anim._pad[1] = anim._pad[2] = 0;
    reg.emplace<AnimatorComponent>(e, anim);

    AnimStateMachineComponent smComp;
    smComp.machine = &machine;
    smComp.currentState = machine.defaultState;
    reg.emplace<AnimStateMachineComponent>(e, std::move(smComp));

    return e;
}

}  // namespace

TEST_CASE("AnimStateMachine addState and addTransition", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 1;

    AnimStateMachine sm;
    uint32_t idle = sm.addState("Idle", clipIdle, true, 1.0f);
    uint32_t run = sm.addState("Run", clipRun, true, 2.0f);

    REQUIRE(idle == 0);
    REQUIRE(run == 1);
    REQUIRE(sm.states.size() == 2);
    REQUIRE(sm.states[0].name == "Idle");
    REQUIRE(sm.states[0].clipId == clipIdle);
    REQUIRE(sm.states[0].loop == true);
    REQUIRE(sm.states[0].speed == 1.0f);
    REQUIRE(sm.states[1].name == "Run");
    REQUIRE(sm.states[1].clipId == clipRun);
    REQUIRE(sm.states[1].speed == 2.0f);
    REQUIRE(sm.states[1].nameHash == fnv1aHash("Run"));

    sm.addTransition(idle, run, 0.3f, "speed", TransitionCondition::Compare::Greater, 0.5f);
    REQUIRE(sm.states[idle].transitions.size() == 1);
    REQUIRE(sm.states[idle].transitions[0].targetState == run);
    REQUIRE(sm.states[idle].transitions[0].blendDuration == 0.3f);
    REQUIRE(sm.states[idle].transitions[0].conditions.size() == 1);
}

TEST_CASE("AnimStateMachine basic state transition", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    AnimStateMachine sm;
    uint32_t idle = sm.addState("Idle", clipIdle);
    uint32_t run = sm.addState("Run", clipRun);
    sm.addTransition(idle, run, 0.2f, "speed", TransitionCondition::Compare::Greater, 0.5f);

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);

    AnimStateMachineSystem system;

    // No transition yet — speed param is 0
    system.update(reg, 0.016f, res);
    auto* smComp = reg.get<AnimStateMachineComponent>(e);
    auto* animComp = reg.get<AnimatorComponent>(e);
    REQUIRE(smComp->currentState == idle);
    REQUIRE(animComp->nextClipId == UINT32_MAX);

    // Set speed > 0.5 — transition should trigger
    smComp->setFloat("speed", 1.0f);
    system.update(reg, 0.016f, res);
    REQUIRE(smComp->currentState == run);
    REQUIRE(animComp->nextClipId == clipRun);
    REQUIRE(animComp->flags & AnimatorComponent::kFlagBlending);
    REQUIRE(animComp->blendDuration == 0.2f);
}

TEST_CASE("AnimStateMachine condition evaluation — Greater, Less, Equal", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    SECTION("Greater")
    {
        AnimStateMachine sm;
        uint32_t s0 = sm.addState("A", clipIdle);
        uint32_t s1 = sm.addState("B", clipRun);
        sm.addTransition(s0, s1, 0.1f, "val", TransitionCondition::Compare::Greater, 5.0f);

        Registry reg;
        EntityID e = createAnimEntity(reg, sm, clipIdle);
        AnimStateMachineSystem system;

        // val = 3 — should not transition
        reg.get<AnimStateMachineComponent>(e)->setFloat("val", 3.0f);
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

        // val = 6 — should transition
        reg.get<AnimStateMachineComponent>(e)->setFloat("val", 6.0f);
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
    }

    SECTION("Less")
    {
        AnimStateMachine sm;
        uint32_t s0 = sm.addState("A", clipIdle);
        uint32_t s1 = sm.addState("B", clipRun);
        sm.addTransition(s0, s1, 0.1f, "val", TransitionCondition::Compare::Less, 5.0f);

        Registry reg;
        EntityID e = createAnimEntity(reg, sm, clipIdle);
        AnimStateMachineSystem system;

        reg.get<AnimStateMachineComponent>(e)->setFloat("val", 10.0f);
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

        reg.get<AnimStateMachineComponent>(e)->setFloat("val", 2.0f);
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
    }

    SECTION("Equal")
    {
        AnimStateMachine sm;
        uint32_t s0 = sm.addState("A", clipIdle);
        uint32_t s1 = sm.addState("B", clipRun);
        sm.addTransition(s0, s1, 0.1f, "val", TransitionCondition::Compare::Equal, 42.0f);

        Registry reg;
        EntityID e = createAnimEntity(reg, sm, clipIdle);
        AnimStateMachineSystem system;

        reg.get<AnimStateMachineComponent>(e)->setFloat("val", 41.0f);
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

        reg.get<AnimStateMachineComponent>(e)->setFloat("val", 42.0f);
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
    }
}

TEST_CASE("AnimStateMachine BoolTrue and BoolFalse conditions", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    SECTION("BoolTrue")
    {
        AnimStateMachine sm;
        uint32_t s0 = sm.addState("A", clipIdle);
        uint32_t s1 = sm.addState("B", clipRun);
        sm.addTransition(s0, s1, 0.1f, "jumping", TransitionCondition::Compare::BoolTrue);

        Registry reg;
        EntityID e = createAnimEntity(reg, sm, clipIdle);
        AnimStateMachineSystem system;

        // Default (false) — no transition
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

        // Set true — transition
        reg.get<AnimStateMachineComponent>(e)->setBool("jumping", true);
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
    }

    SECTION("BoolFalse")
    {
        AnimStateMachine sm;
        uint32_t s0 = sm.addState("A", clipIdle);
        uint32_t s1 = sm.addState("B", clipRun);
        sm.addTransition(s0, s1, 0.1f, "grounded", TransitionCondition::Compare::BoolFalse);

        Registry reg;
        EntityID e = createAnimEntity(reg, sm, clipIdle);
        AnimStateMachineSystem system;

        // grounded = true — no transition
        reg.get<AnimStateMachineComponent>(e)->setBool("grounded", true);
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

        // grounded = false — transition
        reg.get<AnimStateMachineComponent>(e)->setBool("grounded", false);
        system.update(reg, 0.016f, res);
        REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
    }
}

TEST_CASE("AnimStateMachine multiple conditions AND logic", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    AnimStateMachine sm;
    uint32_t s0 = sm.addState("A", clipIdle);
    uint32_t s1 = sm.addState("B", clipRun);

    // Manually build a transition with two conditions
    StateTransition t;
    t.targetState = s1;
    t.blendDuration = 0.15f;

    TransitionCondition c1;
    c1.paramHash = fnv1aHash("speed");
    c1.compare = TransitionCondition::Compare::Greater;
    c1.threshold = 1.0f;
    t.conditions.push_back(c1);

    TransitionCondition c2;
    c2.paramHash = fnv1aHash("grounded");
    c2.compare = TransitionCondition::Compare::BoolTrue;
    t.conditions.push_back(c2);

    sm.states[s0].transitions.push_back(std::move(t));

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);
    AnimStateMachineSystem system;

    // Only speed set — grounded is false by default
    reg.get<AnimStateMachineComponent>(e)->setFloat("speed", 5.0f);
    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

    // Only grounded set — speed not enough
    reg.get<AnimStateMachineComponent>(e)->setFloat("speed", 0.5f);
    reg.get<AnimStateMachineComponent>(e)->setBool("grounded", true);
    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

    // Both conditions met
    reg.get<AnimStateMachineComponent>(e)->setFloat("speed", 5.0f);
    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
}

TEST_CASE("AnimStateMachine transition priority — first match wins", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    // Add a third clip
    AnimationClip jump;
    jump.name = "Jump";
    jump.duration = 0.5f;
    uint32_t clipJump = res.addClip(std::move(jump));

    AnimStateMachine sm;
    uint32_t s0 = sm.addState("Idle", clipIdle);
    uint32_t s1 = sm.addState("Run", clipRun);
    uint32_t s2 = sm.addState("Jump", clipJump);

    // Both transitions can match with speed > 1, but first should win
    sm.addTransition(s0, s1, 0.2f, "speed", TransitionCondition::Compare::Greater, 1.0f);
    sm.addTransition(s0, s2, 0.2f, "speed", TransitionCondition::Compare::Greater, 1.0f);

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);
    AnimStateMachineSystem system;

    reg.get<AnimStateMachineComponent>(e)->setFloat("speed", 5.0f);
    system.update(reg, 0.016f, res);

    // First transition (to Run) should win
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
}

TEST_CASE("AnimStateMachine exit time transitions", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    AnimStateMachine sm;
    uint32_t s0 = sm.addState("Idle", clipIdle);  // clipIdle duration = 2.0
    uint32_t s1 = sm.addState("Run", clipRun);
    sm.addTransitionWithExitTime(s0, s1, 0.2f, 0.75f);  // exit at 75% of clip

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);
    AnimStateMachineSystem system;

    // playbackTime = 0 — not at 75% yet (0/2 = 0%)
    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

    // Set playbackTime to 1.0 (50% of 2.0s clip) — still not enough
    reg.get<AnimatorComponent>(e)->playbackTime = 1.0f;
    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

    // Set playbackTime to 1.6 (80% of 2.0s clip) — should transition
    reg.get<AnimatorComponent>(e)->playbackTime = 1.6f;
    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
}

TEST_CASE("AnimStateMachine blend duration applied correctly", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    AnimStateMachine sm;
    uint32_t s0 = sm.addState("Idle", clipIdle);
    uint32_t s1 = sm.addState("Run", clipRun);
    sm.addTransition(s0, s1, 0.5f, "go", TransitionCondition::Compare::BoolTrue);

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);
    AnimStateMachineSystem system;

    reg.get<AnimStateMachineComponent>(e)->setBool("go", true);
    system.update(reg, 0.016f, res);

    auto* animComp = reg.get<AnimatorComponent>(e);
    REQUIRE(animComp->blendDuration == 0.5f);
    REQUIRE(animComp->blendElapsed == 0.0f);
    REQUIRE((animComp->flags & AnimatorComponent::kFlagBlending) != 0);
}

TEST_CASE("AnimStateMachine no transition when conditions not met", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    AnimStateMachine sm;
    uint32_t s0 = sm.addState("Idle", clipIdle);
    sm.addState("Run", clipRun);
    sm.addTransition(s0, 1, 0.2f, "speed", TransitionCondition::Compare::Greater, 100.0f);

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);
    AnimStateMachineSystem system;

    // Speed is only 5 — well below threshold of 100
    reg.get<AnimStateMachineComponent>(e)->setFloat("speed", 5.0f);
    system.update(reg, 0.016f, res);

    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);
    REQUIRE(reg.get<AnimatorComponent>(e)->nextClipId == UINT32_MAX);
}

TEST_CASE("AnimStateMachine default state initialization", "[animation]")
{
    AnimStateMachine sm;
    uint32_t s0 = sm.addState("Idle", 0);
    uint32_t s1 = sm.addState("Run", 1);

    REQUIRE(sm.defaultState == 0);

    sm.defaultState = s1;
    REQUIRE(sm.defaultState == 1);

    // Component should use the machine's default state
    AnimStateMachineComponent comp;
    comp.machine = &sm;
    comp.currentState = sm.defaultState;
    REQUIRE(comp.currentState == s1);
}

TEST_CASE("AnimStateMachine unconditional transition (no conditions)", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    AnimStateMachine sm;
    uint32_t s0 = sm.addState("Idle", clipIdle);
    uint32_t s1 = sm.addState("Run", clipRun);
    // No conditions — should always transition
    sm.addTransition(s0, s1, 0.1f);

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);
    AnimStateMachineSystem system;

    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
    REQUIRE(reg.get<AnimatorComponent>(e)->nextClipId == clipRun);
}

TEST_CASE("AnimStateMachine state speed and loop flags applied", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    AnimStateMachine sm;
    uint32_t s0 = sm.addState("Idle", clipIdle, true, 1.0f);
    uint32_t s1 = sm.addState("Run", clipRun, false, 2.5f);  // non-looping, speed 2.5
    sm.addTransition(s0, s1, 0.1f, "go", TransitionCondition::Compare::BoolTrue);

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);
    AnimStateMachineSystem system;

    reg.get<AnimStateMachineComponent>(e)->setBool("go", true);
    system.update(reg, 0.016f, res);

    auto* animComp = reg.get<AnimatorComponent>(e);
    REQUIRE(animComp->speed == 2.5f);
    // Loop flag should be cleared (non-looping state)
    REQUIRE((animComp->flags & AnimatorComponent::kFlagLooping) == 0);
}

TEST_CASE("AnimStateMachine does not transition during active blend", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    AnimationClip jump;
    jump.name = "Jump";
    jump.duration = 0.5f;
    uint32_t clipJump = res.addClip(std::move(jump));

    AnimStateMachine sm;
    uint32_t s0 = sm.addState("Idle", clipIdle);
    uint32_t s1 = sm.addState("Run", clipRun);
    uint32_t s2 = sm.addState("Jump", clipJump);
    sm.addTransition(s0, s1, 0.2f, "run", TransitionCondition::Compare::BoolTrue);
    sm.addTransition(s1, s2, 0.1f, "jump", TransitionCondition::Compare::BoolTrue);

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);
    AnimStateMachineSystem system;

    // Trigger transition to Run
    reg.get<AnimStateMachineComponent>(e)->setBool("run", true);
    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
    REQUIRE((reg.get<AnimatorComponent>(e)->flags & AnimatorComponent::kFlagBlending) != 0);

    // Now set jump = true while still blending — should NOT transition
    reg.get<AnimStateMachineComponent>(e)->setBool("jump", true);
    system.update(reg, 0.016f, res);
    // Should still be in Run (s1), not Jump (s2)
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
}

TEST_CASE("AnimStateMachineComponent parameter accessors", "[animation]")
{
    AnimStateMachineComponent comp;

    comp.setFloat("speed", 3.14f);
    REQUIRE(comp.getFloat("speed") == 3.14f);

    comp.setBool("grounded", true);
    REQUIRE(comp.getBool("grounded") == true);

    comp.setBool("grounded", false);
    REQUIRE(comp.getBool("grounded") == false);

    // Default for unknown params
    REQUIRE(comp.getFloat("unknown") == 0.0f);
    REQUIRE(comp.getBool("unknown") == false);
}

TEST_CASE("AnimStateMachine NotEqual condition", "[animation]")
{
    uint32_t clipIdle = 0, clipRun = 0;
    AnimationResources res = buildTestResources(clipIdle, clipRun);

    AnimStateMachine sm;
    uint32_t s0 = sm.addState("A", clipIdle);
    uint32_t s1 = sm.addState("B", clipRun);
    sm.addTransition(s0, s1, 0.1f, "state", TransitionCondition::Compare::NotEqual, 0.0f);

    Registry reg;
    EntityID e = createAnimEntity(reg, sm, clipIdle);
    AnimStateMachineSystem system;

    // state = 0 — NotEqual 0 is false, no transition
    reg.get<AnimStateMachineComponent>(e)->setFloat("state", 0.0f);
    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s0);

    // state = 1 — NotEqual 0 is true, transition
    reg.get<AnimStateMachineComponent>(e)->setFloat("state", 1.0f);
    system.update(reg, 0.016f, res);
    REQUIRE(reg.get<AnimStateMachineComponent>(e)->currentState == s1);
}
