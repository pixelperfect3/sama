#include "engine/rendering/CsmSplitCalculator.h"

#include <cfloat>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace engine::rendering
{

CsmSplits computeCsmSplits(uint32_t cascadeCount, float cameraNear, float cameraFar, float lambda)
{
    CsmSplits result;
    result.nearPlane = cameraNear;
    result.count = cascadeCount;

    for (uint32_t i = 1; i <= cascadeCount; ++i)
    {
        float p = static_cast<float>(i) / static_cast<float>(cascadeCount);
        float cLog = cameraNear * std::pow(cameraFar / cameraNear, p);
        float cUni = cameraNear + (cameraFar - cameraNear) * p;
        result.splitDistances[i - 1] = lambda * cLog + (1.0f - lambda) * cUni;
    }

    return result;
}

math::Mat4 cascadeLightProj(const math::Mat4& cameraView, const math::Mat4& lightView, float fovY,
                            float aspectRatio, float nearSplit, float farSplit)
{
    float tanHalfFov = std::tan(fovY * 0.5f);
    float nearH = nearSplit * tanHalfFov;
    float nearW = nearH * aspectRatio;
    float farH = farSplit * tanHalfFov;
    float farW = farH * aspectRatio;

    // 8 corners of the camera sub-frustum slice in camera view space.
    // GLM view space has -Z forward, so near/far distances become negative Z.
    math::Vec3 corners[8] = {
        {-nearW, nearH, -nearSplit},  {nearW, nearH, -nearSplit}, {nearW, -nearH, -nearSplit},
        {-nearW, -nearH, -nearSplit}, {-farW, farH, -farSplit},   {farW, farH, -farSplit},
        {farW, -farH, -farSplit},     {-farW, -farH, -farSplit},
    };

    // Transform corners: camera view space -> world space -> light view space.
    math::Mat4 invCamView = glm::inverse(cameraView);
    math::Mat4 camToLight = lightView * invCamView;

    math::Vec3 mn(FLT_MAX);
    math::Vec3 mx(-FLT_MAX);

    for (auto& c : corners)
    {
        math::Vec3 lc = math::Vec3(camToLight * math::Vec4(c, 1.0f));
        mn = glm::min(mn, lc);
        mx = glm::max(mx, lc);
    }

    return glm::ortho(mn.x, mx.x, mn.y, mx.y, mn.z, mx.z);
}

}  // namespace engine::rendering
