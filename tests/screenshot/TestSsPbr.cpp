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

TEST_CASE("screenshot: PBR lit cube", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle prog = bgfx::ProgramHandle{engine::rendering::loadPbrProgram().idx};
    engine::rendering::RenderResources res;
    uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));
    const engine::rendering::Mesh& mesh = *res.getMesh(meshId);

    // Camera
    auto view = glm::lookAt(glm::vec3(0, 1, 4), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(60.0f), static_cast<float>(fx.width()) / fx.height(),
                                 0.1f, 100.0f);

    engine::rendering::RenderPass(engine::rendering::kViewOpaque)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x202020ff)
        .transform(view, proj);

    // Set material uniform: [0]={albedo.rgb, roughness} [1]={metallic, emissiveScale, 0, 0}
    float matData[8] = {0.7f, 0.7f, 0.7f, 0.5f,   // albedo + roughness
                        0.0f, 0.0f, 0.0f, 0.0f};  // metallic + emissiveScale
    bgfx::setUniform(uniforms.u_material, matData, 2);

    // Set directional light: [0]={dir.xyz, 0} [1]={color*intensity.xyz, 0}
    glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, -2.0f, 1.0f));
    float lightData[8] = {lightDir.x, lightDir.y, lightDir.z, 0.0f,   // direction
                          1.0f,       1.0f,       1.0f,       0.0f};  // white light
    bgfx::setUniform(uniforms.u_dirLight, lightData, 2);

    // Bind white defaults for unset texture slots so unbound samplers
    // don't produce zero (black albedo, zero AO) on Metal.
    bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
    bgfx::setTexture(1, uniforms.s_normal, fx.neutralNormalTex());
    bgfx::setTexture(2, uniforms.s_orm, fx.whiteTex());

    // Draw cube
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, mesh.positionVbh);
    bgfx::setVertexBuffer(1, mesh.surfaceVbh);
    bgfx::setIndexBuffer(mesh.ibh);
    bgfx::setState(BGFX_STATE_DEFAULT);
    bgfx::submit(engine::rendering::kViewOpaque, prog);

    auto pixels = fx.captureFrame();

    if (bgfx::isValid(prog))
        bgfx::destroy(prog);
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(
        engine::screenshot::compareOrUpdateGolden("pbr_lit_cube", pixels, fx.width(), fx.height()));
}
