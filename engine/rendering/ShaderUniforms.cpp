#include "engine/rendering/ShaderUniforms.h"

namespace engine::rendering
{

void ShaderUniforms::init()
{
    // Frame-level
    u_frameParams = bgfx::createUniform("u_frameParams", bgfx::UniformType::Vec4, 2);
    // u_invProj is a bgfx predefined uniform name — bgfx::createUniform will
    // return BGFX_INVALID_HANDLE for it.  Phase 8 (SSAO) will access the
    // predefined value directly; we leave this handle invalid until then.
    u_invProj = BGFX_INVALID_HANDLE;

    // Material
    u_material = bgfx::createUniform("u_material", bgfx::UniformType::Vec4, 2);

    // Directional light
    u_dirLight = bgfx::createUniform("u_dirLight", bgfx::UniformType::Vec4, 2);

    // Samplers — Phase 3
    s_albedo = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    s_normal = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
    s_orm = bgfx::createUniform("s_orm", bgfx::UniformType::Sampler);

    // Shadow — Phase 4
    u_shadowMatrix = bgfx::createUniform("u_shadowMatrix", bgfx::UniformType::Mat4, 4);
    u_cascadeSplits = bgfx::createUniform("u_cascadeSplits", bgfx::UniformType::Vec4);
    s_shadowMap = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);

    // Bloom / post-process — Phase 7
    u_bloomParams = bgfx::createUniform("u_bloomParams", bgfx::UniformType::Vec4);
    u_texelSize = bgfx::createUniform("u_texelSize", bgfx::UniformType::Vec4);
    s_hdrColor = bgfx::createUniform("s_hdrColor", bgfx::UniformType::Sampler);
    s_bloomTex = bgfx::createUniform("s_bloomTex", bgfx::UniformType::Sampler);
    s_bloomPrev = bgfx::createUniform("s_bloomPrev", bgfx::UniformType::Sampler);
    s_ldrColor = bgfx::createUniform("s_ldrColor", bgfx::UniformType::Sampler);

    // SSAO — Phase 8
    u_ssaoKernel = bgfx::createUniform("u_ssaoKernel", bgfx::UniformType::Vec4, 16);
    u_ssaoParams = bgfx::createUniform("u_ssaoParams", bgfx::UniformType::Vec4);
    s_depth = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
    s_ssaoMap = bgfx::createUniform("s_ssaoMap", bgfx::UniformType::Sampler);

    // Clustered lighting — Phase 6
    s_lightData = bgfx::createUniform("s_lightData", bgfx::UniformType::Sampler);
    s_lightGrid = bgfx::createUniform("s_lightGrid", bgfx::UniformType::Sampler);
    s_lightIndex = bgfx::createUniform("s_lightIndex", bgfx::UniformType::Sampler);
    u_lightParams = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);

    // IBL — Phase 11
    s_irradiance = bgfx::createUniform("s_irradiance", bgfx::UniformType::Sampler);
    s_prefiltered = bgfx::createUniform("s_prefiltered", bgfx::UniformType::Sampler);
    s_brdfLut = bgfx::createUniform("s_brdfLut", bgfx::UniformType::Sampler);
    u_iblParams = bgfx::createUniform("u_iblParams", bgfx::UniformType::Vec4);
}

void ShaderUniforms::destroy()
{
    // Guard every destroy with an isValid check.
    // Handles that were never created (predefined names, future-phase placeholders)
    // are left as BGFX_INVALID_HANDLE and must not be passed to bgfx::destroy.
    auto safeDestroy = [](bgfx::UniformHandle h)
    {
        if (bgfx::isValid(h))
            bgfx::destroy(h);
    };

    // Frame-level (u_invProj is a bgfx predefined — never created here)
    safeDestroy(u_frameParams);
    safeDestroy(u_invProj);

    // Material
    safeDestroy(u_material);

    // Directional light
    safeDestroy(u_dirLight);

    // Samplers — Phase 3
    safeDestroy(s_albedo);
    safeDestroy(s_normal);
    safeDestroy(s_orm);

    // Shadow — Phase 4
    safeDestroy(u_shadowMatrix);
    safeDestroy(u_cascadeSplits);
    safeDestroy(s_shadowMap);

    // Bloom / post-process — Phase 7
    safeDestroy(u_bloomParams);
    safeDestroy(u_texelSize);
    safeDestroy(s_hdrColor);
    safeDestroy(s_bloomTex);
    safeDestroy(s_bloomPrev);
    safeDestroy(s_ldrColor);

    // SSAO — Phase 8
    safeDestroy(u_ssaoKernel);
    safeDestroy(u_ssaoParams);
    safeDestroy(s_depth);
    safeDestroy(s_ssaoMap);

    // Clustered lighting — Phase 6
    safeDestroy(s_lightData);
    safeDestroy(s_lightGrid);
    safeDestroy(s_lightIndex);
    safeDestroy(u_lightParams);

    // IBL — Phase 11
    safeDestroy(s_irradiance);
    safeDestroy(s_prefiltered);
    safeDestroy(s_brdfLut);
    safeDestroy(u_iblParams);
}

}  // namespace engine::rendering
