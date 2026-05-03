// Skinned animation screenshot test.
//
// Loads Fox.glb (24-joint skeleton, 3 animation clips: Survey, Walk, Run),
// spawns into the ECS via GltfSceneSpawner, sets the AnimatorComponent to a
// chosen clip + playbackTime, runs one updatePoses + computeBoneMatrices
// pass, and renders the skinned mesh via vs_pbr_skinned.  Two cases use
// different clips so the goldens diverge visibly: idle "Survey" at t=0.5
// and running "Run" at t=0.2.  Together they lock in the
// `u_model[BGFX_CONFIG_MAX_BONES]` shader define plus the rest of the FK
// sampling -> bone matrix -> setTransform pipeline.
//
// Run with --update-goldens on first run (or after intentional changes) to
// write the reference images at tests/golden/animation_fox_idle_t05.png
// and tests/golden/animation_fox_run_t02.png.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory_resource>
#include <thread>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSystem.h"
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

namespace
{

using namespace engine::animation;
using namespace engine::assets;
using namespace engine::ecs;
using namespace engine::rendering;
using namespace engine::threading;

// Pick a fixed clip + playbackTime, spawn the Fox, and render one frame.
// The golden name selects which reference PNG to compare against.
void renderFoxAtTime(uint32_t clipId, float playbackTime, const char* goldenName)
{
    engine::screenshot::ScreenshotFixture fx;
    ShaderUniforms uniforms;
    uniforms.init();

    ProgramHandle shadowProg = loadShadowProgram();
    ProgramHandle pbrProg = loadPbrProgram();
    ProgramHandle skinnedShadowProg = loadSkinnedShadowProgram();
    ProgramHandle skinnedPbrProg = loadSkinnedPbrProgram();

    // -----------------------------------------------------------------------
    // Synchronously load Fox.glb.
    // -----------------------------------------------------------------------
    ThreadPool pool(2);
    StdFileSystem fs(".");
    AssetManager assets(pool, fs);
    assets.registerLoader(std::make_unique<TextureLoader>());
    assets.registerLoader(std::make_unique<GltfLoader>());

    const std::string foxPath = ENGINE_SOURCE_DIR "/assets/Fox.glb";
    auto handle = assets.load<GltfAsset>(foxPath);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        assets.processUploads();
        bgfx::frame();
        const AssetState s = assets.state(handle);
        if (s == AssetState::Ready || s == AssetState::Failed)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(assets.state(handle) == AssetState::Ready);

    // -----------------------------------------------------------------------
    // Spawn skeleton + skinned mesh into ECS.
    // -----------------------------------------------------------------------
    RenderResources res;
    res.setWhiteTexture(engine::rendering::TextureHandle{fx.whiteTex().idx});
    res.setNeutralNormalTexture(engine::rendering::TextureHandle{fx.neutralNormalTex().idx});

    Registry reg;
    AnimationResources animRes;
    const GltfAsset* fox = assets.get<GltfAsset>(handle);
    REQUIRE(fox != nullptr);
    GltfSceneSpawner::spawn(*fox, reg, res, animRes);

    REQUIRE(animRes.clipCount() > clipId);

    // Drive the AnimatorComponent: chosen clip, fixed time, no auto-advance.
    // kFlagSampleOnce documents the intent (single deterministic sample).
    EntityID animatedEntity = INVALID_ENTITY;
    reg.view<AnimatorComponent>().each(
        [&](EntityID entity, AnimatorComponent& ac)
        {
            ac.clipId = clipId;
            ac.speed = 1.0f;
            ac.playbackTime = playbackTime;
            ac.flags = AnimatorComponent::kFlagSampleOnce;  // not playing → time stays put
            animatedEntity = entity;
        });
    REQUIRE(animatedEntity != INVALID_ENTITY);

    // Scale the fox down to fit the frame (model space ~70 units across).
    reg.view<TransformComponent>().each(
        [&](EntityID, TransformComponent& tc)
        {
            // Only touch the root entities (no parent → no HierarchyComponent).
            // We can recognise them as those with non-default scale targets;
            // here we rescale every transform once — the spawner sets all
            // root TRS positions to glTF node values and we want them shrunk.
            tc.scale *= 0.02f;
            tc.position *= 0.02f;
            tc.flags |= 1;
        });

    engine::scene::TransformSystem transformSys;
    transformSys.update(reg);

    // Per-frame arena for AnimationSystem (Pose + bone matrix buffer live here).
    // 64 KB easily covers one Fox: 1 Pose (~7 KB) + 24 Mat4s (~1.5 KB) + slack.
    std::array<std::byte, 64 * 1024> arenaStorage{};
    std::pmr::monotonic_buffer_resource arena(arenaStorage.data(), arenaStorage.size());

    AnimationSystem animSys;
    animSys.updatePoses(reg, /*dt=*/0.0f, animRes, &arena);
    animSys.computeBoneMatrices(reg, animRes, &arena);

    const engine::math::Mat4* bones = animSys.boneBuffer();
    REQUIRE(bones != nullptr);

    // -----------------------------------------------------------------------
    // Shadow pass.
    // -----------------------------------------------------------------------
    ShadowRenderer shadow;
    ShadowDesc shadowDesc;
    shadowDesc.resolution = 512;
    shadowDesc.cascadeCount = 1;
    shadow.init(shadowDesc);

    const glm::vec3 kLightDir = glm::normalize(glm::vec3(0.5f, 0.8f, -1.0f));
    const glm::vec3 kLightPos = kLightDir * 10.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-1.5f, 1.5f, -1.5f, 1.5f, 0.1f, 30.f);

    shadow.beginCascade(0, lightView, lightProj);

    DrawCallBuildSystem drawCallSys;
    drawCallSys.submitShadowDrawCalls(reg, res, bgfx::ProgramHandle{shadowProg.idx}, 0);
    drawCallSys.submitSkinnedShadowDrawCalls(reg, res, bgfx::ProgramHandle{skinnedShadowProg.idx},
                                             0, bones);

    // -----------------------------------------------------------------------
    // Opaque PBR pass → off-screen capture FB.
    // -----------------------------------------------------------------------
    auto view = glm::lookAt(glm::vec3(0.0f, 0.6f, 3.5f), glm::vec3(0.f, 0.4f, 0.f),
                            glm::vec3(0.f, 1.f, 0.f));
    auto proj = glm::perspective(glm::radians(45.f),
                                 static_cast<float>(fx.width()) / static_cast<float>(fx.height()),
                                 0.05f, 50.f);

    RenderPass(kViewOpaque)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x1A1A2EFF)
        .transform(view, proj);

    constexpr float kLightIntens = 8.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};

    const glm::mat4 shadowMat = shadow.shadowMatrix(0);
    const glm::vec3 camPos(0.0f, 0.6f, 3.5f);
    PbrFrameParams frame{
        lightData, glm::value_ptr(shadowMat), shadow.atlasTexture(), fx.width(), fx.height(), 0.05f,
        50.f};
    frame.camPos[0] = camPos.x;
    frame.camPos[1] = camPos.y;
    frame.camPos[2] = camPos.z;

    drawCallSys.update(reg, res, bgfx::ProgramHandle{pbrProg.idx}, uniforms, frame);
    drawCallSys.updateSkinned(reg, res, bgfx::ProgramHandle{skinnedPbrProg.idx}, uniforms, frame,
                              bones);

    auto pixels = fx.captureFrame();

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------
    assets.release(handle);
    shadow.shutdown();
    if (isValid(shadowProg))
        bgfx::destroy(bgfx::ProgramHandle{shadowProg.idx});
    if (isValid(pbrProg))
        bgfx::destroy(bgfx::ProgramHandle{pbrProg.idx});
    if (isValid(skinnedShadowProg))
        bgfx::destroy(bgfx::ProgramHandle{skinnedShadowProg.idx});
    if (isValid(skinnedPbrProg))
        bgfx::destroy(bgfx::ProgramHandle{skinnedPbrProg.idx});
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(engine::screenshot::compareOrUpdateGolden(goldenName, pixels, fx.width(), fx.height()));
}

}  // namespace

TEST_CASE("screenshot: fox skinned idle clip t=0.5", "[screenshot]")
{
    renderFoxAtTime(/*clip=Survey*/ 0, 0.5f, "animation_fox_idle_t05");
}

TEST_CASE("screenshot: fox skinned run clip t=0.2", "[screenshot]")
{
    // Fox.glb clips: 0=Survey (idle), 1=Walk, 2=Run.  Use Run mid-stride
    // for a clearly different pose from the idle case.
    renderFoxAtTime(/*clip=Run*/ 2, 0.2f, "animation_fox_run_t02");
}
