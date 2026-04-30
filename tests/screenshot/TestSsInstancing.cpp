// Instancing grid screenshot test.
// Draws 9 individual cubes in a 3x3 grid via 9 separate bgfx::setTransform + bgfx::submit
// calls (not GPU instancing). This validates visual output consistency.
// Note: GPU instancing visual validation can be added when InstanceBufferBuildSystem
// is wired into the screenshot fixture.

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

TEST_CASE("screenshot: instancing 3x3 grid", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle prog = engine::rendering::loadUnlitProgram();
    engine::rendering::RenderResources res;
    uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));
    const engine::rendering::Mesh& mesh = *res.getMesh(meshId);

    // Camera looking down at 3x3 grid
    auto view = glm::lookAt(glm::vec3(0, 5, 7), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(60.0f), static_cast<float>(fx.width()) / fx.height(),
                                 0.1f, 100.0f);

    engine::rendering::RenderPass(engine::rendering::kViewOpaque)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x303030ff)
        .transform(view, proj);

    // 3x3 grid: x in {-2, 0, 2}, z in {-2, 0, 2}, y = 0
    const float positions[] = {-2.0f, 0.0f, 2.0f};
    for (float x : positions)
    {
        for (float z : positions)
        {
            auto model = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, z));
            float mtx[16] = {};
            memcpy(mtx, &model[0][0], sizeof(float) * 16);
            bgfx::setTransform(mtx);
            bgfx::setVertexBuffer(0, mesh.positionVbh);
            bgfx::setVertexBuffer(1, mesh.surfaceVbh);
            bgfx::setIndexBuffer(mesh.ibh);
            bgfx::setState(BGFX_STATE_DEFAULT);
            bgfx::submit(engine::rendering::kViewOpaque, prog);
        }
    }

    auto pixels = fx.captureFrame();

    if (bgfx::isValid(prog))
        bgfx::destroy(prog);
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(engine::screenshot::compareOrUpdateGolden("instancing_grid", pixels, fx.width(),
                                                      fx.height()));
}
