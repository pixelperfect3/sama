// SSAO screenshot test.
// Scene: two cubes forming a corner — one upright at (0,0,0), one flat at (0,-0.6,0).
// Note: SSAO requires a dedicated depth buffer in a specific format and a separate
// SSAO pass (SsaoSystem). This simplified test renders the scene geometry directly
// into captureFb without the SSAO pass, validating consistent visual output.
// Full SSAO integration is future work once the fixture supports multi-pass setups.

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ViewIds.h"

TEST_CASE("screenshot: SSAO scene", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle prog = engine::rendering::loadPbrProgram();
    engine::rendering::RenderResources res;
    uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));
    const engine::rendering::Mesh& mesh = *res.getMesh(meshId);

    // Camera looking at the corner
    auto view =
        glm::lookAt(glm::vec3(1.5f, 1.5f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(60.0f), static_cast<float>(fx.width()) / fx.height(),
                                 0.1f, 100.0f);

    bgfx::setViewFrameBuffer(engine::rendering::kViewOpaque, fx.captureFb());
    bgfx::setViewRect(engine::rendering::kViewOpaque, 0, 0, fx.width(), fx.height());
    bgfx::setViewClear(engine::rendering::kViewOpaque, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       0x303030ff, 1.0f, 0);
    bgfx::setViewTransform(engine::rendering::kViewOpaque, &view[0][0], &proj[0][0]);

    float matData[8] = {0.6f, 0.6f, 0.6f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(uniforms.u_material, matData, 2);

    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.5f));
    float lightData[8] = {lightDir.x, lightDir.y, lightDir.z, 0.0f, 0.9f, 0.9f, 1.0f, 0.0f};
    bgfx::setUniform(uniforms.u_dirLight, lightData, 2);

    // Upright cube at origin
    {
        float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, mesh.positionVbh);
        bgfx::setVertexBuffer(1, mesh.surfaceVbh);
        bgfx::setIndexBuffer(mesh.ibh);
        bgfx::setState(BGFX_STATE_DEFAULT);
        bgfx::submit(engine::rendering::kViewOpaque, prog);
    }

    // Flat ground cube (scale wide and thin)
    {
        auto model = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.6f, 0.0f)),
                                glm::vec3(3.0f, 0.2f, 3.0f));
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
        engine::screenshot::compareOrUpdateGolden("ssao_scene", pixels, fx.width(), fx.height()));
}
