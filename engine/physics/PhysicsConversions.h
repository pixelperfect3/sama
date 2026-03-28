#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>

#include "engine/math/Types.h"

namespace engine::physics
{

inline JPH::Vec3 toJolt(const math::Vec3& v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

inline JPH::Quat toJolt(const math::Quat& q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

inline math::Vec3 fromJolt(const JPH::Vec3& v)
{
    return math::Vec3(v.GetX(), v.GetY(), v.GetZ());
}

inline math::Quat fromJolt(const JPH::Quat& q)
{
    return math::Quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

}  // namespace engine::physics
