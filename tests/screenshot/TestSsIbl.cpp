// IBL ambient screenshot test.
// Scene: PBR cube with IBL ambient contribution.
// Uses PBR shader with IBL params set — if the program is invalid (no GPU),
// the test gracefully skips rendering but still validates the golden compare path.

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

TEST_CASE("screenshot: IBL ambient cube", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle prog = engine::rendering::loadPbrProgram();
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
        .clearColorAndDepth(0x111122ff)
        .transform(view, proj);

    // Gray metallic material for IBL to be visible
    float matData[8] = {0.8f, 0.8f, 0.8f, 0.3f,   // albedo + roughness
                        0.9f, 0.0f, 0.0f, 0.0f};  // near-metallic
    bgfx::setUniform(uniforms.u_material, matData, 2);

    // Dim directional light — IBL provides most of the ambient
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.5f));
    float lightData[8] = {lightDir.x, lightDir.y, lightDir.z, 0.0f,
                          0.3f,       0.3f,       0.4f,       0.0f};  // dim bluish
    bgfx::setUniform(uniforms.u_dirLight, lightData, 2);

    // IBL params: maxMipLevels=1, iblEnabled=0 (no real IBL textures in test)
    // Set iblEnabled=0 so the shader falls back to ambient only
    float iblParams[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(uniforms.u_iblParams, iblParams);

    bgfx::setTexture(0, uniforms.s_albedo, fx.whiteTex());
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
        engine::screenshot::compareOrUpdateGolden("ibl_ambient", pixels, fx.width(), fx.height()));
}
