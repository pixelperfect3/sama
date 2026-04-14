#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationEventQueue.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSystem.h"
#include "engine/animation/Hash.h"
#include "engine/animation/Skeleton.h"
#include "engine/ecs/Registry.h"

using namespace engine::animation;
using engine::ecs::EntityID;
using engine::ecs::Registry;

namespace
{

// Build a trivial one-joint skeleton + clip with configurable duration and events.
struct MinimalSetup
{
    AnimationResources res;
    uint32_t skelId = 0;
    uint32_t clipId = 0;

    MinimalSetup(float duration, std::vector<std::pair<float, std::string>> events)
    {
        Skeleton skel;
        skel.joints.resize(1);
        skel.joints[0].parentIndex = -1;
        skel.joints[0].inverseBindMatrix = engine::math::Mat4(1.0f);
        skelId = res.addSkeleton(std::move(skel));

        AnimationClip clip;
        clip.name = "TestClip";
        clip.duration = duration;
        JointChannel ch;
        ch.jointIndex = 0;
        ch.positions.push_back({0.0f, engine::math::Vec3{0, 0, 0}});
        ch.rotations.push_back({0.0f, engine::math::Quat{1, 0, 0, 0}});
        ch.scales.push_back({0.0f, engine::math::Vec3{1, 1, 1}});
        clip.channels.push_back(std::move(ch));

        for (auto& [t, name] : events)
            clip.addEvent(t, name);

        clipId = res.addClip(std::move(clip));
    }
};

EntityID createAnimatedEntity(Registry& reg, uint32_t skelId, uint32_t clipId, float startTime,
                              float speed, uint8_t flags)
{
    EntityID entity = reg.createEntity();
    reg.emplace<SkeletonComponent>(entity, SkeletonComponent{skelId});
    reg.emplace<SkinComponent>(entity, SkinComponent{0, 1});

    AnimatorComponent anim{};
    anim.clipId = clipId;
    anim.nextClipId = UINT32_MAX;
    anim.playbackTime = startTime;
    anim.prevPlaybackTime = startTime;
    anim.speed = speed;
    anim.blendFactor = 0.0f;
    anim.blendDuration = 0.0f;
    anim.blendElapsed = 0.0f;
    anim.flags = flags;
    anim._pad[0] = anim._pad[1] = anim._pad[2] = 0;
    reg.emplace<AnimatorComponent>(entity, anim);

    return entity;
}

}  // namespace

// ---------------------------------------------------------------------------
// Clip event insertion tests
// ---------------------------------------------------------------------------

TEST_CASE("AnimationClip::addEvent inserts in sorted order", "[animation]")
{
    AnimationClip clip;
    clip.duration = 3.0f;

    clip.addEvent(1.0f, "mid");
    clip.addEvent(0.5f, "early");
    clip.addEvent(2.5f, "late");
    clip.addEvent(1.0f, "mid_dup");

    REQUIRE(clip.events.size() == 4);
    CHECK(clip.events[0].time == 0.5f);
    CHECK(clip.events[0].name == "early");
    CHECK(clip.events[1].time == 1.0f);
    CHECK(clip.events[2].time == 1.0f);
    CHECK(clip.events[3].time == 2.5f);
    CHECK(clip.events[3].name == "late");
}

TEST_CASE("AnimationClip::addEvent computes FNV-1a hash", "[animation]")
{
    AnimationClip clip;
    clip.duration = 1.0f;
    clip.addEvent(0.5f, "footstep");

    REQUIRE(clip.events.size() == 1);
    CHECK(clip.events[0].nameHash == fnv1a("footstep"));
    CHECK(clip.events[0].nameHash != 0);
}

// ---------------------------------------------------------------------------
// AnimationEventQueue tests
// ---------------------------------------------------------------------------

TEST_CASE("AnimationEventQueue::has() and clear()", "[animation]")
{
    AnimationEventQueue queue;
    uint32_t hashA = fnv1a("hit");
    uint32_t hashB = fnv1a("miss");

    CHECK_FALSE(queue.has(hashA));

    queue.events.push_back({hashA, 0.5f});
    CHECK(queue.has(hashA));
    CHECK_FALSE(queue.has(hashB));

    queue.clear();
    CHECK_FALSE(queue.has(hashA));
    CHECK(queue.events.empty());
}

// ---------------------------------------------------------------------------
// Event firing during normal playback
// ---------------------------------------------------------------------------

TEST_CASE("Events fire during normal forward playback", "[animation]")
{
    MinimalSetup setup(2.0f, {{0.5f, "evt_a"}, {1.0f, "evt_b"}});

    Registry reg;
    EntityID e = createAnimatedEntity(reg, setup.skelId, setup.clipId, 0.0f, 1.0f,
                                      AnimatorComponent::kFlagPlaying);

    AnimationSystem sys;
    // Advance 0.6 seconds: should fire evt_a (at 0.5) but not evt_b (at 1.0).
    sys.update(reg, 0.6f, setup.res, nullptr);

    auto* queue = reg.get<AnimationEventQueue>(e);
    REQUIRE(queue != nullptr);
    REQUIRE(queue->events.size() == 1);
    CHECK(queue->events[0].nameHash == fnv1a("evt_a"));

    queue->clear();

    // Advance another 0.5 seconds (0.6 -> 1.1): should fire evt_b (at 1.0).
    sys.update(reg, 0.5f, setup.res, nullptr);

    queue = reg.get<AnimationEventQueue>(e);
    REQUIRE(queue != nullptr);
    REQUIRE(queue->events.size() == 1);
    CHECK(queue->events[0].nameHash == fnv1a("evt_b"));
}

// ---------------------------------------------------------------------------
// Event firing across loop boundary
// ---------------------------------------------------------------------------

TEST_CASE("Events fire across loop boundary", "[animation]")
{
    MinimalSetup setup(2.0f, {{0.2f, "start_evt"}, {1.8f, "end_evt"}});

    Registry reg;
    EntityID e =
        createAnimatedEntity(reg, setup.skelId, setup.clipId, 1.5f, 1.0f,
                             AnimatorComponent::kFlagPlaying | AnimatorComponent::kFlagLooping);

    AnimationSystem sys;
    // Advance 1.0 second: from 1.5 -> wraps to 0.5.
    // Should fire end_evt (at 1.8, in [1.5, 2.0]) and start_evt (at 0.2, in [0, 0.5]).
    sys.update(reg, 1.0f, setup.res, nullptr);

    auto* queue = reg.get<AnimationEventQueue>(e);
    REQUIRE(queue != nullptr);
    REQUIRE(queue->events.size() == 2);

    // Events should include both.
    CHECK(queue->has(fnv1a("end_evt")));
    CHECK(queue->has(fnv1a("start_evt")));
}

// ---------------------------------------------------------------------------
// No events fire when paused
// ---------------------------------------------------------------------------

TEST_CASE("No events fire when paused", "[animation]")
{
    MinimalSetup setup(2.0f, {{0.5f, "evt"}});

    Registry reg;
    EntityID e = createAnimatedEntity(reg, setup.skelId, setup.clipId, 0.0f, 1.0f,
                                      0);  // not playing

    AnimationSystem sys;
    sys.update(reg, 1.0f, setup.res, nullptr);

    auto* queue = reg.get<AnimationEventQueue>(e);
    // Queue should not exist or be empty since we never played.
    if (queue)
        CHECK(queue->events.empty());
}

// ---------------------------------------------------------------------------
// No events fire on kFlagSampleOnce (editor scrub)
// ---------------------------------------------------------------------------

TEST_CASE("No events fire on kFlagSampleOnce", "[animation]")
{
    MinimalSetup setup(2.0f, {{0.5f, "evt"}});

    Registry reg;
    EntityID e = createAnimatedEntity(reg, setup.skelId, setup.clipId, 0.0f, 1.0f,
                                      AnimatorComponent::kFlagSampleOnce);

    AnimationSystem sys;
    sys.update(reg, 1.0f, setup.res, nullptr);

    auto* queue = reg.get<AnimationEventQueue>(e);
    if (queue)
        CHECK(queue->events.empty());
}

// ---------------------------------------------------------------------------
// Event callback invocation
// ---------------------------------------------------------------------------

TEST_CASE("Event callback is invoked when events fire", "[animation]")
{
    MinimalSetup setup(2.0f, {{0.5f, "cb_test"}});

    Registry reg;
    EntityID e = createAnimatedEntity(reg, setup.skelId, setup.clipId, 0.0f, 1.0f,
                                      AnimatorComponent::kFlagPlaying);

    int callCount = 0;
    EntityID callbackEntity = 0;
    std::string callbackName;

    AnimationSystem sys;
    sys.setEventCallback(
        [&](EntityID entity, const AnimationEvent& evt)
        {
            ++callCount;
            callbackEntity = entity;
            callbackName = evt.name;
        });

    sys.update(reg, 0.6f, setup.res, nullptr);

    CHECK(callCount == 1);
    CHECK(callbackEntity == e);
    CHECK(callbackName == "cb_test");
}

// ---------------------------------------------------------------------------
// Multiple events at the same timestamp
// ---------------------------------------------------------------------------

TEST_CASE("Multiple events at same timestamp all fire", "[animation]")
{
    MinimalSetup setup(2.0f, {{0.5f, "evt_x"}, {0.5f, "evt_y"}});

    Registry reg;
    EntityID e = createAnimatedEntity(reg, setup.skelId, setup.clipId, 0.0f, 1.0f,
                                      AnimatorComponent::kFlagPlaying);

    AnimationSystem sys;
    sys.update(reg, 0.6f, setup.res, nullptr);

    auto* queue = reg.get<AnimationEventQueue>(e);
    REQUIRE(queue != nullptr);
    REQUIRE(queue->events.size() == 2);
    CHECK(queue->has(fnv1a("evt_x")));
    CHECK(queue->has(fnv1a("evt_y")));
}
