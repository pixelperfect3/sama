#include <catch2/catch_test_macros.hpp>

#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSystem.h"
#include "engine/animation/Skeleton.h"
#include "engine/ecs/Registry.h"

using namespace engine::animation;
using engine::ecs::EntityID;
using engine::ecs::Registry;

namespace
{

// Build a trivial one-joint skeleton + one-frame clip so sampling is cheap and
// deterministic.
AnimationResources buildMinimalResources(uint32_t& skelIdOut, uint32_t& clipIdOut)
{
    AnimationResources res;

    Skeleton skel;
    skel.joints.resize(1);
    skel.joints[0].parentIndex = -1;
    skel.joints[0].inverseBindMatrix = engine::math::Mat4(1.0f);
    skelIdOut = res.addSkeleton(std::move(skel));

    AnimationClip clip;
    clip.name = "Idle";
    clip.duration = 2.0f;
    JointChannel ch;
    ch.jointIndex = 0;
    ch.positions.push_back({0.0f, engine::math::Vec3{0, 0, 0}});
    ch.rotations.push_back({0.0f, engine::math::Quat{1, 0, 0, 0}});
    ch.scales.push_back({0.0f, engine::math::Vec3{1, 1, 1}});
    clip.channels.push_back(std::move(ch));
    clipIdOut = res.addClip(std::move(clip));

    return res;
}

}  // namespace

TEST_CASE("AnimationSystem kFlagSampleOnce does not advance time", "[animation]")
{
    uint32_t skelId = 0;
    uint32_t clipId = 0;
    AnimationResources res = buildMinimalResources(skelId, clipId);

    Registry reg;
    EntityID entity = reg.createEntity();
    reg.emplace<SkeletonComponent>(entity, SkeletonComponent{skelId});
    reg.emplace<SkinComponent>(entity, SkinComponent{0, 1});

    AnimatorComponent anim{};
    anim.clipId = clipId;
    anim.nextClipId = UINT32_MAX;
    anim.playbackTime = 0.5f;
    anim.prevPlaybackTime = 0.5f;
    anim.speed = 1.0f;
    anim.flags = AnimatorComponent::kFlagSampleOnce;  // explicitly NOT playing
    reg.emplace<AnimatorComponent>(entity, anim);

    AnimationSystem sys;
    sys.update(reg, 1.0f, res, nullptr);

    auto* out = reg.get<AnimatorComponent>(entity);
    REQUIRE(out != nullptr);

    // Time must not have advanced because kFlagPlaying is clear.
    CHECK(out->playbackTime == 0.5f);

    // Sample-once must be consumed.
    CHECK((out->flags & AnimatorComponent::kFlagSampleOnce) == 0);
    CHECK((out->flags & AnimatorComponent::kFlagPlaying) == 0);
}

TEST_CASE("AnimationSystem kFlagPlaying advances time and clears sample-once", "[animation]")
{
    uint32_t skelId = 0;
    uint32_t clipId = 0;
    AnimationResources res = buildMinimalResources(skelId, clipId);

    Registry reg;
    EntityID entity = reg.createEntity();
    reg.emplace<SkeletonComponent>(entity, SkeletonComponent{skelId});
    reg.emplace<SkinComponent>(entity, SkinComponent{0, 1});

    AnimatorComponent anim{};
    anim.clipId = clipId;
    anim.nextClipId = UINT32_MAX;
    anim.playbackTime = 0.0f;
    anim.prevPlaybackTime = 0.0f;
    anim.speed = 1.0f;
    anim.flags = AnimatorComponent::kFlagPlaying | AnimatorComponent::kFlagSampleOnce;
    reg.emplace<AnimatorComponent>(entity, anim);

    AnimationSystem sys;
    sys.update(reg, 0.25f, res, nullptr);

    auto* out = reg.get<AnimatorComponent>(entity);
    REQUIRE(out != nullptr);
    CHECK(out->playbackTime == 0.25f);
    CHECK((out->flags & AnimatorComponent::kFlagSampleOnce) == 0);
    CHECK((out->flags & AnimatorComponent::kFlagPlaying) != 0);
}
