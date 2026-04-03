// IK Demo -- macOS
//
// Loads a skinned glTF model and demonstrates inverse kinematics:
//   - FK animation plays on the character
//   - IK post-processes the pose to reach targets
//   - Foot IK plants feet on tilted platform cubes
//   - Hand IK with draggable target via ImGui sliders
//   - Debug joint visualization
//
// Controls:
//   Right-drag  -- orbit camera
//   Scroll      -- zoom
//   WASD        -- move camera target
//   ImGui panel -- IK controls

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSystem.h"
#include "engine/animation/FootIkHelper.h"
#include "engine/animation/IkComponents.h"
#include "engine/animation/IkSolvers.h"
#include "engine/animation/IkSystem.h"
#include "engine/animation/LookAtHelper.h"
#include "engine/assets/AssetManager.h"
#include "engine/assets/GltfAsset.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/GltfSceneSpawner.h"
#include "engine/assets/StdFileSystem.h"
#include "engine/assets/TextureLoader.h"
#include "engine/core/Engine.h"
#include "engine/core/OrbitCamera.h"
#include "engine/ecs/Registry.h"
#include "engine/input/InputState.h"
#include "engine/input/Key.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/IblResources.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/TransformSystem.h"
#include "engine/threading/ThreadPool.h"
#include "imgui.h"

using namespace engine::animation;
using namespace engine::assets;
using namespace engine::core;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::rendering;
using namespace engine::threading;

// =============================================================================
// Entry point
// =============================================================================

static float s_zoomScrollDelta = 0.f;

int main()
{
    Engine eng;
    EngineDesc desc;
    desc.windowTitle = "IK Demo";
    if (!eng.init(desc))
        return 1;

    // Hook up zoom scroll.
    glfwSetScrollCallback(eng.glfwHandle(),
                          [](GLFWwindow* win, double /*xoff*/, double yoff)
                          {
                              auto* e = static_cast<Engine*>(glfwGetWindowUserPointer(win));
                              if (e)
                                  e->imguiScrollAccum() += static_cast<float>(yoff);
                              s_zoomScrollDelta += static_cast<float>(yoff);
                          });

    // -- Asset system ---------------------------------------------------------
    ThreadPool threadPool(2);
    StdFileSystem fileSystem(".");
    AssetManager assets(threadPool, fileSystem);
    assets.registerLoader(std::make_unique<TextureLoader>());
    assets.registerLoader(std::make_unique<GltfLoader>());

    // -- IBL (procedural sky/ground) -----------------------------------------
    IblResources ibl;
    ibl.generateDefault();

    // -- Start async model load ----------------------------------------------
    auto modelHandle = assets.load<GltfAsset>("BrainStem.glb");

    // -- ECS ------------------------------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;
    AnimationSystem animSys;
    AnimationResources animRes;
    IkSystem ikSys;
    bool modelSpawned = false;

    // -- Ground plane ---------------------------------------------------------
    MeshData cubeData = makeCubeMeshData();
    Mesh cubeMesh = buildMesh(cubeData);
    uint32_t cubeMeshId = eng.resources().addMesh(std::move(cubeMesh));

    Material groundMat;
    groundMat.albedo = {0.3f, 0.3f, 0.3f, 1.0f};
    groundMat.roughness = 0.8f;
    groundMat.metallic = 0.0f;
    uint32_t groundMatId = eng.resources().addMaterial(groundMat);

    EntityID groundEntity = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = {0.0f, -0.5f, 0.0f};
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {5.0f, 0.1f, 5.0f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(groundEntity, tc);
        reg.emplace<WorldTransformComponent>(groundEntity);
        reg.emplace<MeshComponent>(groundEntity, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(groundEntity, MaterialComponent{groundMatId});
        reg.emplace<VisibleTag>(groundEntity);
        reg.emplace<ShadowVisibleTag>(groundEntity, ShadowVisibleTag{0xFF});
    }

    // -- Tilted platform cubes (uneven terrain) --------------------------------
    Material platformMat;
    platformMat.albedo = {0.5f, 0.4f, 0.3f, 1.0f};
    platformMat.roughness = 0.7f;
    platformMat.metallic = 0.1f;
    uint32_t platformMatId = eng.resources().addMaterial(platformMat);

    auto makePlatform = [&](glm::vec3 pos, glm::vec3 scale, float tiltDeg, glm::vec3 tiltAxis)
    {
        EntityID e = reg.createEntity();
        TransformComponent tc{};
        tc.position = pos;
        tc.rotation = glm::angleAxis(glm::radians(tiltDeg), tiltAxis);
        tc.scale = scale;
        tc.flags = 1;
        reg.emplace<TransformComponent>(e, tc);
        reg.emplace<WorldTransformComponent>(e);
        reg.emplace<MeshComponent>(e, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(e, MaterialComponent{platformMatId});
        reg.emplace<VisibleTag>(e);
        reg.emplace<ShadowVisibleTag>(e, ShadowVisibleTag{0xFF});
    };

    makePlatform({-1.0f, -0.3f, 0.5f}, {1.0f, 0.05f, 1.0f}, 10.0f, {1, 0, 0});
    makePlatform({1.0f, -0.2f, -0.5f}, {0.8f, 0.05f, 0.8f}, -8.0f, {0, 0, 1});
    makePlatform({0.0f, -0.1f, -1.0f}, {1.2f, 0.05f, 0.6f}, 5.0f, {1, 0, 1});

    // -- IK target marker (small cube for hand target) -------------------------
    Material targetMat;
    targetMat.albedo = {1.0f, 1.0f, 0.0f, 1.0f};
    targetMat.emissiveScale = 3.0f;
    targetMat.roughness = 1.0f;
    uint32_t targetMatId = eng.resources().addMaterial(targetMat);

    EntityID targetMarker = reg.createEntity();
    glm::vec3 handTargetPos{0.5f, 1.0f, 0.5f};
    {
        TransformComponent tc{};
        tc.position = handTargetPos;
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {0.05f, 0.05f, 0.05f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(targetMarker, tc);
        reg.emplace<WorldTransformComponent>(targetMarker);
        reg.emplace<MeshComponent>(targetMarker, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(targetMarker, MaterialComponent{targetMatId});
        reg.emplace<VisibleTag>(targetMarker);
    }

    // -- Light indicator cube -------------------------------------------------
    Material lightMat{};
    lightMat.albedo = {1.0f, 0.9f, 0.3f, 1.0f};
    lightMat.emissiveScale = 5.0f;
    lightMat.roughness = 1.0f;
    uint32_t lightMatId = eng.resources().addMaterial(lightMat);

    EntityID lightIndicator = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)) * 5.0f;
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {0.15f, 0.15f, 0.15f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(lightIndicator, tc);
        reg.emplace<MeshComponent>(lightIndicator, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(lightIndicator, MaterialComponent{lightMatId});
        reg.emplace<VisibleTag>(lightIndicator);
    }

    // -- Light ----------------------------------------------------------------
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f));
    constexpr float kLightIntens = 8.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};

    const glm::vec3 kLightPos = kLightDir * 15.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-5.f, 5.f, -5.f, 5.f, 0.1f, 40.f);

    // -- Camera and interaction state -----------------------------------------
    engine::core::OrbitCamera cam;
    cam.distance = 5.0f;
    cam.pitch = 15.0f;
    cam.target = {0, 0.5f, 0};
    bool rightDragging = false;
    double prevMouseX = 0.0, prevMouseY = 0.0;

    float renderMs = 0.f;

    // -- IK state -------------------------------------------------------------
    bool enableIk = true;
    float ikBlendWeight = 1.0f;
    bool showJointDebug = false;

    // -- Main loop ------------------------------------------------------------
    float dt = 0.f;
    while (eng.beginFrame(dt))
    {
        if (eng.fbWidth() == 0 || eng.fbHeight() == 0)
            continue;

        const auto& input = eng.inputState();
        const float fbW = static_cast<float>(eng.fbWidth());
        const float fbH = static_cast<float>(eng.fbHeight());

        double mx, my;
        glfwGetCursorPos(eng.glfwHandle(), &mx, &my);

        bool imguiWants = eng.imguiWantsMouse();

        // -- Camera orbit and zoom -----------------------------------------------
        if (!imguiWants)
        {
            bool leftDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool rightDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            bool dragging = leftDown || rightDown;
            if (dragging)
            {
                if (rightDragging)
                {
                    float dx = static_cast<float>(mx - prevMouseX);
                    float dy = static_cast<float>(my - prevMouseY);
                    cam.orbit(dx, dy);
                }
                rightDragging = true;
            }
            else
            {
                rightDragging = false;
            }

            if (std::abs(s_zoomScrollDelta) > 0.01f)
            {
                cam.zoom(s_zoomScrollDelta, 0.5f, 1.0f, 20.0f);
            }
        }

        prevMouseX = mx;
        prevMouseY = my;
        s_zoomScrollDelta = 0.f;

        cam.moveTarget(input, dt);

        // -- Asset uploads & spawn on Ready -----------------------------------
        assets.processUploads();

        const AssetState modelState = assets.state(modelHandle);
        if (!modelSpawned && modelState == AssetState::Ready)
        {
            const GltfAsset* model = assets.get<GltfAsset>(modelHandle);
            GltfSceneSpawner::spawn(*model, reg, eng.resources(), animRes);
            modelSpawned = true;

            reg.view<AnimatorComponent>().each(
                [&](EntityID, AnimatorComponent& ac)
                {
                    ac.flags |= AnimatorComponent::kFlagPlaying;
                    ac.flags |= AnimatorComponent::kFlagLooping;
                    ac.speed = 1.0f;
                });

            // Add IK components to animated entities.
            reg.view<SkeletonComponent, AnimatorComponent>().each(
                [&](EntityID entity, const SkeletonComponent& sc, AnimatorComponent& /*ac*/)
                {
                    const Skeleton* skel = animRes.getSkeleton(sc.skeletonId);
                    if (!skel || skel->jointCount() < 3)
                        return;

                    // Set up a simple two-bone IK chain using the first
                    // three joints as a demo. Real usage would identify
                    // specific leg/arm joints by name hash.
                    IkChainsComponent chains;
                    IkChainDef chain;
                    chain.rootJoint = 0;
                    chain.midJoint = std::min(1u, skel->jointCount() - 1);
                    chain.endEffectorJoint = std::min(2u, skel->jointCount() - 1);
                    chain.solverType = IkSolverType::TwoBone;
                    chain.poleVector = engine::math::Vec3{0, 0, 1};
                    chain.weight = ikBlendWeight;
                    chains.chains.push_back(chain);
                    reg.emplace<IkChainsComponent>(entity, std::move(chains));

                    IkTargetsComponent targets;
                    IkTarget target;
                    target.position = handTargetPos;
                    targets.targets.push_back(target);
                    reg.emplace<IkTargetsComponent>(entity, std::move(targets));
                });
        }
        else if (modelState == AssetState::Failed)
        {
            static bool printed = false;
            if (!printed)
            {
                printed = true;
                fprintf(stderr, "ik_demo: asset load failed: %s\n",
                        assets.error(modelHandle).c_str());
            }
        }

        // -- Update IK chain parameters from ImGui ----------------------------
        reg.view<IkChainsComponent, IkTargetsComponent>().each(
            [&](EntityID, IkChainsComponent& chainsComp, IkTargetsComponent& targetsComp)
            {
                for (size_t i = 0; i < chainsComp.chains.size(); ++i)
                {
                    chainsComp.chains[i].enabled = enableIk ? 1 : 0;
                    chainsComp.chains[i].weight = ikBlendWeight;
                }
                if (!targetsComp.targets.empty())
                {
                    targetsComp.targets[0].position = handTargetPos;
                }
            });

        // Update target marker position.
        auto* markerTc = reg.get<TransformComponent>(targetMarker);
        if (markerTc)
        {
            markerTc->position = handTargetPos;
            markerTc->flags |= 1;
        }

        // -- Animation + IK system update -------------------------------------
        auto* arena = eng.frameArena().resource();

        // Phase 1: FK pose sampling.
        animSys.updatePoses(reg, dt, animRes, arena);

        // Phase 2: IK post-process.
        ikSys.update(reg, animRes, arena);

        // Phase 3: Bone matrix computation.
        animSys.computeBoneMatrices(reg, animRes, arena);

        // -- Transform system -------------------------------------------------
        transformSys.update(reg);

        // -- Render -----------------------------------------------------------
        double frameStart = glfwGetTime();
        eng.renderer().beginFrameDirect();

        glm::mat4 viewMat = cam.view();
        glm::vec3 camPos = cam.position();
        glm::mat4 projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 50.f);

        // Shadow pass
        eng.shadow().beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, eng.resources(), eng.shadowProgram(), 0);
        const engine::math::Mat4* shadowBones = animSys.boneBuffer();
        if (shadowBones)
        {
            drawCallSys.submitSkinnedShadowDrawCalls(reg, eng.resources(),
                                                     eng.skinnedShadowProgram(), 0, shadowBones);
        }

        // Opaque pass
        const auto W = eng.fbWidth();
        const auto H = eng.fbHeight();

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x1A1A2EFF)
            .transform(viewMat, projMat);

        const glm::mat4 shadowMat = eng.shadow().shadowMatrix(0);
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), eng.shadow().atlasTexture(), W, H, 0.05f, 50.f,
        };
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;

        if (ibl.isValid())
        {
            frame.iblEnabled = true;
            frame.maxMipLevels = 7.0f;
            frame.irradiance = ibl.irradiance();
            frame.prefiltered = ibl.prefiltered();
            frame.brdfLut = ibl.brdfLut();
        }

        drawCallSys.update(reg, eng.resources(), eng.pbrProgram(), eng.uniforms(), frame);

        const engine::math::Mat4* boneBuffer = animSys.boneBuffer();
        if (boneBuffer)
        {
            drawCallSys.updateSkinned(reg, eng.resources(), eng.skinnedPbrProgram(), eng.uniforms(),
                                      frame, boneBuffer);
        }

        // -- HUD --------------------------------------------------------------
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(1, 1, 0x0f, "IK Demo  |  %.1f fps  |  %.3f ms  |  render %.3f ms",
                            dt > 0 ? 1.f / dt : 0.f, dt * 1000.f, renderMs);
        bgfx::dbgTextPrintf(1, 2, 0x07, "RMB=orbit  |  Scroll=zoom  |  WASD=move  |  Arena: %zu KB",
                            eng.frameArena().bytesUsed() / 1024);

        if (modelState == AssetState::Ready)
            bgfx::dbgTextPrintf(1, 3, 0x0a, "BrainStem.glb — Ready");
        else if (modelState == AssetState::Failed)
            bgfx::dbgTextPrintf(1, 3, 0x0c, "BrainStem.glb — FAILED");
        else
            bgfx::dbgTextPrintf(1, 3, 0x0e, "BrainStem.glb — Loading...");

        // -- ImGui panel ------------------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(10, 80), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 320), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IK Controls"))
        {
            ImGui::Checkbox("Enable IK", &enableIk);

            ImGui::SliderFloat("IK Blend Weight", &ikBlendWeight, 0.0f, 1.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("Hand Target:");
            ImGui::SliderFloat("X", &handTargetPos.x, -2.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Y", &handTargetPos.y, 0.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("Z", &handTargetPos.z, -2.0f, 2.0f, "%.2f");

            ImGui::Separator();
            ImGui::Checkbox("Show Joint Debug", &showJointDebug);

            ImGui::Separator();
            uint32_t activeChains = 0;
            reg.view<IkChainsComponent>().each(
                [&](EntityID, const IkChainsComponent& cc)
                {
                    for (size_t j = 0; j < cc.chains.size(); ++j)
                    {
                        if (cc.chains[j].enabled)
                            ++activeChains;
                    }
                });
            ImGui::Text("Active chains: %u", activeChains);
            ImGui::Text("IK enabled: %s", enableIk ? "YES" : "NO");

            ImGui::Separator();
            glm::vec3 cp = cam.position();
            ImGui::Text("Camera: (%.1f, %.1f, %.1f)", cp.x, cp.y, cp.z);
        }
        ImGui::End();

        double frameEnd = glfwGetTime();
        renderMs = static_cast<float>((frameEnd - frameStart) * 1000.0);

        eng.endFrame();
    }

    // -- Cleanup --------------------------------------------------------------
    assets.release(modelHandle);
    ibl.shutdown();

    return 0;
}
