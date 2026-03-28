#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/math/Types.h"

namespace engine::physics
{

enum class BodyType : uint8_t
{
    Static,    // never moves (floors, walls)
    Dynamic,   // moved by physics simulation
    Kinematic  // moved by game code, pushes dynamic bodies
};

enum class ColliderShape : uint8_t
{
    Box,
    Sphere,
    Capsule,
    Mesh  // triangle mesh, static only
};

struct RigidBodyComponent  // offset  size
{
    uint32_t bodyID = ~0u;              //  0       4   Jolt BodyID, set by PhysicsSystem
    float mass = 1.0f;                  //  4       4
    float linearDamping = 0.05f;        //  8       4
    float angularDamping = 0.05f;       // 12       4
    float friction = 0.5f;              // 16       4
    float restitution = 0.3f;           // 20       4
    BodyType type = BodyType::Dynamic;  // 24   1
    uint8_t layer = 0;                  // 25       1   collision layer index
    uint8_t _pad[2] = {};               // 26       2
};  // total: 28 bytes
static_assert(sizeof(RigidBodyComponent) == 28);
static_assert(offsetof(RigidBodyComponent, mass) == 4);
static_assert(offsetof(RigidBodyComponent, type) == 24);

struct ColliderComponent  // offset  size
{
    math::Vec3 offset{0.0f};                   //  0      12
    math::Vec3 halfExtents{0.5f};              // 12      12
    float radius = 0.5f;                       // 24       4
    ColliderShape shape = ColliderShape::Box;  // 28       1
    uint8_t _pad[3] = {};                      // 29       3
};  // total: 32 bytes
static_assert(sizeof(ColliderComponent) == 32);
static_assert(offsetof(ColliderComponent, halfExtents) == 12);
static_assert(offsetof(ColliderComponent, radius) == 24);
static_assert(offsetof(ColliderComponent, shape) == 28);

struct PhysicsBodyCreatedTag
{
};

}  // namespace engine::physics
