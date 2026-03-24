#pragma once

#include "engine/math/Types.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// CsmSplits — per-cascade near/far planes for Cascaded Shadow Maps.
//
// splitDistances[i] is the far plane of cascade i (positive, along the view
// direction away from the camera).  The near plane of cascade 0 is nearPlane;
// the near plane of cascade i>0 is splitDistances[i-1].
// ---------------------------------------------------------------------------

struct CsmSplits
{
    float nearPlane;          ///< Same as camera near plane
    float splitDistances[4];  ///< Far plane for each cascade (up to 4)
    uint32_t count;           ///< Number of active cascades
};

// ---------------------------------------------------------------------------
// computeCsmSplits — practical split scheme (Nvidia GPU Gems 3).
//
// Blends logarithmic and linear split distributions:
//   C_i = lambda * C_log_i + (1 - lambda) * C_uni_i
//
// lambda=0 => purely linear (equal-size slices)
// lambda=1 => purely logarithmic (geometric growth, finer near splits)
//
// cameraNear / cameraFar: camera frustum Z extents (positive values).
// cascadeCount: number of cascades, 1–4.
// ---------------------------------------------------------------------------

CsmSplits computeCsmSplits(uint32_t cascadeCount, float cameraNear, float cameraFar, float lambda);

// ---------------------------------------------------------------------------
// cascadeLightProj — tight orthographic projection for a single cascade.
//
// Fits the ortho box around the camera sub-frustum slice [nearSplit, farSplit]
// transformed into light view space, returning a projection matrix that can be
// used directly as the shadow light projection.
//
// cameraView:  view matrix of the main (scene) camera
// lightView:   view matrix of the directional light
// fovY:        vertical field of view of the camera (radians)
// aspectRatio: camera aspect ratio (width / height)
// nearSplit:   near plane of this cascade (positive distance from camera)
// farSplit:    far plane of this cascade (positive distance from camera)
// ---------------------------------------------------------------------------

math::Mat4 cascadeLightProj(const math::Mat4& cameraView, const math::Mat4& lightView, float fovY,
                            float aspectRatio, float nearSplit, float farSplit);

}  // namespace engine::rendering
