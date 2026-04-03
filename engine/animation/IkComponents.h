#pragma once

#include <cstdint>

#include "engine/animation/Pose.h"
#include "engine/math/Types.h"
#include "engine/memory/InlinedVector.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// ECS components for Inverse Kinematics.
//
// Follow the same conventions as AnimationComponents.h:
// largest-alignment-first layout, explicit padding, static_assert on sizeof.
// ---------------------------------------------------------------------------

enum class IkSolverType : uint8_t
{
    TwoBone = 0,
    Ccd = 1,
    Fabrik = 2,
};

struct IkChainDef  // offset  size
{
    math::Vec3 poleVector{0.0f, 0.0f, 1.0f};          //  0      12  bend direction hint (world)
    float weight = 1.0f;                              // 12       4  FK/IK blend (0=FK, 1=IK)
    uint32_t rootJoint;                               // 16       4  first joint in chain
    uint32_t endEffectorJoint;                        // 20       4  last joint (tip)
    uint32_t midJoint;                                // 24       4  mid joint (TwoBone only)
    uint16_t maxIterations = 10;                      // 28       2  CCD/FABRIK max iterations
    IkSolverType solverType = IkSolverType::TwoBone;  // 30       1
    uint8_t enabled = 1;                              // 31       1  quick toggle
};  // total: 32 bytes
static_assert(sizeof(IkChainDef) == 32);

struct IkChainsComponent
{
    memory::InlinedVector<IkChainDef, 4> chains;  // up to 4 chains inline
};

struct IkTarget  // offset  size
{
    math::Vec3 position{0.0f};           //  0      12  world-space target
    math::Quat orientation{1, 0, 0, 0};  // 12      16  optional target orientation
    uint8_t hasOrientation = 0;          // 28       1  apply orientation override?
    uint8_t _pad[3] = {};                // 29       3
};  // total: 32 bytes
static_assert(sizeof(IkTarget) == 32);

struct IkTargetsComponent
{
    memory::InlinedVector<IkTarget, 4> targets;  // parallel to IkChainsComponent::chains
};

struct PoseComponent
{
    Pose* pose = nullptr;  // arena-allocated, valid for one frame
};

}  // namespace engine::animation
