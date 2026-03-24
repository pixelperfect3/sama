#pragma once

#include <bgfx/bgfx.h>

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// ShaderUniforms — all bgfx uniform handles created once at renderer startup.
//
// Every uniform used across the entire rendering pipeline lives here.
// Handles are created in init() and destroyed in destroy().  No other code
// calls bgfx::createUniform for these names — keeping creation centralised
// prevents accidental name mismatches and duplicate handle leaks.
//
// Uniform names are case-sensitive and must match the names declared in .sc
// shader sources exactly.
//
// Phases that introduce new uniforms should add their handles here and extend
// init() / destroy() — never create transient uniforms per-frame.
//
// Sampler slot assignments (fixed, documented to avoid conflicts):
//   slot 0  — s_albedo
//   slot 1  — s_normal
//   slot 2  — s_orm
//   slot 5  — s_shadowMap     (Phase 4)
//   slot 6  — s_irradiance    (Phase 11)
//   slot 7  — s_prefiltered   (Phase 11)
//   slot 8  — s_brdfLut       (Phase 11)
//   slot 9  — s_depth         (Phase 8)
//   slot 10 — s_ssaoMap       (Phase 8)
//   slot 12 — s_lightData     (Phase 6)
//   slot 13 — s_lightGrid     (Phase 6)
//   slot 14 — s_lightIndex    (Phase 6)
// ---------------------------------------------------------------------------

struct ShaderUniforms
{
    // -----------------------------------------------------------------------
    // Frame-level
    // -----------------------------------------------------------------------

    // vec4[2]: [0]={width, height, near, far}  [1]={time, dt, 0, 0}
    bgfx::UniformHandle u_frameParams;

    // mat4: inverse projection matrix (used by SSAO — Phase 8)
    bgfx::UniformHandle u_invProj;

    // -----------------------------------------------------------------------
    // Material (Phase 3)
    // -----------------------------------------------------------------------

    // vec4[2]: [0]={albedo.rgb, roughness}  [1]={metallic, emissiveScale, 0, 0}
    bgfx::UniformHandle u_material;

    // -----------------------------------------------------------------------
    // Directional light (Phase 3)
    // -----------------------------------------------------------------------

    // vec4[2]: [0]={dir.xyz, 0}  [1]={color.rgb*intensity, 0}
    bgfx::UniformHandle u_dirLight;

    // -----------------------------------------------------------------------
    // Samplers (Phase 3+)
    // -----------------------------------------------------------------------

    bgfx::UniformHandle s_albedo;  // slot 0
    bgfx::UniformHandle s_normal;  // slot 1
    bgfx::UniformHandle s_orm;     // slot 2

    // -----------------------------------------------------------------------
    // Shadow (Phase 4)
    // -----------------------------------------------------------------------

    // mat4[4]: one matrix per cascade
    bgfx::UniformHandle u_shadowMatrix;

    // vec4: split distances along Z
    bgfx::UniformHandle u_cascadeSplits;

    bgfx::UniformHandle s_shadowMap;  // slot 5

    // -----------------------------------------------------------------------
    // Bloom (Phase 7)
    // -----------------------------------------------------------------------

    // vec4: {threshold, intensity, 0, 0}
    bgfx::UniformHandle u_bloomParams;

    // -----------------------------------------------------------------------
    // SSAO (Phase 8)
    // -----------------------------------------------------------------------

    // vec4[16]: hemisphere sample kernel
    bgfx::UniformHandle u_ssaoKernel;

    // vec4: {radius, bias, 0, 0}
    bgfx::UniformHandle u_ssaoParams;

    bgfx::UniformHandle s_depth;    // slot 9
    bgfx::UniformHandle s_ssaoMap;  // slot 10

    // -----------------------------------------------------------------------
    // Clustered lighting (Phase 6)
    // Light data is uploaded as RGBA32F textures instead of uniform arrays to
    // avoid the bgfx uniform array size limit on some backends.
    // -----------------------------------------------------------------------

    bgfx::UniformHandle s_lightData;    // slot 12 — RGBA32F 256×4, one light per row
    bgfx::UniformHandle s_lightGrid;    // slot 13 — cluster grid (offset+count per tile)
    bgfx::UniformHandle s_lightIndex;   // slot 14 — flat light index list
    bgfx::UniformHandle u_lightParams;  // vec4: {numLights, screenW, screenH, 0}

    // -----------------------------------------------------------------------
    // IBL (Phase 11)
    // -----------------------------------------------------------------------

    bgfx::UniformHandle s_irradiance;   // slot 6
    bgfx::UniformHandle s_prefiltered;  // slot 7
    bgfx::UniformHandle s_brdfLut;      // slot 8

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    // Create all bgfx uniform handles.  Must be called after bgfx::init().
    void init();

    // Destroy all bgfx uniform handles.  Must be called before bgfx::shutdown().
    void destroy();
};

}  // namespace engine::rendering
