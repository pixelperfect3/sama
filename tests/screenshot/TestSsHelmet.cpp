// Helmet screenshot test.
//
// Loads DamagedHelmet.glb via AssetManager (with a real bgfx context from
// BgfxContext), waits synchronously for the asset to reach Ready or Failed,
// spawns ECS entities, and renders one PBR + shadow frame.
//
// Run with --update-goldens on first run to write the reference image.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <thread>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/assets/AssetManager.h"
#include "engine/assets/AssetState.h"
#include "engine/assets/GltfAsset.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/GltfSceneSpawner.h"
#include "engine/assets/StdFileSystem.h"
#include "engine/assets/TextureLoader.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ShadowRenderer.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/TransformSystem.h"
#include "engine/threading/ThreadPool.h"

using namespace engine::assets;
using namespace engine::ecs;
using namespace engine::rendering;
using namespace engine::threading;

TEST_CASE("screenshot: damaged helmet PBR", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle shadowProg = bgfx::ProgramHandle{loadShadowProgram().idx};
    bgfx::ProgramHandle pbrProg = bgfx::ProgramHandle{loadPbrProgram().idx};

    // -----------------------------------------------------------------------
    // Load the helmet GLB synchronously.
    // ENGINE_SOURCE_DIR is defined by CMake so the test finds the file
    // regardless of where it is run from.
    // -----------------------------------------------------------------------

    ThreadPool pool(2);
    StdFileSystem fs(".");
    AssetManager assets(pool, fs);
    assets.registerLoader(std::make_unique<TextureLoader>());
    assets.registerLoader(std::make_unique<GltfLoader>());

    const std::string helmetPath = ENGINE_SOURCE_DIR "/assets/DamagedHelmet.glb";
    auto handle = assets.load<GltfAsset>(helmetPath);

    // Drain until Ready or Failed (max 10 seconds — GLB is ~7 MB on disk).
    // bgfx::frame() is required after processUploads() so that texture and
    // vertex buffer creation commands are committed to the GPU before use.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        assets.processUploads();
        bgfx::frame();  // commit deferred resource-creation commands
        const AssetState s = assets.state(handle);
        if (s == AssetState::Ready || s == AssetState::Failed)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(assets.state(handle) == AssetState::Ready);

    // -----------------------------------------------------------------------
    // Spawn into ECS
    // -----------------------------------------------------------------------

    RenderResources res;
    res.setWhiteTexture(fx.whiteTex());
    res.setNeutralNormalTexture(fx.neutralNormalTex());

    Registry reg;
    const GltfAsset* helmet = assets.get<GltfAsset>(handle);
    REQUIRE(helmet != nullptr);
    GltfSceneSpawner::spawn(*helmet, reg, res);

    // Compute world matrices from the scene hierarchy.
    engine::scene::TransformSystem transformSys;
    transformSys.update(reg);

    // -----------------------------------------------------------------------
    // Shadow pass (view 0)
    // -----------------------------------------------------------------------

    ShadowRenderer shadow;
    ShadowDesc shadowDesc;
    shadowDesc.resolution = 512;
    shadowDesc.cascadeCount = 1;
    shadow.init(shadowDesc);

    // Light from upper-right, shining toward -Z to match the DamagedHelmet
    // visor orientation (front-face normals point in -Z).
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(0.5f, 0.8f, -1.0f));
    const glm::vec3 kLightPos = kLightDir * 10.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-2.f, 2.f, -2.f, 2.f, 0.1f, 30.f);

    shadow.beginCascade(0, lightView, lightProj);

    DrawCallBuildSystem drawCallSys;
    drawCallSys.submitShadowDrawCalls(reg, res, shadowProg, 0);

    // -----------------------------------------------------------------------
    // Opaque pass (view 9) → off-screen capture framebuffer
    // -----------------------------------------------------------------------

    // Camera offset to the right so the golden metallic panels are visible.
    auto view = glm::lookAt(glm::vec3(1.5f, 0.5f, 2.8f), glm::vec3(0.f, 0.f, 0.f),
                            glm::vec3(0.f, 1.f, 0.f));
    auto proj = glm::perspective(glm::radians(45.f),
                                 static_cast<float>(fx.width()) / static_cast<float>(fx.height()),
                                 0.05f, 50.f);

    RenderPass(kViewOpaque)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x1A1A2EFF)
        .transform(view, proj);

    constexpr float kLightIntens = 12.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};

    const glm::mat4 shadowMat = shadow.shadowMatrix(0);
    const glm::vec3 camPos(1.5f, 0.5f, 2.8f);
    PbrFrameParams frame{
        lightData, glm::value_ptr(shadowMat), shadow.atlasTexture(), fx.width(), fx.height(), 0.05f,
        50.f};
    frame.camPos[0] = camPos.x;
    frame.camPos[1] = camPos.y;
    frame.camPos[2] = camPos.z;

    drawCallSys.update(reg, res, pbrProg, uniforms, frame);

    // -----------------------------------------------------------------------
    // Capture, compare, cleanup
    // -----------------------------------------------------------------------

    auto pixels = fx.captureFrame();

    assets.release(handle);
    shadow.shutdown();
    if (bgfx::isValid(shadowProg))
        bgfx::destroy(shadowProg);
    if (bgfx::isValid(pbrProg))
        bgfx::destroy(pbrProg);
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(engine::screenshot::compareOrUpdateGolden("damaged_helmet", pixels, fx.width(),
                                                      fx.height()));
}
