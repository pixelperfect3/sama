// Hard-PCF (1-tap) shadow screenshot test — guards the low-tier shadow path.
//
// fs_pbr.sc takes a runtime branch on u_iblParams.z: when > 0.5 the shadow
// filter is a single shadow2D() sample (hardware bilinear depth-compare,
// 4-sample average for free); when <= 0.5 it does the 4-tap PCF 2x2.
//
// TestSsShadow exercises the 4-tap default path; this test exercises the
// 1-tap path the integrating team's low-tier preset will land on.  Without
// it, any future regression that breaks the runtime branch (typo in the
// uniform slot, accidental constant-fold, shader-permutation conflict)
// would slip through silently — TestSsShadow would still pass with the
// default branch, but the low-tier render would render with wrong shadow
// quality (visible as either over-soft or aliased shadow edges).
//
// Scene mirrors TestSsShadow exactly (occluder cube + ground plane, light
// at (-4, 8, -4)) so the only intentional difference between the two
// goldens is the PCF tap count — a side-by-side visual review reveals
// the harder shadow edge of the 1-tap path, which is the expected
// low-tier appearance.

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/math/Types.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ShadowRenderer.h"
#include "engine/rendering/ViewIds.h"

TEST_CASE("screenshot: shadow cubes scene (hard 1-tap PCF, low-tier path)", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    engine::rendering::ProgramHandle shadowProg = engine::rendering::loadShadowProgram();
    engine::rendering::ProgramHandle pbrProg = engine::rendering::loadPbrProgram();

    engine::rendering::RenderResources res;
    res.setWhiteTexture(engine::rendering::TextureHandle{fx.whiteTex().idx});
    res.setNeutralNormalTexture(engine::rendering::TextureHandle{fx.neutralNormalTex().idx});

    uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));
    const engine::rendering::Mesh& mesh = *res.getMesh(meshId);

    auto view = glm::lookAt(glm::vec3(0, 4, 6), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(60.0F), static_cast<float>(fx.width()) / fx.height(),
                                 0.1F, 100.0F);

    engine::rendering::ShadowRenderer shadow;
    engine::rendering::ShadowDesc shadowDesc;
    shadowDesc.resolution = 512;
    shadowDesc.cascadeCount = 1;
    shadow.init(shadowDesc);

    glm::vec3 lightPos(-4.0F, 8.0F, -4.0F);
    auto lightView = glm::lookAt(lightPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto lightProj = glm::ortho(-4.0F, 4.0F, -4.0F, 4.0F, 0.1F, 30.0F);

    shadow.beginCascade(0, lightView, lightProj);

    {
        auto model = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 2.0F, 0.0F));
        float mtx[16] = {};
        memcpy(mtx, &model[0][0], sizeof(float) * 16);
        bgfx::setTransform(mtx);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(engine::rendering::kViewShadowBase, bgfx::ProgramHandle{shadowProg.idx});
    }

    engine::rendering::RenderPass(engine::rendering::kViewOpaque)
        .framebuffer(fx.sceneFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x404040FF)
        .transform(view, proj);

    auto shadowMat = shadow.shadowMatrix(0);

    glm::vec3 towardLight = glm::normalize(lightPos);
    float lightData[8] = {towardLight.x, towardLight.y, towardLight.z, 0.0F,
                          6.0F,          5.7F,          5.4F,          0.0F};

    // KEY ASSERTION OF THIS TEST: set u_iblParams.z = 1.0 so fs_pbr.sc takes
    // the 1-tap path.  iblEnabled stays 0 (no IBL textures bound), so the
    // hemisphere ambient path runs as in TestSsShadow.  The only behavioral
    // difference between TestSsShadow and this test should be the PCF tap
    // count.
    float iblParams[4] = {1.0F, 0.0F, 1.0F, 0.0F};
    bgfx::setUniform(uniforms.u_iblParams, iblParams);

    {
        float matData[8] = {0.7F, 0.7F, 0.7F, 0.5F, 0.0F, 0.0F, 0.0F, 0.0F};
        bgfx::setUniform(uniforms.u_material, matData, 2);
        bgfx::setUniform(uniforms.u_dirLight, lightData, 2);
        bgfx::setUniform(uniforms.u_shadowMatrix, &shadowMat[0][0], 4);
        bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
        bgfx::setTexture(1, uniforms.s_normal, fx.neutralNormalTex());
        bgfx::setTexture(2, uniforms.s_orm, fx.whiteTex());
        bgfx::setTexture(5, uniforms.s_shadowMap, shadow.atlasTexture());

        auto model = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 2.0F, 0.0F));
        float mtx[16] = {};
        memcpy(mtx, &model[0][0], sizeof(float) * 16);
        bgfx::setTransform(mtx);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setVertexBuffer(1, mesh.surfaceVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_DEFAULT);
        bgfx::submit(engine::rendering::kViewOpaque, bgfx::ProgramHandle{pbrProg.idx});
    }

    {
        float matData[8] = {0.6F, 0.6F, 0.6F, 0.8F, 0.0F, 0.0F, 0.0F, 0.0F};
        bgfx::setUniform(uniforms.u_material, matData, 2);
        bgfx::setUniform(uniforms.u_dirLight, lightData, 2);
        bgfx::setUniform(uniforms.u_shadowMatrix, &shadowMat[0][0], 4);
        bgfx::setUniform(uniforms.u_iblParams, iblParams);
        bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
        bgfx::setTexture(1, uniforms.s_normal, fx.neutralNormalTex());
        bgfx::setTexture(2, uniforms.s_orm, fx.whiteTex());
        bgfx::setTexture(5, uniforms.s_shadowMap, shadow.atlasTexture());

        auto model = glm::scale(glm::mat4(1.0F), glm::vec3(3.0F, 0.2F, 3.0F));
        float mtx[16] = {};
        memcpy(mtx, &model[0][0], sizeof(float) * 16);
        bgfx::setTransform(mtx);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setVertexBuffer(1, mesh.surfaceVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_DEFAULT);
        bgfx::submit(engine::rendering::kViewOpaque, bgfx::ProgramHandle{pbrProg.idx});
    }

    fx.runTonemap(engine::rendering::kViewPostProcessBase);

    auto pixels = fx.captureFrame();

    shadow.shutdown();
    if (engine::rendering::isValid(shadowProg))
        bgfx::destroy(bgfx::ProgramHandle{shadowProg.idx});
    if (engine::rendering::isValid(pbrProg))
        bgfx::destroy(bgfx::ProgramHandle{pbrProg.idx});
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(engine::screenshot::compareOrUpdateGolden("shadow_cubes_scene_hard_pcf", pixels,
                                                      fx.width(), fx.height()));
}
