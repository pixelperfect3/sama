#include "engine/rendering/LightClusterBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>

#include "engine/rendering/EcsComponents.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// init / shutdown
// ---------------------------------------------------------------------------

void LightClusterBuilder::init()
{
    // Light data: 256 columns × 4 rows, RGBA32F.
    // Row 0 = position/radius, row 1 = color/type,
    // row 2 = spotDir/cosOuter, row 3 = cosInner/pad.
    lightDataTex_ = bgfx::createTexture2D(static_cast<uint16_t>(kMaxLights), 4, false, 1,
                                          bgfx::TextureFormat::RGBA32F,
                                          BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);

    // Grid: 3456 texels × 1 row, RGBA32F (x=offset, y=count, zw unused).
    lightGridTex_ = bgfx::createTexture2D(static_cast<uint16_t>(kClusterCount), 1, false, 1,
                                          bgfx::TextureFormat::RGBA32F,
                                          BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);

    // Index list: 8192 texels × 1 row, R32F.
    lightIndexTex_ = bgfx::createTexture2D(static_cast<uint16_t>(kMaxLightIndices), 1, false, 1,
                                           bgfx::TextureFormat::R32F,
                                           BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
}

void LightClusterBuilder::shutdown()
{
    if (bgfx::isValid(lightIndexTex_))
    {
        bgfx::destroy(lightIndexTex_);
        lightIndexTex_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightGridTex_))
    {
        bgfx::destroy(lightGridTex_);
        lightGridTex_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightDataTex_))
    {
        bgfx::destroy(lightDataTex_);
        lightDataTex_ = BGFX_INVALID_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// update — main entry point called once per frame
// ---------------------------------------------------------------------------

void LightClusterBuilder::update(ecs::Registry& reg, const math::Mat4& viewMatrix,
                                 const math::Mat4& projMatrix, float nearPlane, float farPlane,
                                 uint16_t screenWidth, uint16_t screenHeight)
{
    lightCount_ = collectLights(reg, viewMatrix);
    buildClusters(nearPlane, farPlane, screenWidth, screenHeight, projMatrix);
    uploadTextures();
}

// ---------------------------------------------------------------------------
// collectLights
//
// Gather PointLightComponent and SpotLightComponent entities into lights_[].
// Lights are sorted spot-before-point, then by ascending distance from the
// camera origin (view-space origin = (0,0,0)).  Cap at kMaxLights.
// ---------------------------------------------------------------------------

uint32_t LightClusterBuilder::collectLights(ecs::Registry& reg, const math::Mat4& viewMatrix)
{
    struct CandidateLight
    {
        LightEntry entry;
        float distSq;  // squared distance from camera for sorting
        int priority;  // 0=spot, 1=point (lower = higher priority)
    };

    // Use a temporary vector for sorting; its size is bounded by kMaxLights
    // plus any overflow that we discard.  We collect at most kMaxLights+1 to
    // detect overflow, then truncate.
    static std::array<CandidateLight, kMaxLights> candidates{};
    uint32_t candidateCount = 0;

    // --- Point lights ---
    auto pointView = reg.view<WorldTransformComponent, PointLightComponent>();
    pointView.each(
        [&](ecs::EntityID /*id*/, const WorldTransformComponent& xform,
            const PointLightComponent& pl)
        {
            if (candidateCount >= kMaxLights)
                return;

            math::Vec4 worldPos4 = math::Vec4(xform.matrix[3]);
            math::Vec4 viewPos4 = viewMatrix * worldPos4;
            math::Vec3 viewPos = math::Vec3(viewPos4);

            CandidateLight& c = candidates[candidateCount++];
            c.entry.position = viewPos;
            c.entry.radius = pl.radius;
            c.entry.color = pl.color * pl.intensity;
            c.entry.type = 0.0f;
            c.entry.spotDirection = math::Vec3(0.0f, 0.0f, -1.0f);
            c.entry.cosOuterAngle = -1.0f;
            c.entry.cosInnerAngle = -1.0f;
            c.entry._pad[0] = 0.0f;
            c.entry._pad[1] = 0.0f;
            c.entry._pad[2] = 0.0f;
            c.distSq = glm::dot(viewPos, viewPos);
            c.priority = 1;
        });

    // --- Spot lights ---
    auto spotView = reg.view<WorldTransformComponent, SpotLightComponent>();
    spotView.each(
        [&](ecs::EntityID /*id*/, const WorldTransformComponent& xform,
            const SpotLightComponent& sl)
        {
            if (candidateCount >= kMaxLights)
                return;

            math::Vec4 worldPos4 = math::Vec4(xform.matrix[3]);
            math::Vec4 viewPos4 = viewMatrix * worldPos4;
            math::Vec3 viewPos = math::Vec3(viewPos4);

            // Transform the spot direction (no translation — use upper-left 3×3).
            math::Vec3 worldDir = sl.direction;
            math::Vec4 viewDir4 = viewMatrix * math::Vec4(worldDir, 0.0f);
            math::Vec3 viewDir = glm::normalize(math::Vec3(viewDir4));

            CandidateLight& c = candidates[candidateCount++];
            c.entry.position = viewPos;
            c.entry.radius = sl.radius;
            c.entry.color = sl.color * sl.intensity;
            c.entry.type = 1.0f;
            c.entry.spotDirection = viewDir;
            c.entry.cosOuterAngle = sl.cosOuterAngle;
            c.entry.cosInnerAngle = sl.cosInnerAngle;
            c.entry._pad[0] = 0.0f;
            c.entry._pad[1] = 0.0f;
            c.entry._pad[2] = 0.0f;
            c.distSq = glm::dot(viewPos, viewPos);
            c.priority = 0;
        });

    // Sort: spot (priority=0) before point (priority=1), then nearest first.
    std::sort(candidates.begin(), candidates.begin() + static_cast<ptrdiff_t>(candidateCount),
              [](const CandidateLight& a, const CandidateLight& b)
              {
                  if (a.priority != b.priority)
                      return a.priority < b.priority;
                  return a.distSq < b.distSq;
              });

    for (uint32_t i = 0; i < candidateCount; ++i)
        lights_[i] = candidates[i].entry;

    return candidateCount;
}

// ---------------------------------------------------------------------------
// buildClusters
//
// For each of the 3456 clusters, compute its view-space AABB and test it
// against every light sphere.  Fills grid_[] and indices_[].
//
// X/Y cluster extents are derived from NDC tile boundaries un-projected via
// the inverse projection matrix.
// Z slices use exponential (log-depth) partitioning.
// ---------------------------------------------------------------------------

void LightClusterBuilder::buildClusters(float nearPlane, float farPlane, uint16_t screenWidth,
                                        uint16_t screenHeight, const math::Mat4& projMatrix)
{
    // --- Zero the grid ---
    for (auto& g : grid_)
    {
        g.offset = 0;
        g.count = 0;
    }

    if (lightCount_ == 0)
        return;

    // Inverse projection matrix — used to unproject NDC corners to view space.
    math::Mat4 invProj = glm::inverse(projMatrix);

    // Unproject NDC (x,y,z) to view space using the full inverse projection.
    // bgfx projections produce NDC z in [0,1].
    auto unprojectNdc = [&](float nx, float ny, float nz) -> math::Vec3
    {
        math::Vec4 clip = math::Vec4(nx, ny, nz, 1.0f);
        math::Vec4 view = invProj * clip;
        return math::Vec3(view) / view.w;
    };

    auto scaleToZ = [](const math::Vec3& v, float targetZ) -> math::Vec3
    {
        if (std::abs(v.z) < 1e-6f)
            return v;
        float t = targetZ / v.z;
        return v * t;
    };

    // --- Precompute cluster AABBs once (used by both count and fill passes) ---
    struct ClusterAABB
    {
        math::Vec3 aabbMin;
        math::Vec3 aabbMax;
    };
    std::array<ClusterAABB, kClusterCount> clusterAABBs{};

    for (uint32_t z = 0; z < kClusterZ; ++z)
    {
        float sliceNear =
            nearPlane *
            std::pow(farPlane / nearPlane, static_cast<float>(z) / static_cast<float>(kClusterZ));
        float sliceFar =
            nearPlane * std::pow(farPlane / nearPlane,
                                 static_cast<float>(z + 1) / static_cast<float>(kClusterZ));

        float nearZ = -sliceNear;  // right-handed: view-space Z is negative
        float farZ = -sliceFar;

        for (uint32_t y = 0; y < kClusterY; ++y)
        {
            float ndcYMin = 1.0f - static_cast<float>(y + 1) / static_cast<float>(kClusterY) * 2.0f;
            float ndcYMax = 1.0f - static_cast<float>(y) / static_cast<float>(kClusterY) * 2.0f;

            for (uint32_t x = 0; x < kClusterX; ++x)
            {
                float ndcXMin = static_cast<float>(x) / static_cast<float>(kClusterX) * 2.0f - 1.0f;
                float ndcXMax =
                    static_cast<float>(x + 1) / static_cast<float>(kClusterX) * 2.0f - 1.0f;

                math::Vec3 c000 = unprojectNdc(ndcXMin, ndcYMin, 0.0f);
                math::Vec3 c100 = unprojectNdc(ndcXMax, ndcYMin, 0.0f);
                math::Vec3 c010 = unprojectNdc(ndcXMin, ndcYMax, 0.0f);
                math::Vec3 c110 = unprojectNdc(ndcXMax, ndcYMax, 0.0f);
                math::Vec3 c001 = unprojectNdc(ndcXMin, ndcYMin, 1.0f);
                math::Vec3 c101 = unprojectNdc(ndcXMax, ndcYMin, 1.0f);
                math::Vec3 c011 = unprojectNdc(ndcXMin, ndcYMax, 1.0f);
                math::Vec3 c111 = unprojectNdc(ndcXMax, ndcYMax, 1.0f);

                math::Vec3 p[8] = {
                    scaleToZ(c000, nearZ), scaleToZ(c100, nearZ), scaleToZ(c010, nearZ),
                    scaleToZ(c110, nearZ), scaleToZ(c001, farZ),  scaleToZ(c101, farZ),
                    scaleToZ(c011, farZ),  scaleToZ(c111, farZ),
                };

                math::Vec3 aabbMin = p[0];
                math::Vec3 aabbMax = p[0];
                for (int k = 1; k < 8; ++k)
                {
                    aabbMin = glm::min(aabbMin, p[k]);
                    aabbMax = glm::max(aabbMax, p[k]);
                }

                uint32_t clusterIdx = z * (kClusterX * kClusterY) + y * kClusterX + x;
                clusterAABBs[clusterIdx] = {aabbMin, aabbMax};
            }
        }
    }

    // --- Count pass: count lights per cluster ---
    uint32_t totalIndices = 0;

    for (uint32_t ci = 0; ci < kClusterCount; ++ci)
    {
        const auto& aabb = clusterAABBs[ci];
        uint32_t count = 0;
        for (uint32_t li = 0; li < lightCount_; ++li)
        {
            const LightEntry& light = lights_[li];

            // Closest point on AABB to sphere center.
            math::Vec3 closest = glm::clamp(light.position, aabb.aabbMin, aabb.aabbMax);
            math::Vec3 delta = light.position - closest;
            float distSq = glm::dot(delta, delta);

            if (distSq <= light.radius * light.radius)
                ++count;
        }

        grid_[ci].count = count;
        totalIndices += count;
    }

    // --- Compute offsets (prefix sum) ---
    {
        uint32_t running = 0;
        for (uint32_t i = 0; i < kClusterCount; ++i)
        {
            grid_[i].offset = running;
            running += grid_[i].count;
            grid_[i].count = 0;  // reset — will be re-incremented in fill pass
        }
    }

    // Cap total indices to kMaxLightIndices.
    totalIndices = std::min(totalIndices, kMaxLightIndices);

    // --- Fill pass: write actual indices (reusing cached AABBs) ---
    for (uint32_t ci = 0; ci < kClusterCount; ++ci)
    {
        const auto& aabb = clusterAABBs[ci];
        uint32_t base = grid_[ci].offset;

        for (uint32_t li = 0; li < lightCount_; ++li)
        {
            const LightEntry& light = lights_[li];
            math::Vec3 closest = glm::clamp(light.position, aabb.aabbMin, aabb.aabbMax);
            math::Vec3 delta = light.position - closest;
            float distSq = glm::dot(delta, delta);

            if (distSq <= light.radius * light.radius)
            {
                uint32_t writeIdx = base + grid_[ci].count;
                if (writeIdx < kMaxLightIndices)
                {
                    indices_[writeIdx] = li;
                    ++grid_[ci].count;
                }
            }
        }
    }

    (void)screenWidth;
    (void)screenHeight;
}

// ---------------------------------------------------------------------------
// uploadTextures
// ---------------------------------------------------------------------------

void LightClusterBuilder::uploadTextures()
{
    // -----------------------------------------------------------------------
    // Light data texture: kMaxLights × 4 RGBA32F
    // Row 0: position.xyz, radius
    // Row 1: color.xyz,    type
    // Row 2: spotDir.xyz,  cosOuter
    // Row 3: cosInner, 0, 0, 0
    // -----------------------------------------------------------------------
    {
        static float buf[kMaxLights * 4 * 4];
        std::memset(buf, 0, sizeof(buf));

        for (uint32_t i = 0; i < lightCount_; ++i)
        {
            const LightEntry& l = lights_[i];

            float* r0 = buf + i * 4;                    // row 0
            float* r1 = buf + kMaxLights * 4 + i * 4;   // row 1
            float* r2 = buf + kMaxLights * 8 + i * 4;   // row 2
            float* r3 = buf + kMaxLights * 12 + i * 4;  // row 3

            r0[0] = l.position.x;
            r0[1] = l.position.y;
            r0[2] = l.position.z;
            r0[3] = l.radius;

            r1[0] = l.color.x;
            r1[1] = l.color.y;
            r1[2] = l.color.z;
            r1[3] = l.type;

            r2[0] = l.spotDirection.x;
            r2[1] = l.spotDirection.y;
            r2[2] = l.spotDirection.z;
            r2[3] = l.cosOuterAngle;

            r3[0] = l.cosInnerAngle;
            r3[1] = 0.0f;
            r3[2] = 0.0f;
            r3[3] = 0.0f;
        }

        if (bgfx::isValid(lightDataTex_))
        {
            bgfx::updateTexture2D(lightDataTex_, 0, 0, 0, 0, static_cast<uint16_t>(kMaxLights), 4,
                                  bgfx::copy(buf, sizeof(buf)));
        }
    }

    // -----------------------------------------------------------------------
    // Grid texture: kClusterCount × 1 RGBA32F
    // .x = offset, .y = count, .z = 0, .w = 0
    // -----------------------------------------------------------------------
    {
        static float gridBuf[kClusterCount * 4];
        for (uint32_t i = 0; i < kClusterCount; ++i)
        {
            gridBuf[i * 4 + 0] = static_cast<float>(grid_[i].offset);
            gridBuf[i * 4 + 1] = static_cast<float>(grid_[i].count);
            gridBuf[i * 4 + 2] = 0.0f;
            gridBuf[i * 4 + 3] = 0.0f;
        }

        if (bgfx::isValid(lightGridTex_))
        {
            bgfx::updateTexture2D(lightGridTex_, 0, 0, 0, 0, static_cast<uint16_t>(kClusterCount),
                                  1, bgfx::copy(gridBuf, sizeof(gridBuf)));
        }
    }

    // -----------------------------------------------------------------------
    // Index texture: kMaxLightIndices × 1 R32F
    // -----------------------------------------------------------------------
    {
        static float idxBuf[kMaxLightIndices];
        for (uint32_t i = 0; i < kMaxLightIndices; ++i)
            idxBuf[i] = static_cast<float>(indices_[i]);

        if (bgfx::isValid(lightIndexTex_))
        {
            bgfx::updateTexture2D(lightIndexTex_, 0, 0, 0, 0,
                                  static_cast<uint16_t>(kMaxLightIndices), 1,
                                  bgfx::copy(idxBuf, sizeof(idxBuf)));
        }
    }
}

}  // namespace engine::rendering
