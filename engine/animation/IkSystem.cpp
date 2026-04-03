#include "engine/animation/IkSystem.h"

#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory_resource>
#include <vector>

#include "engine/animation/AnimationComponents.h"
#include "engine/animation/IkComponents.h"
#include "engine/animation/IkSolvers.h"
#include "engine/animation/Skeleton.h"

namespace engine::animation
{

void IkSystem::update(ecs::Registry& reg, const AnimationResources& animRes,
                      std::pmr::memory_resource* arena)
{
    std::pmr::memory_resource* alloc = arena ? arena : std::pmr::get_default_resource();

    auto view = reg.view<SkeletonComponent, IkChainsComponent, IkTargetsComponent, PoseComponent>();

    view.each(
        [&](ecs::EntityID /*entity*/, const SkeletonComponent& skelComp,
            IkChainsComponent& chainsComp, IkTargetsComponent& targetsComp, PoseComponent& poseComp)
        {
            const Skeleton* skeleton = animRes.getSkeleton(skelComp.skeletonId);
            if (!skeleton || skeleton->joints.empty())
            {
                return;
            }
            if (!poseComp.pose)
            {
                return;
            }

            Pose& pose = *poseComp.pose;
            const uint32_t jointCount = skeleton->jointCount();

            // Allocate world-space positions from arena.
            auto* worldPositions = static_cast<math::Vec3*>(
                alloc->allocate(sizeof(math::Vec3) * jointCount, alignof(math::Vec3)));
            computeWorldPositions(*skeleton, pose, worldPositions);

            const size_t chainCount =
                std::min(chainsComp.chains.size(), targetsComp.targets.size());

            for (size_t i = 0; i < chainCount; ++i)
            {
                const IkChainDef& chain = chainsComp.chains[i];
                const IkTarget& target = targetsComp.targets[i];

                if (!chain.enabled || chain.weight <= 0.0f)
                {
                    continue;
                }

                // Save FK rotations for blending.
                memory::InlinedVector<math::Quat, 8> fkRotations;
                memory::InlinedVector<uint32_t, 8> modifiedJoints;

                switch (chain.solverType)
                {
                    case IkSolverType::TwoBone:
                    {
                        // Save FK rotations for the two joints we modify.
                        fkRotations.push_back(pose.jointPoses[chain.rootJoint].rotation);
                        fkRotations.push_back(pose.jointPoses[chain.midJoint].rotation);
                        modifiedJoints.push_back(chain.rootJoint);
                        modifiedJoints.push_back(chain.midJoint);

                        solveTwoBone(*skeleton, pose, worldPositions, chain.rootJoint,
                                     chain.midJoint, chain.endEffectorJoint, target.position,
                                     chain.poleVector);
                        break;
                    }
                    case IkSolverType::Ccd:
                    {
#if ENGINE_IK_ENABLE_CCD
                        auto chainJoints = buildChainFromHierarchy(*skeleton, chain.rootJoint,
                                                                   chain.endEffectorJoint);

                        // Save FK rotations for all chain joints except end effector.
                        for (size_t j = 0; j + 1 < chainJoints.size(); ++j)
                        {
                            fkRotations.push_back(pose.jointPoses[chainJoints[j]].rotation);
                            modifiedJoints.push_back(chainJoints[j]);
                        }

                        solveCcd(*skeleton, pose, worldPositions, chainJoints, target.position,
                                 chain.maxIterations);
#else
                        static bool warned = false;
                        if (!warned)
                        {
                            fprintf(stderr,
                                    "IkSystem: CCD solver disabled at compile "
                                    "time\n");
                            warned = true;
                        }
#endif
                        break;
                    }
                    case IkSolverType::Fabrik:
                    {
#if ENGINE_IK_ENABLE_FABRIK
                        auto chainJoints = buildChainFromHierarchy(*skeleton, chain.rootJoint,
                                                                   chain.endEffectorJoint);

                        for (size_t j = 0; j + 1 < chainJoints.size(); ++j)
                        {
                            fkRotations.push_back(pose.jointPoses[chainJoints[j]].rotation);
                            modifiedJoints.push_back(chainJoints[j]);
                        }

                        solveFabrik(*skeleton, pose, worldPositions, chainJoints, target.position,
                                    chain.maxIterations);
#else
                        static bool warned = false;
                        if (!warned)
                        {
                            fprintf(stderr,
                                    "IkSystem: FABRIK solver disabled at compile "
                                    "time\n");
                            warned = true;
                        }
#endif
                        break;
                    }
                }

                // Blend: interpolate between FK and IK rotations.
                if (chain.weight < 1.0f && !modifiedJoints.empty())
                {
                    for (size_t j = 0; j < modifiedJoints.size(); ++j)
                    {
                        uint32_t jIdx = modifiedJoints[j];
                        math::Quat ikRot = pose.jointPoses[jIdx].rotation;
                        pose.jointPoses[jIdx].rotation =
                            glm::slerp(fkRotations[j], ikRot, chain.weight);
                    }
                    // Recompute world positions after blending.
                    computeWorldPositions(*skeleton, pose, worldPositions);
                }

                // Optional: apply target orientation to end effector.
                if (target.hasOrientation)
                {
                    // Convert target world orientation to local space.
                    math::Quat parentWorldRot{1, 0, 0, 0};
                    int32_t parentIdx = skeleton->joints[chain.endEffectorJoint].parentIndex;
                    if (parentIdx >= 0)
                    {
                        // Accumulate parent rotations.
                        math::Quat wr{1, 0, 0, 0};
                        // Walk from root to parent.
                        memory::InlinedVector<uint32_t, 16> ancestors;
                        int32_t cur = parentIdx;
                        while (cur >= 0)
                        {
                            ancestors.push_back(static_cast<uint32_t>(cur));
                            cur = skeleton->joints[cur].parentIndex;
                        }
                        for (int32_t a = static_cast<int32_t>(ancestors.size()) - 1; a >= 0; --a)
                        {
                            wr = wr * pose.jointPoses[ancestors[a]].rotation;
                        }
                        parentWorldRot = wr;
                    }
                    pose.jointPoses[chain.endEffectorJoint].rotation =
                        glm::inverse(parentWorldRot) * target.orientation;
                    computeWorldPositions(*skeleton, pose, worldPositions);
                }
            }
        });
}

}  // namespace engine::animation
