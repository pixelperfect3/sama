// Shadow scene screenshot test.
// Scene: an occluder cube at (0,2,0) casting a shadow on a 3×0.2×3 ground
// plane.  Two bgfx passes:
//   Pass 0  Shadow pass — depth-only into ShadowRenderer atlas (kViewShadowBase).
//   Pass 9  Opaque pass — PBR shader reading the shadow atlas via s_shadowMap.
// Light at (-4,8,-4) looking at origin.

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

TEST_CASE("screenshot: shadow cubes scene", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle shadowProg = engine::rendering::loadShadowProgram();
    bgfx::ProgramHandle pbrProg = engine::rendering::loadPbrProgram();

    engine::rendering::RenderResources res;
    res.setWhiteTexture(fx.whiteTex());
    res.setNeutralNormalTexture(fx.neutralNormalTex());

    uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));
    const engine::rendering::Mesh& mesh = *res.getMesh(meshId);

    // Camera at (0,4,6) looking at (0,1,0)
    auto view = glm::lookAt(glm::vec3(0, 4, 6), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(60.0f), static_cast<float>(fx.width()) / fx.height(),
                                 0.1f, 100.0f);

    // -----------------------------------------------------------------------
    // Shadow pass (view 0)
    // Light source at (-4, 8, -4); orthographic projection covers the scene.
    // -----------------------------------------------------------------------

    engine::rendering::ShadowRenderer shadow;
    engine::rendering::ShadowDesc shadowDesc;
    shadowDesc.resolution = 512;
    shadowDesc.cascadeCount = 1;
    shadow.init(shadowDesc);

    glm::vec3 lightPos(-4.0f, 8.0f, -4.0f);
    auto lightView = glm::lookAt(lightPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto lightProj = glm::ortho(-4.0f, 4.0f, -4.0f, 4.0f, 0.1f, 30.0f);

    shadow.beginCascade(0, lightView, lightProj);

    // Render occluder into shadow atlas — position stream only, depth write only.
    {
        auto model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
        float mtx[16] = {};
        memcpy(mtx, &model[0][0], sizeof(float) * 16);
        bgfx::setTransform(mtx);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(engine::rendering::kViewShadowBase, shadowProg);
    }

    // -----------------------------------------------------------------------
    // Opaque pass (view 9)
    // -----------------------------------------------------------------------

    engine::rendering::RenderPass(engine::rendering::kViewOpaque)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x404040ff)
        .transform(view, proj);

    // Shadow matrix and shadow atlas are set per-draw (bgfx state is consumed by submit).
    auto shadowMat = shadow.shadowMatrix(0);

    // Light direction: from surface toward light = normalize(lightPos - origin).
    glm::vec3 towardLight = glm::normalize(lightPos);
    float lightData[8] = {towardLight.x, towardLight.y, towardLight.z, 0.0f,
                          6.0f,          5.7f,          5.4f,          0.0f};

    // Draw occluder cube
    {
        float matData[8] = {0.7f, 0.7f, 0.7f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(uniforms.u_material, matData, 2);
        bgfx::setUniform(uniforms.u_dirLight, lightData, 2);
        bgfx::setUniform(uniforms.u_shadowMatrix, &shadowMat[0][0], 4);
        bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
        bgfx::setTexture(1, uniforms.s_normal, fx.neutralNormalTex());
        bgfx::setTexture(2, uniforms.s_orm, fx.whiteTex());
        bgfx::setTexture(5, uniforms.s_shadowMap, shadow.atlasTexture());

        auto model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
        float mtx[16] = {};
        memcpy(mtx, &model[0][0], sizeof(float) * 16);
        bgfx::setTransform(mtx);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setVertexBuffer(1, mesh.surfaceVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_DEFAULT);
        bgfx::submit(engine::rendering::kViewOpaque, pbrProg);
    }

    // Draw ground plane (shadow receiver)
    {
        float matData[8] = {0.6f, 0.6f, 0.6f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(uniforms.u_material, matData, 2);
        bgfx::setUniform(uniforms.u_dirLight, lightData, 2);
        bgfx::setUniform(uniforms.u_shadowMatrix, &shadowMat[0][0], 4);
        bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
        bgfx::setTexture(1, uniforms.s_normal, fx.neutralNormalTex());
        bgfx::setTexture(2, uniforms.s_orm, fx.whiteTex());
        bgfx::setTexture(5, uniforms.s_shadowMap, shadow.atlasTexture());

        auto model = glm::scale(glm::mat4(1.0f), glm::vec3(3.0f, 0.2f, 3.0f));
        float mtx[16] = {};
        memcpy(mtx, &model[0][0], sizeof(float) * 16);
        bgfx::setTransform(mtx);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setVertexBuffer(1, mesh.surfaceVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_DEFAULT);
        bgfx::submit(engine::rendering::kViewOpaque, pbrProg);
    }

    auto pixels = fx.captureFrame();

    shadow.shutdown();
    if (bgfx::isValid(shadowProg))
        bgfx::destroy(shadowProg);
    if (bgfx::isValid(pbrProg))
        bgfx::destroy(pbrProg);
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(engine::screenshot::compareOrUpdateGolden("shadow_cubes_scene", pixels, fx.width(),
                                                      fx.height()));
}
