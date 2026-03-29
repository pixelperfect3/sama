#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/math/Types.h"

namespace engine::animation
{

struct Joint                             // offset  size
{
    math::Mat4 inverseBindMatrix{1.0f};  //  0      64  mesh space -> bone-local space
    int32_t parentIndex = -1;            // 64       4  -1 = root joint (no parent)
    uint32_t nameHash = 0;              // 68       4  FNV-1a hash of joint name
    uint8_t _pad[8] = {};              // 72       8  trailing alignment padding
};  // total: 80 bytes (aligned to 16 for Mat4)
static_assert(sizeof(Joint) == 80);

struct Skeleton
{
    std::vector<Joint> joints;  // ordered such that parent always precedes child

#if !defined(NDEBUG)
    // Debug-only: human-readable joint names, parallel to joints[].
    // Populated by GltfLoader from glTF node names. Compiled out in
    // release builds (NDEBUG is defined by CMake/Xcode/MSVC/NDK in
    // Release mode).
    std::vector<std::string> debugJointNames;
#endif

    [[nodiscard]] uint32_t jointCount() const noexcept
    {
        return static_cast<uint32_t>(joints.size());
    }

    // Find joint by name hash. Returns index or -1 if not found.
    // Linear scan -- fine for <128 joints, called at setup time (not per-frame).
    [[nodiscard]] int32_t findJoint(uint32_t hash) const noexcept
    {
        for (uint32_t i = 0; i < joints.size(); ++i)
            if (joints[i].nameHash == hash)
                return static_cast<int32_t>(i);
        return -1;
    }
};

}  // namespace engine::animation
