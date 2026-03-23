#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/math/Types.h"

// ---------------------------------------------------------------------------
// Render-specific ECS components.
//
// Every component is laid out to eliminate implicit padding.  Fields are
// ordered largest-alignment-first; small fields are grouped at the end.
// static_assert on sizeof and offsetof catches accidental regressions at
// compile time.
//
// All of these components live in the same Registry as game-logic components.
// ---------------------------------------------------------------------------

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Tag components — presence in the SparseSet is the signal; no data array.
// ---------------------------------------------------------------------------

// Added by FrustumCullSystem when an entity is visible to the main camera.
// Removed by the same system when the entity falls outside the frustum.
struct VisibleTag
{
};
static_assert(sizeof(VisibleTag) == 1);

// cascadeMask: bit N set means the entity is visible to shadow cascade N.
// Set by ShadowCullSystem; read by DrawCallBuildSystem to skip shadow draw
// calls for cascades the entity is not visible to.
struct ShadowVisibleTag
{
    uint8_t cascadeMask;
};
static_assert(sizeof(ShadowVisibleTag) == 1);

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

enum class ProjectionType : uint8_t
{
    Perspective,
    Orthographic
};

struct CameraComponent  // offset  size
{
    float fovY;           //  0       4
    float nearPlane;      //  4       4
    float farPlane;       //  8       4
    float aspectRatio;    // 12       4
    ProjectionType type;  // 16       1
    uint8_t viewLayer;    // 17       1
    uint8_t _pad[2];      // 18       2  (explicit — documents intent)
};  // total:  20 bytes
static_assert(sizeof(CameraComponent) == 20);
static_assert(offsetof(CameraComponent, type) == 16);

// ---------------------------------------------------------------------------
// Mesh and Material handle components
// ---------------------------------------------------------------------------

// Handle-only components — split so FrustumCullSystem can query MeshComponent
// for AABB bounds without touching MaterialComponent.
struct MeshComponent
{
    uint32_t mesh;  // index into RenderResources mesh table
};
static_assert(sizeof(MeshComponent) == 4);

struct MaterialComponent
{
    uint32_t material;  // index into RenderResources material table
};
static_assert(sizeof(MaterialComponent) == 4);

// ---------------------------------------------------------------------------
// Light components
// ---------------------------------------------------------------------------

// Directional — one per scene (sun).
struct DirectionalLightComponent  // offset  size
{
    math::Vec3 direction;  //  0      12
    math::Vec3 color;      // 12      12
    float intensity;       // 24       4
    uint8_t flags;         // 28       1  bit 0: castShadows
    uint8_t _pad[3];       // 29       3
};  // total:  32 bytes
static_assert(sizeof(DirectionalLightComponent) == 32);

// Point — no angular attenuation.
struct PointLightComponent  // offset  size
{
    math::Vec3 color;  //  0      12
    float intensity;   // 12       4
    float radius;      // 16       4
};  // total:  20 bytes
static_assert(sizeof(PointLightComponent) == 20);

// Spot — stores cos(innerAngle) and cos(outerAngle), not raw angles.
// The shader computes dot(lightDir, fragDir) and compares to the precomputed
// cosines, avoiding acos() per pixel.
struct SpotLightComponent  // offset  size
{
    math::Vec3 direction;  //  0      12
    math::Vec3 color;      // 12      12
    float intensity;       // 24       4
    float cosInnerAngle;   // 28    4  precomputed: cos(innerAngle)
    float cosOuterAngle;   // 32    4  precomputed: cos(outerAngle)
    float radius;          // 36    4
};  // total: 40 bytes
static_assert(sizeof(SpotLightComponent) == 40);

// ---------------------------------------------------------------------------
// Instanced mesh
// ---------------------------------------------------------------------------

struct InstancedMeshComponent  // offset  size
{
    uint32_t mesh;             //  0       4
    uint32_t material;         //  4       4
    uint32_t instanceGroupId;  //  8       4
};  // total:  12 bytes
static_assert(sizeof(InstancedMeshComponent) == 12);

// ---------------------------------------------------------------------------
// Scene-graph transform components (written by TransformSystem, read by all
// render systems — no transform logic lives in the renderer itself).
// ---------------------------------------------------------------------------

// Local TRS — written by game code, read by TransformSystem.
// dirty flag in bit 0 of flags; set when position/rotation/scale changes.
struct TransformComponent  // offset  size
{
    math::Vec3 position;  //  0      12
    math::Quat rotation;  // 12      16
    math::Vec3 scale;     // 28      12
    uint8_t flags;        // 40       1  bit 0: dirty
    uint8_t _pad[3];      // 41       3
};  // total:  44 bytes
static_assert(sizeof(TransformComponent) == 44);
static_assert(offsetof(TransformComponent, rotation) == 12);

// World matrix — written by TransformSystem, read by all render systems.
// Mat4 is 16-byte aligned — SIMD-friendly, one cache line per entity.
struct WorldTransformComponent
{
    math::Mat4 matrix;  // 64 bytes, offset 0
};
static_assert(sizeof(WorldTransformComponent) == 64);

}  // namespace engine::rendering
