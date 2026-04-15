#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <string>

using Catch::Approx;

#include "engine/animation/AnimStateMachine.h"
#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSerializer.h"

using namespace engine::animation;

namespace
{

// Helper: create a temp file path that is cleaned up automatically.
struct TempFile
{
    std::string path;

    explicit TempFile(const char* suffix)
    {
        path = std::filesystem::temp_directory_path().string() + "/sama_test_" +
               std::to_string(reinterpret_cast<uintptr_t>(this)) + suffix;
    }

    ~TempFile()
    {
        std::filesystem::remove(path);
    }
};

AnimationResources buildResources()
{
    AnimationResources res;

    AnimationClip walk;
    walk.name = "Walk";
    walk.duration = 1.0f;
    (void)res.addClip(std::move(walk));

    AnimationClip run;
    run.name = "Run";
    run.duration = 0.8f;
    (void)res.addClip(std::move(run));

    AnimationClip idle;
    idle.name = "Idle";
    idle.duration = 2.0f;
    (void)res.addClip(std::move(idle));

    return res;
}

}  // namespace

// ---------------------------------------------------------------------------
// Events serialization
// ---------------------------------------------------------------------------

TEST_CASE("saveEvents + loadEvents round-trip", "[animation][serializer]")
{
    TempFile tmp(".events.json");

    AnimationResources res = buildResources();
    res.getClipMut(0)->addEvent(0.25f, "footstep_left");
    res.getClipMut(0)->addEvent(0.75f, "footstep_right");
    res.getClipMut(1)->addEvent(0.15f, "footstep_left");

    REQUIRE(saveEvents(res, tmp.path));

    // Load into fresh resources with the same clip names.
    AnimationResources res2 = buildResources();
    REQUIRE(loadEvents(res2, tmp.path));

    // Walk clip should have 2 events.
    const auto* walkClip = res2.getClip(0);
    REQUIRE(walkClip->events.size() == 2);
    CHECK(walkClip->events[0].name == "footstep_left");
    CHECK(walkClip->events[0].time == Approx(0.25f));
    CHECK(walkClip->events[1].name == "footstep_right");
    CHECK(walkClip->events[1].time == Approx(0.75f));

    // Run clip should have 1 event.
    const auto* runClip = res2.getClip(1);
    REQUIRE(runClip->events.size() == 1);
    CHECK(runClip->events[0].name == "footstep_left");
    CHECK(runClip->events[0].time == Approx(0.15f));

    // Idle clip should have no events.
    const auto* idleClip = res2.getClip(2);
    CHECK(idleClip->events.empty());
}

TEST_CASE("loadEvents with missing clips gracefully skips", "[animation][serializer]")
{
    TempFile tmp(".events.json");

    // Save events for Walk clip.
    AnimationResources res = buildResources();
    res.getClipMut(0)->addEvent(0.5f, "step");
    REQUIRE(saveEvents(res, tmp.path));

    // Load into resources that only have Run and Idle (no Walk).
    AnimationResources res2;
    AnimationClip run;
    run.name = "Run";
    run.duration = 0.8f;
    (void)res2.addClip(std::move(run));

    REQUIRE(loadEvents(res2, tmp.path));
    CHECK(res2.getClip(0)->events.empty());  // Run clip unchanged
}

// ---------------------------------------------------------------------------
// State machine serialization
// ---------------------------------------------------------------------------

TEST_CASE("saveStateMachine + loadStateMachine round-trip", "[animation][serializer]")
{
    TempFile tmp(".statemachine.json");

    AnimationResources res = buildResources();

    AnimStateMachine machine;
    uint32_t sIdle = machine.addState("Idle", 2, true, 1.0f);
    uint32_t sWalk = machine.addState("Walk", 0, true, 1.0f);
    machine.defaultState = 0;

    machine.addTransition(sIdle, sWalk, 0.3f, "speed", TransitionCondition::Compare::Greater, 0.5f);
    machine.addTransitionWithExitTime(sWalk, sIdle, 0.2f, 0.9f);

    REQUIRE(saveStateMachine(machine, res, tmp.path));

    AnimStateMachine loaded;
    REQUIRE(loadStateMachine(loaded, res, tmp.path));

    REQUIRE(loaded.states.size() == 2);
    CHECK(loaded.defaultState == 0);

    CHECK(loaded.states[0].name == "Idle");
    CHECK(loaded.states[0].clipId == 2);
    CHECK(loaded.states[0].loop == true);
    CHECK(loaded.states[0].speed == Approx(1.0f));

    CHECK(loaded.states[1].name == "Walk");
    CHECK(loaded.states[1].clipId == 0);

    // Check transition from Idle -> Walk.
    REQUIRE(loaded.states[0].transitions.size() == 1);
    const auto& tr = loaded.states[0].transitions[0];
    CHECK(tr.targetState == 1);
    CHECK(tr.blendDuration == Approx(0.3f));
    CHECK(tr.hasExitTime == false);
    REQUIRE(tr.conditions.size() == 1);
    CHECK(tr.conditions[0].paramName == "speed");
    CHECK(tr.conditions[0].compare == TransitionCondition::Compare::Greater);
    CHECK(tr.conditions[0].threshold == Approx(0.5f));

    // Check transition from Walk -> Idle with exit time.
    REQUIRE(loaded.states[1].transitions.size() == 1);
    const auto& tr2 = loaded.states[1].transitions[0];
    CHECK(tr2.targetState == 0);
    CHECK(tr2.hasExitTime == true);
    CHECK(tr2.exitTime == Approx(0.9f));
}

TEST_CASE("loadStateMachine resolves clip names", "[animation][serializer]")
{
    TempFile tmp(".statemachine.json");

    AnimationResources res = buildResources();

    AnimStateMachine machine;
    machine.addState("Idle", 2, true, 1.0f);  // clipId=2 -> "Idle" clip
    REQUIRE(saveStateMachine(machine, res, tmp.path));

    // Load with same resources -- clipId should resolve back to 2.
    AnimStateMachine loaded;
    REQUIRE(loadStateMachine(loaded, res, tmp.path));
    REQUIRE(loaded.states.size() == 1);
    CHECK(loaded.states[0].clipId == 2);
}

TEST_CASE("loadStateMachine with unknown clip names sets UINT32_MAX", "[animation][serializer]")
{
    TempFile tmp(".statemachine.json");

    AnimationResources res = buildResources();

    AnimStateMachine machine;
    machine.addState("Idle", 2, true, 1.0f);
    REQUIRE(saveStateMachine(machine, res, tmp.path));

    // Load with empty resources -- clip "Idle" won't be found.
    AnimationResources emptyRes;
    AnimStateMachine loaded;
    REQUIRE(loadStateMachine(loaded, emptyRes, tmp.path));
    REQUIRE(loaded.states.size() == 1);
    CHECK(loaded.states[0].clipId == UINT32_MAX);
}

TEST_CASE("loadEvents returns false for non-existent file", "[animation][serializer]")
{
    AnimationResources res = buildResources();
    CHECK_FALSE(loadEvents(res, "/tmp/nonexistent_12345.events.json"));
}

TEST_CASE("loadStateMachine returns false for non-existent file", "[animation][serializer]")
{
    AnimationResources res = buildResources();
    AnimStateMachine machine;
    CHECK_FALSE(loadStateMachine(machine, res, "/tmp/nonexistent_12345.statemachine.json"));
}

TEST_CASE("loadEvents returns false for malformed JSON", "[animation][serializer]")
{
    TempFile tmp(".events.json");

    // Write invalid JSON.
    FILE* f = fopen(tmp.path.c_str(), "w");
    REQUIRE(f);
    fprintf(f, "{ invalid json !!!");
    fclose(f);

    AnimationResources res = buildResources();
    CHECK_FALSE(loadEvents(res, tmp.path));
}

TEST_CASE("loadStateMachine returns false for malformed JSON", "[animation][serializer]")
{
    TempFile tmp(".statemachine.json");

    FILE* f = fopen(tmp.path.c_str(), "w");
    REQUIRE(f);
    fprintf(f, "not valid json");
    fclose(f);

    AnimationResources res = buildResources();
    AnimStateMachine machine;
    CHECK_FALSE(loadStateMachine(machine, res, tmp.path));
}
