// Post-process screenshot test.
// Renders a bright cube directly into captureFb for deterministic output.
// Note: full PostProcessSystem chain integration (bloom, tonemap, FXAA via
// PostProcessResources::sceneFb + submit) is future work — requires
// hooking the fixture's readback texture into the last post-process pass output.

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ViewIds.h"

TEST_CASE("screenshot: postprocess scene", "[screenshot]")
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
    auto view = glm::lookAt(glm::vec3(0, 0.5f, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspective(glm::radians(60.0f), static_cast<float>(fx.width()) / fx.height(),
                                 0.1f, 100.0f);

    bgfx::setViewFrameBuffer(engine::rendering::kViewOpaque, fx.captureFb());
    bgfx::setViewRect(engine::rendering::kViewOpaque, 0, 0, fx.width(), fx.height());
    bgfx::setViewClear(engine::rendering::kViewOpaque, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       0x000000ff, 1.0f, 0);
    bgfx::setViewTransform(engine::rendering::kViewOpaque, &view[0][0], &proj[0][0]);

    // Bright white material (simulating emissive/bloom-worthy surface)
    float matData[8] = {1.0f, 1.0f, 1.0f, 0.1f,   // albedo + low roughness
                        0.0f, 1.0f, 0.0f, 0.0f};  // non-metallic, emissiveScale=1
    bgfx::setUniform(uniforms.u_material, matData, 2);

    // Strong directional light
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.0f, -1.0f, -1.0f));
    float lightData[8] = {lightDir.x, lightDir.y, lightDir.z, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f};
    bgfx::setUniform(uniforms.u_dirLight, lightData, 2);

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

    REQUIRE(engine::screenshot::compareOrUpdateGolden("postprocess_scene", pixels, fx.width(),
                                                      fx.height()));
}
