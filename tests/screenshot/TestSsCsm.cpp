// CSM (Cascaded Shadow Maps) screenshot test.
// Scene: wide plane + tall box, PBR shading without cascade shadow map.
// Note: Full 3-cascade CSM setup (CsmSplitCalculator + ShadowRenderer + shadow atlas)
// requires complex per-test initialization. This simplified test renders the scene
// geometry with PBR + directional light, validating consistent visual output.
// CSM-specific shadow integration is future work once the fixture supports
// multi-view shadow passes.

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ViewIds.h"

TEST_CASE("screenshot: CSM scene", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle prog = engine::rendering::loadPbrProgram();
    engine::rendering::RenderResources res;
    uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));
    const engine::rendering::Mesh& mesh = *res.getMesh(meshId);

    // Camera at (0, 5, 15) looking at origin
    auto view = glm::lookAt(glm::vec3(0, 5, 15), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(45.0f), static_cast<float>(fx.width()) / fx.height(),
                                 0.1f, 200.0f);

    engine::rendering::RenderPass(engine::rendering::kViewOpaque)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x507090ff)
        .transform(view, proj);

    glm::vec3 lightDir = glm::normalize(glm::vec3(0.3f, -1.0f, 0.5f));
    float lightData[8] = {lightDir.x, lightDir.y, lightDir.z, 0.0f, 1.0f, 0.95f, 0.9f, 0.0f};
    bgfx::setUniform(uniforms.u_dirLight, lightData, 2);

    // Wide flat plane (ground)
    {
        float matData[8] = {0.5f, 0.45f, 0.4f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(uniforms.u_material, matData, 2);
        bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
        bgfx::setTexture(2, uniforms.s_orm, fx.whiteTex());

        auto model = glm::scale(glm::mat4(1.0f), glm::vec3(20.0f, 0.2f, 20.0f));
        float mtx[16] = {};
        memcpy(mtx, &model[0][0], sizeof(float) * 16);
        bgfx::setTransform(mtx);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setVertexBuffer(1, mesh.surfaceVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_DEFAULT);
        bgfx::submit(engine::rendering::kViewOpaque, prog);
    }

    // Tall box
    {
        float matData[8] = {0.6f, 0.5f, 0.4f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(uniforms.u_material, matData, 2);
        bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
        bgfx::setTexture(2, uniforms.s_orm, fx.whiteTex());

        auto model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, 0.0f)),
                                glm::vec3(1.0f, 3.0f, 1.0f));
        float mtx[16] = {};
        memcpy(mtx, &model[0][0], sizeof(float) * 16);
        bgfx::setTransform(mtx);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setVertexBuffer(1, mesh.surfaceVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_DEFAULT);
        bgfx::submit(engine::rendering::kViewOpaque, prog);
    }

    auto pixels = fx.captureFrame();

    if (bgfx::isValid(prog))
        bgfx::destroy(prog);
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(
        engine::screenshot::compareOrUpdateGolden("csm_scene", pixels, fx.width(), fx.height()));
}
