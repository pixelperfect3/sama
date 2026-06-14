#include "engine/animation/AnimationSystem.h"

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory_resource>
#include <vector>

#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationEventQueue.h"
#include "engine/animation/AnimationSampler.h"
#include "engine/animation/IkComponents.h"
#include "engine/animation/Pose.h"
#include "engine/animation/Skeleton.h"
#include "engine/rendering/EcsComponents.h"

namespace engine::animation
{

namespace
{

// Build a local TRS matrix from a JointPose.  Direct construction — same
// pattern as engine/scene/TransformSystem.cpp `composeLocal` (audit item
// #C-xform-trs).  The old `translate * mat4_cast * scale` chain cost
// ~176 muls + 3 Mat4 temporaries on the stack; this form is ~29 muls
// with no temporaries.  Closed-form TRS for column-major glm::mat4:
//
//   col0 = R[0] * s.x     col1 = R[1] * s.y     col2 = R[2] * s.z
//   col3 = (pos, 1)
//
// where R is the unit-quat rotation as a Mat3.
math::Mat4 trsMatrix(const JointPose& jp)
{
    const math::Mat3 rot = glm::mat3_cast(jp.rotation);
    return {math::Vec4(rot[0] * jp.scale.x, 0.0F), math::Vec4(rot[1] * jp.scale.y, 0.0F),
            math::Vec4(rot[2] * jp.scale.z, 0.0F), math::Vec4(jp.position, 1.0F)};
}

}  // anonymous namespace

void AnimationSystem::update(ecs::Registry& reg, float dt, AnimationResources& animRes,
                             std::pmr::memory_resource* arena)
{
    // Use the arena if provided, otherwise fall back to default allocator.
    std::pmr::memory_resource* alloc = arena ? arena : std::pmr::get_default_resource();

    // Construct the bone buffer without pre-allocating its backing store —
    // `reserve(256)` would call `alloc->allocate(256 * sizeof(Mat4))` even
    // when the view is empty, wasting 16 KB of arena memory per frame on
    // scenes with zero skinned entities.  Audit item line 147 — first
    // skinned entity inside the lambda triggers the reserve.
    std::pmr::vector<math::Mat4> boneBuffer(alloc);
    bool boneBufferReserved = false;

    auto view = reg.view<SkeletonComponent, AnimatorComponent, SkinComponent>();

    view.each(
        [&](ecs::EntityID entity, const SkeletonComponent& skelComp, AnimatorComponent& animComp,
            SkinComponent& skinComp)
        {
            const Skeleton* skeleton = animRes.getSkeleton(skelComp.skeletonId);
            if (!skeleton || skeleton->parentIndices.empty())
                return;

            // Lazy reserve — first skinned entity pays the 16 KB; empty-view
            // scenes don't.  See `boneBufferReserved` declaration above.
            if (!boneBufferReserved)
            {
                boneBuffer.reserve(256);
                boneBufferReserved = true;
            }

            const uint32_t jointCount = skeleton->jointCount();

            // Remember the time before advancing for event detection.
            const float prevTime = animComp.playbackTime;
            const bool isLooping = (animComp.flags & AnimatorComponent::kFlagLooping) != 0;

            // Advance playback time if playing (kFlagSampleOnce does NOT advance time).
            if (animComp.flags & AnimatorComponent::kFlagPlaying)
            {
                animComp.playbackTime += dt * animComp.speed;

                const AnimationClip* clip = animRes.getClip(animComp.clipId);
                if (clip && clip->duration > 0.0f)
                {
                    if (isLooping)
                    {
                        while (animComp.playbackTime >= clip->duration)
                            animComp.playbackTime -= clip->duration;
                        while (animComp.playbackTime < 0.0f)
                            animComp.playbackTime += clip->duration;
                    }
                    else
                    {
                        animComp.playbackTime =
                            glm::clamp(animComp.playbackTime, 0.0f, clip->duration);
                    }
                }

                // Fire animation events for the interval we just advanced through.
                if (clip && !clip->events.empty())
                {
                    fireEvents(reg, entity, *clip, prevTime, animComp.playbackTime, isLooping);
                }
            }

            // Update prevPlaybackTime for the next frame.
            animComp.prevPlaybackTime = animComp.playbackTime;

            // Sample current clip when playing or when the editor has requested a
            // one-shot sample (e.g. during scrubbing while paused). The sample-once
            // flag is consumed here so it doesn't keep firing every frame.
            const bool sampleOnce = (animComp.flags & AnimatorComponent::kFlagSampleOnce) != 0;
            if (sampleOnce)
                animComp.flags &= ~AnimatorComponent::kFlagSampleOnce;

            Pose poseA;
            const AnimationClip* clipA = animRes.getClip(animComp.clipId);
            if (clipA)
                sampleClip(*clipA, *skeleton, animComp.playbackTime, poseA);
            else
            {
                poseA.jointPoses.resize(jointCount);
                return;  // no valid clip
            }

            // Handle blending if active; otherwise avoid the copy entirely.
            Pose finalPose;
            if (animComp.flags & AnimatorComponent::kFlagBlending)
            {
                const AnimationClip* clipB = animRes.getClip(animComp.nextClipId);
                if (clipB)
                {
                    animComp.blendElapsed += dt;
                    animComp.blendFactor = (animComp.blendDuration > 0.0f)
                                               ? animComp.blendElapsed / animComp.blendDuration
                                               : 1.0f;

                    if (animComp.blendFactor >= 1.0f)
                    {
                        // Blend complete: promote next clip to current.
                        animComp.clipId = animComp.nextClipId;
                        animComp.nextClipId = UINT32_MAX;
                        animComp.blendFactor = 0.0f;
                        animComp.blendElapsed = 0.0f;
                        animComp.blendDuration = 0.0f;
                        animComp.flags &= ~AnimatorComponent::kFlagBlending;

                        // Sample the promoted clip.
                        sampleClip(*clipB, *skeleton, animComp.playbackTime, finalPose);
                    }
                    else
                    {
                        Pose poseB;
                        sampleClip(*clipB, *skeleton, animComp.playbackTime, poseB);
                        blendPoses(poseA, poseB, animComp.blendFactor, finalPose);
                    }
                }
                else
                {
                    finalPose = std::move(poseA);
                }
            }
            else
            {
                finalPose = std::move(poseA);
            }

            // Compute final bone matrices.
            // Forward pass: parent-first ordering guaranteed by Skeleton.
            //
            // reserve + emplace_back instead of `(jointCount, Mat4(1.0F), alloc)`:
            // the old form value-initialised every slot to identity (jointCount
            // × 16 stores) before the loop overwrote each one (jointCount × 16
            // more stores).  emplace_back constructs each slot in place
            // exactly once — halves the per-joint write count.  Audit item
            // line 120.
            std::pmr::vector<math::Mat4> worldTransforms(alloc);
            worldTransforms.reserve(jointCount);
            for (uint32_t i = 0; i < jointCount; ++i)
            {
                const math::Mat4 local = trsMatrix(finalPose.jointPoses[i]);
                const int32_t parent = skeleton->parentIndices[i];
                if (parent >= 0 && static_cast<uint32_t>(parent) < jointCount)
                {
                    worldTransforms.emplace_back(worldTransforms[parent] * local);
                }
                else
                {
                    worldTransforms.emplace_back(local);
                }
            }

            // Incorporate the entity's world transform so skinned meshes
            // render at the correct position, not always at the origin.
            const auto* wtc = reg.get<rendering::WorldTransformComponent>(entity);
            const math::Mat4 entityWorld = wtc ? wtc->matrix : math::Mat4(1.0f);

            // Record offset and append bone matrices.
            skinComp.boneMatrixOffset = static_cast<uint32_t>(boneBuffer.size());
            skinComp.boneCount = jointCount;

            for (uint32_t i = 0; i < jointCount; ++i)
            {
                math::Mat4 finalMatrix =
                    entityWorld * worldTransforms[i] * skeleton->inverseBindMatrices[i];
                boneBuffer.push_back(finalMatrix);
            }
        });

    // Stash the raw pointer past the local pmr::vector's lifetime. Safe because
    // `arena` is a FrameArena: its memory is reclaimed only at end-of-frame
    // reset(), not on vector destruction. Would dangle with a normal allocator.
    boneBuffer_ = boneBuffer.data();
    boneBufferSize_ = static_cast<uint32_t>(boneBuffer.size());
}

void AnimationSystem::updatePoses(ecs::Registry& reg, float dt, AnimationResources& animRes,
                                  std::pmr::memory_resource* arena)
{
    std::pmr::memory_resource* alloc = arena ? arena : std::pmr::get_default_resource();

    auto view = reg.view<SkeletonComponent, AnimatorComponent, SkinComponent>();

    view.each(
        [&](ecs::EntityID entity, const SkeletonComponent& skelComp, AnimatorComponent& animComp,
            SkinComponent& /*skinComp*/)
        {
            const Skeleton* skeleton = animRes.getSkeleton(skelComp.skeletonId);
            if (!skeleton || skeleton->parentIndices.empty())
                return;

            const uint32_t jointCount = skeleton->jointCount();

            // Remember the time before advancing for event detection.
            const float prevTime = animComp.playbackTime;
            const bool isLooping = (animComp.flags & AnimatorComponent::kFlagLooping) != 0;

            // Advance playback time if playing.
            if (animComp.flags & AnimatorComponent::kFlagPlaying)
            {
                animComp.playbackTime += dt * animComp.speed;

                const AnimationClip* clip = animRes.getClip(animComp.clipId);
                if (clip && clip->duration > 0.0f)
                {
                    if (isLooping)
                    {
                        while (animComp.playbackTime >= clip->duration)
                            animComp.playbackTime -= clip->duration;
                        while (animComp.playbackTime < 0.0f)
                            animComp.playbackTime += clip->duration;
                    }
                    else
                    {
                        animComp.playbackTime =
                            glm::clamp(animComp.playbackTime, 0.0f, clip->duration);
                    }
                }

                // Fire animation events for the interval we just advanced through.
                if (clip && !clip->events.empty())
                {
                    fireEvents(reg, entity, *clip, prevTime, animComp.playbackTime, isLooping);
                }
            }

            // Update prevPlaybackTime for the next frame.
            animComp.prevPlaybackTime = animComp.playbackTime;

            // Consume sample-once flag (editor scrubbing) the same way as update().
            if (animComp.flags & AnimatorComponent::kFlagSampleOnce)
                animComp.flags &= ~AnimatorComponent::kFlagSampleOnce;

            // Sample current clip.
            Pose poseA;
            const AnimationClip* clipA = animRes.getClip(animComp.clipId);
            if (clipA)
                sampleClip(*clipA, *skeleton, animComp.playbackTime, poseA);
            else
            {
                poseA.jointPoses.resize(jointCount);
                return;
            }

            // Handle blending if active; otherwise avoid the copy entirely.
            Pose finalPose;
            if (animComp.flags & AnimatorComponent::kFlagBlending)
            {
                const AnimationClip* clipB = animRes.getClip(animComp.nextClipId);
                if (clipB)
                {
                    animComp.blendElapsed += dt;
                    animComp.blendFactor = (animComp.blendDuration > 0.0f)
                                               ? animComp.blendElapsed / animComp.blendDuration
                                               : 1.0f;

                    if (animComp.blendFactor >= 1.0f)
                    {
                        animComp.clipId = animComp.nextClipId;
                        animComp.nextClipId = UINT32_MAX;
                        animComp.blendFactor = 0.0f;
                        animComp.blendElapsed = 0.0f;
                        animComp.blendDuration = 0.0f;
                        animComp.flags &= ~AnimatorComponent::kFlagBlending;
                        sampleClip(*clipB, *skeleton, animComp.playbackTime, finalPose);
                    }
                    else
                    {
                        Pose poseB;
                        sampleClip(*clipB, *skeleton, animComp.playbackTime, poseB);
                        blendPoses(poseA, poseB, animComp.blendFactor, finalPose);
                    }
                }
                else
                {
                    finalPose = std::move(poseA);
                }
            }
            else
            {
                finalPose = std::move(poseA);
            }

            // Store the pose in PoseComponent for IK to modify.  PoseComponent
            // now owns Pose by value (audit item line 142), so re-use the
            // existing slot's storage via move-assign — the InlinedVector<128>
            // inside Pose reuses its inline buffer (or heap allocation, for
            // > 128-joint skeletons) across frames instead of arena-allocating
            // a fresh 5 KB block every frame.  alloc is intentionally unused
            // by this write path now.
            (void)alloc;
            PoseComponent* existingPose = reg.get<PoseComponent>(entity);
            if (existingPose != nullptr)
            {
                existingPose->pose = std::move(finalPose);
            }
            else
            {
                PoseComponent freshComp;
                freshComp.pose = std::move(finalPose);
                reg.emplace<PoseComponent>(entity, std::move(freshComp));
            }
        });
}

void AnimationSystem::computeBoneMatrices(ecs::Registry& reg, AnimationResources& animRes,
                                          std::pmr::memory_resource* arena)
{
    std::pmr::memory_resource* alloc = arena ? arena : std::pmr::get_default_resource();

    // Same lazy-reserve pattern as update() — see audit item line 147.
    std::pmr::vector<math::Mat4> boneBuffer(alloc);
    bool boneBufferReserved = false;

    auto view = reg.view<SkeletonComponent, SkinComponent, PoseComponent>();

    view.each(
        [&](ecs::EntityID entity, const SkeletonComponent& skelComp, SkinComponent& skinComp,
            const PoseComponent& poseComp)
        {
            const Skeleton* skeleton = animRes.getSkeleton(skelComp.skeletonId);
            if (!skeleton || skeleton->parentIndices.empty())
                return;
            if (poseComp.pose.jointPoses.empty())
                return;

            if (!boneBufferReserved)
            {
                boneBuffer.reserve(256);
                boneBufferReserved = true;
            }

            const Pose& finalPose = poseComp.pose;
            const uint32_t jointCount = skeleton->jointCount();

            // Compute final bone matrices.  Same reserve+emplace_back form
            // as update() — single write per slot.  Audit line 120.
            std::pmr::vector<math::Mat4> worldTransforms(alloc);
            worldTransforms.reserve(jointCount);
            for (uint32_t i = 0; i < jointCount; ++i)
            {
                const math::Mat4 local = trsMatrix(finalPose.jointPoses[i]);
                const int32_t parent = skeleton->parentIndices[i];
                if (parent >= 0 && static_cast<uint32_t>(parent) < jointCount)
                {
                    worldTransforms.emplace_back(worldTransforms[parent] * local);
                }
                else
                {
                    worldTransforms.emplace_back(local);
                }
            }

            const auto* wtc = reg.get<rendering::WorldTransformComponent>(entity);
            const math::Mat4 entityWorld = wtc ? wtc->matrix : math::Mat4(1.0f);

            skinComp.boneMatrixOffset = static_cast<uint32_t>(boneBuffer.size());
            skinComp.boneCount = jointCount;

            for (uint32_t i = 0; i < jointCount; ++i)
            {
                math::Mat4 finalMatrix =
                    entityWorld * worldTransforms[i] * skeleton->inverseBindMatrices[i];
                boneBuffer.push_back(finalMatrix);
            }
        });

    // Same lifetime trick as update(): the pointer survives because `arena`
    // is a FrameArena that doesn't reclaim memory until end-of-frame reset().
    boneBuffer_ = boneBuffer.data();
    boneBufferSize_ = static_cast<uint32_t>(boneBuffer.size());
}

void AnimationSystem::fireEvents(ecs::Registry& reg, ecs::EntityID entity,
                                 const AnimationClip& clip, float prevTime, float newTime,
                                 bool looping)
{
    // Ensure the entity has an AnimationEventQueue component.
    auto* queue = reg.get<AnimationEventQueue>(entity);
    if (!queue)
    {
        reg.emplace<AnimationEventQueue>(entity, AnimationEventQueue{});
        queue = reg.get<AnimationEventQueue>(entity);
    }

    // Helper lambda: collect events in the half-open interval (rangeStart, rangeEnd].
    // We use open-start so that an event exactly at prevTime (already fired last frame)
    // is not re-fired, but an event exactly at newTime is fired.
    auto collectInRange = [&](float rangeStart, float rangeEnd)
    {
        for (const auto& evt : clip.events)
        {
            if (evt.time > rangeStart && evt.time <= rangeEnd)
            {
                queue->events.push_back({evt.nameHash, evt.time});
                if (eventCallback_)
                    eventCallback_(entity, evt);
            }
        }
    };

    // Helper lambda: collect events in reverse for the half-open interval
    // [rangeStart, rangeEnd). Used for reverse playback.
    auto collectInRangeReverse = [&](float rangeStart, float rangeEnd)
    {
        // Iterate in reverse order through events.
        for (auto it = clip.events.rbegin(); it != clip.events.rend(); ++it)
        {
            if (it->time >= rangeStart && it->time < rangeEnd)
            {
                queue->events.push_back({it->nameHash, it->time});
                if (eventCallback_)
                    eventCallback_(entity, *it);
            }
        }
    };

    if (newTime >= prevTime)
    {
        // Normal forward playback, no wrap.
        collectInRange(prevTime, newTime);
    }
    else if (looping && prevTime > newTime)
    {
        // Wrapped around. Could be forward wrap or reverse wrap.
        // Forward wrap: prevTime -> duration, then 0 -> newTime.
        // Reverse wrap: prevTime -> 0, then duration -> newTime.
        if (prevTime > newTime)
        {
            // Forward wrap (most common): events in (prevTime, duration] + (0, newTime]
            // But also handle reverse: if speed is negative the wrap goes the other way.
            // We detect direction by checking if duration - prevTime + newTime is small
            // (forward) vs prevTime + duration - newTime is small (reverse).
            // Simpler: just check both ranges in forward order for forward wrap.
            collectInRange(prevTime, clip.duration);
            collectInRange(-0.0001f, newTime);  // use slightly negative to include t=0
        }
    }
}

}  // namespace engine::animation
