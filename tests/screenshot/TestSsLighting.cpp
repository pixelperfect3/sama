// Clustered lighting screenshot test.
// Scene: PBR cube with a directional light to show color variation.
// Point-light cluster texture setup is noted as future work — for now uses PBR
// with a directional light to produce a meaningful lit scene.

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

TEST_CASE("screenshot: clustered lights scene", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    engine::rendering::ProgramHandle prog = engine::rendering::loadPbrProgram();
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
        .clearColorAndDepth(0x101010ff)
        .transform(view, proj);

    // Gray PBR material
    float matData[8] = {0.7f, 0.7f, 0.7f, 0.4f,   // albedo + roughness
                        0.0f, 0.0f, 0.0f, 0.0f};  // metallic + emissive
    bgfx::setUniform(uniforms.u_material, matData, 2);

    // Warm directional light from upper-right-front (toward surface from light).
    // u_dirLight[0].xyz must point FROM the surface TOWARD the light (see fs_pbr.sc).
    // Using (1,1,0.5) lights the three camera-facing faces of the unit cube:
    //   front (+Z) NdotL≈0.41, top (+Y) NdotL≈0.82, right (+X) NdotL≈0.41.
    glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 1.0f, 0.5f));
    float lightData[8] = {lightDir.x, lightDir.y, lightDir.z, 0.0f,
                          1.0f,       0.9f,       0.8f,       0.0f};  // warm white
    bgfx::setUniform(uniforms.u_dirLight, lightData, 2);

    bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
    bgfx::setTexture(2, uniforms.s_orm, fx.whiteTex());

    // Draw cube
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, mesh.positionVbh);
    bgfx::setVertexBuffer(1, mesh.surfaceVbh);
    bgfx::setIndexBuffer(mesh.ibh);
    bgfx::setState(BGFX_STATE_DEFAULT);
    bgfx::submit(engine::rendering::kViewOpaque, bgfx::ProgramHandle{prog.idx});

    auto pixels = fx.captureFrame();

    if (engine::rendering::isValid(prog))
        bgfx::destroy(bgfx::ProgramHandle{prog.idx});
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(engine::screenshot::compareOrUpdateGolden("clustered_lights", pixels, fx.width(),
                                                      fx.height()));
}
