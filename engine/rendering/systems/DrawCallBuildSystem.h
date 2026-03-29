#pragma once

#include <bgfx/bgfx.h>

#include "engine/ecs/Registry.h"
#include "engine/math/Types.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderUniforms.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// PbrFrameParams — frame-level data for the PBR + shadow draw pass.
//
// bgfx resets all per-draw state (uniforms, textures) after every submit(),
// so these values must be re-uploaded before each draw call alongside
// the per-object u_material.
// ---------------------------------------------------------------------------

struct PbrFrameParams
{
    const float* lightData;           // 8 floats: u_dirLight[2]  {dir.xyz,0} + {col*intensity,0}
    const float* shadowMatrix;        // 16 floats: u_shadowMatrix[0]  world → shadow UV
    bgfx::TextureHandle shadowAtlas;  // s_shadowMap slot 5; BGFX_INVALID_HANDLE = no shadows
    uint16_t viewportW = 0;           // screen width  — drives u_lightParams/u_frameParams
    uint16_t viewportH = 0;           // screen height — drives u_lightParams/u_frameParams
    float nearPlane = 0.05f;          // camera near plane (world units)
    float farPlane = 100.0f;          // camera far plane  (world units)
    float camPos[3] = {0.f, 0.f, 0.f};  // camera world position — used for V vector in shader
};

// ---------------------------------------------------------------------------
// DrawCallBuildSystem — single-threaded draw call submission.
//
// Phase 2 (unlit):
//   update(reg, res, program)
//   Reads:  VisibleTag, WorldTransformComponent, MeshComponent
//   Submits draw calls to bgfx view kViewOpaque.
//
// Phase 3 (PBR, no shadows):
//   update(reg, res, program, uniforms*)
//   Also reads MaterialComponent; uploads per-draw u_material.
//   Frame-level uniforms (u_dirLight) must be set by the caller.
//
// Phase 3 (PBR + directional shadow):
//   update(reg, res, program, uniforms, PbrFrameParams)
//   Sets u_material, u_dirLight, u_shadowMatrix, s_albedo, s_orm, s_shadowMap
//   before each submit — no caller pre-setup required.
//
// Phase 4 (shadows):
//   submitShadowDrawCalls(reg, res, shadowProgram, cascadeIndex)
//   Depth-only pass: stream 0 only, no material, CCW cull.
// ---------------------------------------------------------------------------

class DrawCallBuildSystem
{
public:
    // Phase 2 — unlit.
    void update(ecs::Registry& reg, const RenderResources& res, bgfx::ProgramHandle program);

    // Phase 3 — PBR, per-draw u_material only.
    // Caller must set frame-level uniforms (u_dirLight) before this call.
    void update(ecs::Registry& reg, const RenderResources& res, bgfx::ProgramHandle program,
                ShaderUniforms* uniforms);

    // Phase 3 — PBR + directional shadow.
    // Sets all required per-draw state: material, light, shadow matrix, and
    // all texture slots.  No caller pre-setup needed.
    void update(ecs::Registry& reg, const RenderResources& res, bgfx::ProgramHandle program,
                const ShaderUniforms& uniforms, const PbrFrameParams& frame);

    // Phase 4 — depth-only shadow pass.
    void submitShadowDrawCalls(ecs::Registry& reg, const RenderResources& res,
                               bgfx::ProgramHandle shadowProgram, uint32_t cascadeIndex);

    // PBR + skeletal animation: submit skinned entities.
    // boneBuffer points to the AnimationSystem's per-frame bone matrix array.
    // skinnedProgram is the PBR vertex shader with GPU skinning support.
    void updateSkinned(ecs::Registry& reg, const RenderResources& res,
                       bgfx::ProgramHandle skinnedProgram, const ShaderUniforms& uniforms,
                       const PbrFrameParams& frame, const math::Mat4* boneBuffer);

    // Shadow pass for skinned entities — uses bone matrices for correct
    // animated shadow depth.
    void submitSkinnedShadowDrawCalls(ecs::Registry& reg, const RenderResources& res,
                                      bgfx::ProgramHandle skinnedShadowProgram,
                                      uint32_t cascadeIndex, const math::Mat4* boneBuffer);
};

}  // namespace engine::rendering
