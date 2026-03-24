#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ViewIds.h"

TEST_CASE("screenshot: unlit flat-color cube", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle prog = engine::rendering::loadUnlitProgram();
    engine::rendering::RenderResources res;
    uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));
    const engine::rendering::Mesh& mesh = *res.getMesh(meshId);

    // Camera
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(60.0f), static_cast<float>(fx.width()) / fx.height(),
                                 0.1f, 100.0f);

    bgfx::setViewFrameBuffer(engine::rendering::kViewOpaque, fx.captureFb());
    bgfx::setViewRect(engine::rendering::kViewOpaque, 0, 0, fx.width(), fx.height());
    bgfx::setViewClear(engine::rendering::kViewOpaque, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       0x303030ff, 1.0f, 0);
    bgfx::setViewTransform(engine::rendering::kViewOpaque, &view[0][0], &proj[0][0]);

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
        engine::screenshot::compareOrUpdateGolden("unlit_cube", pixels, fx.width(), fx.height()));
}
