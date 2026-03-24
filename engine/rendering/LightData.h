#pragma once

#include <cstdint>

#include "engine/math/Types.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// CPU-side light data written per frame before GPU upload.
//
// Layout matches the RGBA32F texture format used by LightClusterBuilder:
//   Row 0: position.xyz, radius
//   Row 1: color.xyz (premultiplied by intensity), type  (0=point, 1=spot)
//   Row 2: spotDirection.xyz, cosOuterAngle
//   Row 3: cosInnerAngle, 0, 0, 0
//
// Each field is 4 bytes; the struct is exactly 64 bytes (4 rows × 4 floats ×
// 4 bytes).  static_assert enforces this at compile time.
// ---------------------------------------------------------------------------

struct LightEntry
{
    math::Vec3 position;
    float radius;
    math::Vec3 color;  // premultiplied by intensity
    float type;        // 0 = point, 1 = spot
    math::Vec3 spotDirection;
    float cosOuterAngle;
    float cosInnerAngle;
    float _pad[3];
};
static_assert(sizeof(LightEntry) == 64);

// ---------------------------------------------------------------------------
// Cluster grid constants
//
// 16×9×24 matches a 16:9 viewport with 24 logarithmic depth slices.
// Total 3456 clusters; each cluster stores an offset into the flat index list
// and a count of lights assigned to it.
// ---------------------------------------------------------------------------

static constexpr uint32_t kMaxLights = 256;
static constexpr uint32_t kClusterX = 16;
static constexpr uint32_t kClusterY = 9;
static constexpr uint32_t kClusterZ = 24;
static constexpr uint32_t kClusterCount = kClusterX * kClusterY * kClusterZ;  // 3456
static constexpr uint32_t kMaxLightIndices = 8192;

// One entry per cluster — offset into the flat light-index array and count.
struct ClusterGridEntry
{
    uint32_t offset;
    uint32_t count;
};

}  // namespace engine::rendering
