#pragma once

#include <cstdint>

#include "engine/animation/IkComponents.h"
#include "engine/math/Types.h"
#include "engine/memory/InlinedVector.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// LookAtHelper -- sets up a CCD chain from spine base to head for look-at
// targeting.
//
// Usage:
//   1. Configure the chain joints (spine1 -> spine2 -> neck -> head).
//   2. Set per-joint weight distribution (e.g. 0.1, 0.2, 0.3, 0.4).
//   3. Call setupChain() each frame with the look-at point.
// ---------------------------------------------------------------------------

struct LookAtHelper
{
    // Chain from spine base to head (typically 3-5 joints).
    memory::InlinedVector<uint32_t, 6> chainJoints;

    // Per-joint weight distribution (how much each joint contributes).
    // Should sum to approximately 1.0.
    memory::InlinedVector<float, 6> jointWeights;

    // Compute the IK chain def and target for look-at.
    // Sets up a CCD chain with the look-at point as target.
    void setupChain(IkChainDef& outChain, IkTarget& outTarget, const math::Vec3& lookAtPoint) const
    {
        if (chainJoints.empty())
        {
            return;
        }

        outChain.rootJoint = chainJoints.front();
        outChain.endEffectorJoint = chainJoints.back();
        outChain.solverType = IkSolverType::Ccd;
        outChain.maxIterations = 8;
        outChain.enabled = 1;

        outTarget.position = lookAtPoint;
        outTarget.hasOrientation = 0;
    }
};

}  // namespace engine::animation
