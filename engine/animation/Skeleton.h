#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/math/Types.h"

namespace engine::animation
{

struct JointRestPose
{
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec3 scale{1.0f};
};

// ---------------------------------------------------------------------------
// Joint
//
// Kept as a value type for setup-time code that wants to read all fields of
// one bone at once (asset loaders, debug inspection, tests).  It is NOT used
// as the storage type inside `Skeleton` — see the hot/cold split there.
// docs/PERF_AUDIT_2026-05-25.md item #M1 has the rationale.
// ---------------------------------------------------------------------------

struct Joint  // offset  size
{
    math::Mat4 inverseBindMatrix{1.0f};  //  0      64  mesh space -> bone-local space
    int32_t parentIndex = -1;            // 64       4  -1 = root joint (no parent)
    uint32_t nameHash = 0;               // 68       4  FNV-1a hash of joint name
    uint8_t _pad[8] = {};                // 72       8  trailing alignment padding
};  // total: 80 bytes (aligned to 16 for Mat4)
static_assert(sizeof(Joint) == 80);

// ---------------------------------------------------------------------------
// Skeleton — parallel-array storage (hot/cold split)
//
// The per-frame AnimationSystem hot loop walks every joint reading only
// `parentIndex` (4 B).  Storing joints as an array-of-structs of 80 B each
// meant the parent walk dragged in the full 64 B `inverseBindMatrix` on
// every fetch — ~20× cache-line traffic vs the data actually needed.
//
// We now keep three parallel arrays indexed by joint:
//   * parentIndices       — hot:  parent-chain walks in AnimationSystem +
//                                  IkSystem + IkSolvers (4 B/joint)
//   * inverseBindMatrices — warm: bone-matrix final compose in skinning
//                                  (64 B/joint, one full cache line each)
//   * nameHashes          — cold: findJoint() linear scan, called only at
//                                  setup (4 B/joint)
//
// The `Joint` struct above stays as a value type so non-hot consumers can
// still pass a "one bone's worth of data" around without juggling three
// arrays — `Skeleton::joint(i)` synthesises one on demand.
// docs/PERF_AUDIT_2026-05-25.md item #M1.
// ---------------------------------------------------------------------------

struct Skeleton
{
    // Ordered such that parent always precedes child — required by the
    // single-pass parent walk in AnimationSystem.
    std::vector<int32_t> parentIndices;
    std::vector<math::Mat4> inverseBindMatrices;
    std::vector<uint32_t> nameHashes;

    std::vector<JointRestPose> restPoses;  // parallel to the three above

#if !defined(NDEBUG)
    // Debug-only: human-readable joint names, parallel to the joint arrays.
    // Populated by GltfLoader from glTF node names. Compiled out in
    // release builds (NDEBUG is defined by CMake/Xcode/MSVC/NDK in
    // Release mode).
    std::vector<std::string> debugJointNames;
#endif

    [[nodiscard]] uint32_t jointCount() const noexcept
    {
        return static_cast<uint32_t>(parentIndices.size());
    }

    // Resize all four parallel arrays in lockstep.  Safer than three separate
    // resize() calls — drops one of the arrays out of sync at the call site
    // would silently corrupt later joint lookups.
    void resize(std::size_t n)
    {
        parentIndices.resize(n, -1);
        inverseBindMatrices.resize(n, math::Mat4(1.0f));
        nameHashes.resize(n, 0);
        restPoses.resize(n);
    }

    // Synthesise a Joint by value for setup-time / debug consumers.  Not for
    // hot paths — those should read the parallel arrays directly.
    [[nodiscard]] Joint joint(uint32_t i) const noexcept
    {
        Joint j;
        j.inverseBindMatrix = inverseBindMatrices[i];
        j.parentIndex = parentIndices[i];
        j.nameHash = nameHashes[i];
        return j;
    }

    // Find joint by name hash. Returns index or -1 if not found.
    // Linear scan -- fine for <128 joints, called at setup time (not per-frame).
    // Now walks `nameHashes` (4 B/joint contiguous) instead of striding through
    // 80-B Joint records — one cache line per 16 joints instead of 80 B / joint.
    [[nodiscard]] int32_t findJoint(uint32_t hash) const noexcept
    {
        for (uint32_t i = 0; i < nameHashes.size(); ++i)
        {
            if (nameHashes[i] == hash)
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }
};

}  // namespace engine::animation
