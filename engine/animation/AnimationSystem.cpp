#include "engine/animation/AnimationSystem.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory_resource>
#include <vector>

#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationSampler.h"
#include "engine/animation/IkComponents.h"
#include "engine/animation/Pose.h"
#include "engine/animation/Skeleton.h"

namespace engine::animation
{

namespace
{

// Build a local TRS matrix from a JointPose.
math::Mat4 trsMatrix(const JointPose& jp)
{
    math::Mat4 m(1.0f);
    m = glm::translate(m, jp.position);
    m *= glm::mat4_cast(jp.rotation);
    m = glm::scale(m, jp.scale);
    return m;
}

}  // anonymous namespace

void AnimationSystem::update(ecs::Registry& reg, float dt, AnimationResources& animRes,
                             std::pmr::memory_resource* arena)
{
    // Use the arena if provided, otherwise fall back to default allocator.
    std::pmr::memory_resource* alloc = arena ? arena : std::pmr::get_default_resource();

    std::pmr::vector<math::Mat4> boneBuffer(alloc);
    boneBuffer.reserve(256);  // reasonable initial estimate

    auto view = reg.view<SkeletonComponent, AnimatorComponent, SkinComponent>();

    view.each(
        [&](ecs::EntityID /*entity*/, const SkeletonComponent& skelComp,
            AnimatorComponent& animComp, SkinComponent& skinComp)
        {
            const Skeleton* skeleton = animRes.getSkeleton(skelComp.skeletonId);
            if (!skeleton || skeleton->joints.empty())
                return;

            const uint32_t jointCount = skeleton->jointCount();

            // Advance playback time if playing (kFlagSampleOnce does NOT advance time).
            if (animComp.flags & AnimatorComponent::kFlagPlaying)
            {
                animComp.playbackTime += dt * animComp.speed;

                const AnimationClip* clip = animRes.getClip(animComp.clipId);
                if (clip && clip->duration > 0.0f)
                {
                    if (animComp.flags & AnimatorComponent::kFlagLooping)
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
            }

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

            // Handle blending if active.
            Pose finalPose = poseA;
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
            }

            // Compute final bone matrices.
            // Forward pass: parent-first ordering guaranteed by Skeleton.
            std::pmr::vector<math::Mat4> worldTransforms(jointCount, math::Mat4(1.0f), alloc);
            for (uint32_t i = 0; i < jointCount; ++i)
            {
                math::Mat4 local = trsMatrix(finalPose.jointPoses[i]);
                int32_t parent = skeleton->joints[i].parentIndex;
                if (parent >= 0 && static_cast<uint32_t>(parent) < jointCount)
                    worldTransforms[i] = worldTransforms[parent] * local;
                else
                    worldTransforms[i] = local;
            }

            // Record offset and append bone matrices.
            skinComp.boneMatrixOffset = static_cast<uint32_t>(boneBuffer.size());
            skinComp.boneCount = jointCount;

            for (uint32_t i = 0; i < jointCount; ++i)
            {
                math::Mat4 finalMatrix = worldTransforms[i] * skeleton->joints[i].inverseBindMatrix;
                boneBuffer.push_back(finalMatrix);
            }
        });

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
            if (!skeleton || skeleton->joints.empty())
                return;

            const uint32_t jointCount = skeleton->jointCount();

            // Advance playback time if playing.
            if (animComp.flags & AnimatorComponent::kFlagPlaying)
            {
                animComp.playbackTime += dt * animComp.speed;

                const AnimationClip* clip = animRes.getClip(animComp.clipId);
                if (clip && clip->duration > 0.0f)
                {
                    if (animComp.flags & AnimatorComponent::kFlagLooping)
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
            }

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

            // Handle blending if active.
            Pose finalPose = poseA;
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
            }

            // Store the pose in PoseComponent for IK to modify.
            auto* posePtr = static_cast<Pose*>(alloc->allocate(sizeof(Pose), alignof(Pose)));
            new (posePtr) Pose(std::move(finalPose));

            auto* existingPose = reg.get<PoseComponent>(entity);
            if (existingPose)
            {
                existingPose->pose = posePtr;
            }
            else
            {
                reg.emplace<PoseComponent>(entity, PoseComponent{posePtr});
            }
        });
}

void AnimationSystem::computeBoneMatrices(ecs::Registry& reg, AnimationResources& animRes,
                                          std::pmr::memory_resource* arena)
{
    std::pmr::memory_resource* alloc = arena ? arena : std::pmr::get_default_resource();

    std::pmr::vector<math::Mat4> boneBuffer(alloc);
    boneBuffer.reserve(256);

    auto view = reg.view<SkeletonComponent, SkinComponent, PoseComponent>();

    view.each(
        [&](ecs::EntityID /*entity*/, const SkeletonComponent& skelComp, SkinComponent& skinComp,
            const PoseComponent& poseComp)
        {
            const Skeleton* skeleton = animRes.getSkeleton(skelComp.skeletonId);
            if (!skeleton || skeleton->joints.empty())
                return;
            if (!poseComp.pose)
                return;

            const Pose& finalPose = *poseComp.pose;
            const uint32_t jointCount = skeleton->jointCount();

            // Compute final bone matrices.
            std::pmr::vector<math::Mat4> worldTransforms(jointCount, math::Mat4(1.0f), alloc);
            for (uint32_t i = 0; i < jointCount; ++i)
            {
                math::Mat4 local = trsMatrix(finalPose.jointPoses[i]);
                int32_t parent = skeleton->joints[i].parentIndex;
                if (parent >= 0 && static_cast<uint32_t>(parent) < jointCount)
                    worldTransforms[i] = worldTransforms[parent] * local;
                else
                    worldTransforms[i] = local;
            }

            skinComp.boneMatrixOffset = static_cast<uint32_t>(boneBuffer.size());
            skinComp.boneCount = jointCount;

            for (uint32_t i = 0; i < jointCount; ++i)
            {
                math::Mat4 finalMatrix = worldTransforms[i] * skeleton->joints[i].inverseBindMatrix;
                boneBuffer.push_back(finalMatrix);
            }
        });

    boneBuffer_ = boneBuffer.data();
    boneBufferSize_ = static_cast<uint32_t>(boneBuffer.size());
}

}  // namespace engine::animation
