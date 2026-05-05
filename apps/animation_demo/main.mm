// Animation Demo -- macOS
//
// Comprehensive showcase of the Sama engine animation subsystem:
//   1. Multiple animated entities at different positions/speeds
//   2. Crossfade blending between clips
//   3. Speed and playback control (pause, reverse, scrub)
//   4. IK integration (two-bone IK with draggable target)
//   5. Animation events (TODO: pending AnimationEvent system)
//   6. State machine (TODO: pending AnimStateMachine system)
//   7. Full HUD overlay with controls help
//
// Keyboard Controls:
//   Space       -- play/pause
//   1-9         -- select clip (if multiple available)
//   Up/Down     -- adjust speed (+/- 0.1)
//   B           -- trigger crossfade to next clip
//   I           -- toggle IK on/off
//   R           -- reset to default state
//   H           -- toggle HUD
//   Right-drag  -- orbit camera
//   Scroll      -- zoom
//   WASD        -- move camera target

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/animation/AnimStateMachine.h"
#include "engine/animation/AnimStateMachineSystem.h"
#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationEventQueue.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSystem.h"
#include "engine/animation/Hash.h"
#include "engine/animation/IkComponents.h"
#include "engine/animation/IkSystem.h"
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
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/TransformSystem.h"
#include "engine/threading/ThreadPool.h"
#include "engine/ui/DebugHud.h"
#include "imgui.h"

using namespace engine::animation;
using namespace engine::assets;
using namespace engine::core;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::rendering;
using namespace engine::threading;

// =============================================================================
// Constants and helpers
// =============================================================================

static constexpr int kNumInstances = 3;
static constexpr float kInstanceSpacing = 4.0f;
static constexpr float kSpeedStep = 0.1f;
static constexpr float kDefaultBlendDuration = 0.5f;

static float s_zoomScrollDelta = 0.f;

// Per-instance tracking data.
struct InstanceInfo
{
    std::vector<EntityID> entities;            // all entities spawned for this instance
    EntityID animatedEntity = INVALID_ENTITY;  // the entity with AnimatorComponent
    glm::vec3 rootOffset{0.0f};                // world-space offset
};

// =============================================================================
// Entry point
// =============================================================================

int main()
{
    Engine eng;
    EngineDesc desc;
    desc.windowTitle = "Animation Demo";
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
    auto modelHandle = assets.load<GltfAsset>("Fox.glb");

    // -- ECS ------------------------------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;
    AnimationSystem animSys;
    AnimationResources animRes;
    AnimStateMachineSystem stateMachineSys;
    IkSystem ikSys;
    bool modelSpawned = false;

    // -- Instance tracking ----------------------------------------------------
    InstanceInfo instances[kNumInstances];
    for (int i = 0; i < kNumInstances; ++i)
    {
        float x = (static_cast<float>(i) - static_cast<float>(kNumInstances - 1) * 0.5f) *
                  kInstanceSpacing;
        instances[i].rootOffset = glm::vec3(x, 0.0f, 0.0f);
    }

    // -- Demo state -----------------------------------------------------------
    int selectedInstance = 0;
    bool showHud = true;
    bool enableIk = false;
    float ikBlendWeight = 1.0f;
    glm::vec3 ikTargetPos{0.5f, 1.0f, 0.5f};
    uint32_t totalClipCount = 0;

    // State machine: Slow → Normal → Fast based on "speed" parameter.
    AnimStateMachine stateMachine;
    bool stateMachineSetup = false;
    float smSpeedParam = 1.0f;  // controlled by Left/Right arrows

    // Animation events: recent fired events for HUD display.
    struct FiredEvent
    {
        std::string name;
        float age = 0.0f;  // seconds since it fired
    };
    std::vector<FiredEvent> recentEvents;

    // -- Ground plane ---------------------------------------------------------
    MeshData cubeData = makeCubeMeshData();
    Mesh cubeMesh = buildMesh(cubeData);
    uint32_t cubeMeshId = eng.resources().addMesh(std::move(cubeMesh));

    Material groundMat;
    groundMat.albedo = {0.2f, 0.2f, 0.2f, 1.0f};
    groundMat.roughness = 0.8f;
    groundMat.metallic = 0.0f;
    uint32_t groundMatId = eng.resources().addMaterial(groundMat);

    EntityID groundEntity = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = {0.0f, -0.5f, 0.0f};
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {10.0f, 0.1f, 5.0f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(groundEntity, tc);
        reg.emplace<WorldTransformComponent>(groundEntity);
        reg.emplace<MeshComponent>(groundEntity, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(groundEntity, MaterialComponent{groundMatId});
        reg.emplace<VisibleTag>(groundEntity);
        reg.emplace<ShadowVisibleTag>(groundEntity, ShadowVisibleTag{0xFF});
    }

    // -- IK target marker (small yellow cube) ---------------------------------
    Material targetMat;
    targetMat.albedo = {1.0f, 1.0f, 0.0f, 1.0f};
    targetMat.emissiveScale = 3.0f;
    targetMat.roughness = 1.0f;
    uint32_t targetMatId = eng.resources().addMaterial(targetMat);

    EntityID targetMarker = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = ikTargetPos;
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {0.05f, 0.05f, 0.05f};
        tc.flags = 1;
        reg.emplace<TransformComponent>(targetMarker, tc);
        reg.emplace<WorldTransformComponent>(targetMarker);
        reg.emplace<MeshComponent>(targetMarker, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(targetMarker, MaterialComponent{targetMatId});
        // Start hidden (IK off by default).
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
    const glm::mat4 lightProj = glm::ortho(-10.f, 10.f, -5.f, 5.f, 0.1f, 40.f);

    // -- Camera and interaction state -----------------------------------------
    engine::core::OrbitCamera cam;
    cam.distance = 5.0f;
    cam.pitch = 20.0f;
    cam.target = {0, 0.5f, 0};
    bool rightDragging = false;
    double prevMouseX = 0.0, prevMouseY = 0.0;

    float renderMs = 0.f;

    engine::ui::DebugHud hud;
    hud.init();

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

        // -- Camera orbit (left or right drag) and zoom (scroll) ----------------
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
                cam.zoom(s_zoomScrollDelta, 0.5f, 1.0f, 30.0f);
            }
        }

        prevMouseX = mx;
        prevMouseY = my;
        s_zoomScrollDelta = 0.f;

        // WASD moves the camera target along the XZ plane.
        cam.moveTarget(input, dt);

        // -- Keyboard controls (only when ImGui does not want keyboard) ----------
        bool imguiWantsKb = ImGui::GetIO().WantCaptureKeyboard;
        if (!imguiWantsKb && modelSpawned)
        {
            // Space: play/pause all instances
            if (input.isKeyPressed(Key::Space))
            {
                reg.view<AnimatorComponent>().each(
                    [](EntityID, AnimatorComponent& ac)
                    { ac.flags ^= AnimatorComponent::kFlagPlaying; });
            }

            // Up/Down: adjust speed on the selected instance
            if (input.isKeyPressed(Key::Up))
            {
                EntityID sel = instances[selectedInstance].animatedEntity;
                if (sel != INVALID_ENTITY)
                {
                    auto* ac = reg.get<AnimatorComponent>(sel);
                    if (ac)
                        ac->speed = std::min(ac->speed + kSpeedStep, 5.0f);
                }
            }
            if (input.isKeyPressed(Key::Down))
            {
                EntityID sel = instances[selectedInstance].animatedEntity;
                if (sel != INVALID_ENTITY)
                {
                    auto* ac = reg.get<AnimatorComponent>(sel);
                    if (ac)
                        ac->speed = std::max(ac->speed - kSpeedStep, -2.0f);
                }
            }

            // B: trigger crossfade to next clip on selected instance
            if (input.isKeyPressed(Key::B) && totalClipCount > 1)
            {
                EntityID sel = instances[selectedInstance].animatedEntity;
                if (sel != INVALID_ENTITY)
                {
                    auto* ac = reg.get<AnimatorComponent>(sel);
                    if (ac && !(ac->flags & AnimatorComponent::kFlagBlending))
                    {
                        uint32_t nextClip = (ac->clipId + 1) % totalClipCount;
                        ac->nextClipId = nextClip;
                        ac->blendFactor = 0.0f;
                        ac->blendDuration = kDefaultBlendDuration;
                        ac->blendElapsed = 0.0f;
                        ac->flags |= AnimatorComponent::kFlagBlending;
                    }
                }
            }

            // I: toggle IK
            if (input.isKeyPressed(Key::I))
            {
                enableIk = !enableIk;
                // Show/hide IK target marker.
                if (enableIk)
                    reg.emplace<VisibleTag>(targetMarker);
                else
                    reg.remove<VisibleTag>(targetMarker);
            }

            // R: reset to default state
            if (input.isKeyPressed(Key::R))
            {
                reg.view<AnimatorComponent>().each(
                    [](EntityID, AnimatorComponent& ac)
                    {
                        ac.playbackTime = 0.0f;
                        ac.speed = 1.0f;
                        ac.flags =
                            AnimatorComponent::kFlagPlaying | AnimatorComponent::kFlagLooping;
                        ac.blendFactor = 0.0f;
                        ac.blendDuration = 0.0f;
                        ac.blendElapsed = 0.0f;
                        ac.nextClipId = UINT32_MAX;
                    });
                enableIk = false;
                ikBlendWeight = 1.0f;
                reg.remove<VisibleTag>(targetMarker);
            }

            // H: toggle HUD
            if (input.isKeyPressed(Key::H))
            {
                showHud = !showHud;
            }

            // Left/Right: adjust state machine speed parameter
            if (input.isKeyPressed(Key::Right))
                smSpeedParam = std::min(smSpeedParam + 0.25f, 3.0f);
            if (input.isKeyPressed(Key::Left))
                smSpeedParam = std::max(smSpeedParam - 0.25f, 0.0f);

            // 1-9: select clip on selected instance (if multiple clips exist)
            for (int k = 0; k < 9; ++k)
            {
                Key numKey = static_cast<Key>(static_cast<uint16_t>(Key::Num1) + k);
                if (input.isKeyPressed(numKey))
                {
                    uint32_t clipIdx = static_cast<uint32_t>(k);
                    if (clipIdx < totalClipCount)
                    {
                        EntityID sel = instances[selectedInstance].animatedEntity;
                        if (sel != INVALID_ENTITY)
                        {
                            auto* ac = reg.get<AnimatorComponent>(sel);
                            if (ac)
                            {
                                ac->clipId = clipIdx;
                                ac->playbackTime = 0.0f;
                            }
                        }
                    }
                }
            }
        }

        // -- Asset uploads & spawn on Ready -----------------------------------
        assets.processUploads();

        const AssetState modelState = assets.state(modelHandle);
        if (!modelSpawned && modelState == AssetState::Ready)
        {
            const GltfAsset* model = assets.get<GltfAsset>(modelHandle);

            for (int i = 0; i < kNumInstances; ++i)
            {
                std::vector<EntityID> entitiesBefore;
                reg.forEachEntity([&](EntityID e) { entitiesBefore.push_back(e); });

                GltfSceneSpawner::spawn(*model, reg, eng.resources(), animRes);

                // Find newly spawned entities.
                reg.forEachEntity(
                    [&](EntityID e)
                    {
                        if (std::find(entitiesBefore.begin(), entitiesBefore.end(), e) ==
                            entitiesBefore.end())
                        {
                            instances[i].entities.push_back(e);
                        }
                    });

                // Offset only root entities (no parent) to avoid double-offsetting
                // children whose positions are relative to their parent.
                for (EntityID e : instances[i].entities)
                {
                    auto* hc = reg.get<engine::scene::HierarchyComponent>(e);
                    auto* tc = reg.get<TransformComponent>(e);
                    if (tc && !hc)
                    {
                        tc->position.x += instances[i].rootOffset.x;
                        tc->position.y += instances[i].rootOffset.y;
                        tc->position.z += instances[i].rootOffset.z;
                        tc->scale = glm::vec3(0.02f);
                        tc->flags |= 1;
                    }

                    auto* ac = reg.get<AnimatorComponent>(e);
                    if (ac)
                    {
                        instances[i].animatedEntity = e;
                        ac->flags |=
                            AnimatorComponent::kFlagPlaying | AnimatorComponent::kFlagLooping;
                        // Vary speed per instance for visual variety.
                        ac->speed = 0.5f + static_cast<float>(i) * 0.5f;
                        // Stagger playback times so they are not in sync.
                        ac->playbackTime = static_cast<float>(i) * 0.3f;
                    }
                }
            }

            totalClipCount = animRes.clipCount();

            // -- Set up animation events on clips --------------------------------
            // Add programmatic event markers for demonstration.
            for (uint32_t ci = 0; ci < totalClipCount; ++ci)
            {
                AnimationClip* clip = animRes.getClipMut(ci);
                if (clip && clip->duration > 0.0f)
                {
                    clip->addEvent(0.0f, "anim_start");
                    clip->addEvent(clip->duration * 0.25f, "quarter");
                    clip->addEvent(clip->duration * 0.5f, "halfway");
                    clip->addEvent(clip->duration * 0.75f, "three_quarter");
                }
            }

            // Set up global event callback for logging.
            animSys.setEventCallback([&recentEvents](EntityID /*entity*/, const AnimationEvent& evt)
                                     { recentEvents.push_back({evt.name, 0.0f}); });

            // -- Set up state machine --------------------------------------------
            // Three states mapped to Fox's clips: Survey(0), Walk(1), Run(2).
            if (totalClipCount >= 3)
            {
                uint32_t survey = stateMachine.addState("Survey", 0, true, 1.0f);
                uint32_t walk = stateMachine.addState("Walk", 1, true, 1.0f);
                uint32_t run = stateMachine.addState("Run", 2, true, 1.0f);

                // Survey → Walk when speed > 0.75
                stateMachine.addTransition(survey, walk, 0.3f, "speed",
                                           TransitionCondition::Compare::Greater, 0.75f);
                // Walk → Survey when speed < 0.5
                stateMachine.addTransition(walk, survey, 0.3f, "speed",
                                           TransitionCondition::Compare::Less, 0.5f);
                // Walk → Run when speed > 1.75
                stateMachine.addTransition(walk, run, 0.3f, "speed",
                                           TransitionCondition::Compare::Greater, 1.75f);
                // Run → Walk when speed < 1.5
                stateMachine.addTransition(run, walk, 0.3f, "speed",
                                           TransitionCondition::Compare::Less, 1.5f);

                // Attach state machine to the third instance (index 2).
                EntityID smEntity = instances[2].animatedEntity;
                if (smEntity != INVALID_ENTITY)
                {
                    AnimStateMachineComponent smComp;
                    smComp.machine = &stateMachine;
                    smComp.currentState = walk;
                    smComp.setFloat("speed", smSpeedParam);
                    reg.emplace<AnimStateMachineComponent>(smEntity, std::move(smComp));
                    stateMachineSetup = true;
                }
            }

            modelSpawned = true;
        }
        else if (modelState == AssetState::Failed)
        {
            static bool printed = false;
            if (!printed)
            {
                printed = true;
                fprintf(stdout, "animation_demo: asset load failed: %s\n",
                        assets.error(modelHandle).c_str());
            }
        }

        // -- Update IK components on the selected instance --------------------
        if (modelSpawned)
        {
            EntityID sel = instances[selectedInstance].animatedEntity;
            if (sel != INVALID_ENTITY)
            {
                // Ensure IK components exist on the selected entity.
                if (enableIk && !reg.has<IkChainsComponent>(sel))
                {
                    auto* sc = reg.get<SkeletonComponent>(sel);
                    if (sc)
                    {
                        const Skeleton* skel = animRes.getSkeleton(sc->skeletonId);
                        if (skel && skel->jointCount() >= 3)
                        {
                            IkChainsComponent chains;
                            IkChainDef chain;
                            chain.rootJoint = 0;
                            chain.midJoint = std::min(1u, skel->jointCount() - 1);
                            chain.endEffectorJoint = std::min(2u, skel->jointCount() - 1);
                            chain.solverType = IkSolverType::TwoBone;
                            chain.poleVector = engine::math::Vec3{0, 0, 1};
                            chain.weight = ikBlendWeight;
                            chains.chains.push_back(chain);
                            reg.emplace<IkChainsComponent>(sel, std::move(chains));

                            IkTargetsComponent targets;
                            IkTarget target;
                            target.position = ikTargetPos;
                            targets.targets.push_back(target);
                            reg.emplace<IkTargetsComponent>(sel, std::move(targets));
                        }
                    }
                }
                else if (!enableIk && reg.has<IkChainsComponent>(sel))
                {
                    reg.remove<IkChainsComponent>(sel);
                    reg.remove<IkTargetsComponent>(sel);
                }

                // Update IK targets and weights each frame.
                if (enableIk && reg.has<IkChainsComponent>(sel))
                {
                    auto* chainsComp = reg.get<IkChainsComponent>(sel);
                    auto* targetsComp = reg.get<IkTargetsComponent>(sel);
                    if (chainsComp && targetsComp)
                    {
                        for (size_t ci = 0; ci < chainsComp->chains.size(); ++ci)
                        {
                            chainsComp->chains[ci].weight = ikBlendWeight;
                            chainsComp->chains[ci].enabled = 1;
                        }
                        if (!targetsComp->targets.empty())
                        {
                            targetsComp->targets[0].position = ikTargetPos;
                        }
                    }
                }
            }

            // Update IK target marker position.
            auto* markerTc = reg.get<TransformComponent>(targetMarker);
            if (markerTc)
            {
                markerTc->position = ikTargetPos;
                markerTc->flags |= 1;
            }
        }

        // -- Update state machine parameter + age recent events -----------------
        if (stateMachineSetup)
        {
            EntityID smEntity = instances[2].animatedEntity;
            if (smEntity != INVALID_ENTITY)
            {
                auto* smComp = reg.get<AnimStateMachineComponent>(smEntity);
                if (smComp)
                    smComp->setFloat("speed", smSpeedParam);
            }
            stateMachineSys.update(reg, dt, animRes);
        }

        // Age and prune recent events.
        for (auto& ev : recentEvents)
            ev.age += dt;
        recentEvents.erase(std::remove_if(recentEvents.begin(), recentEvents.end(),
                                          [](const FiredEvent& e) { return e.age > 3.0f; }),
                           recentEvents.end());

        // Clear per-entity event queues from last frame.
        reg.view<AnimationEventQueue>().each([](EntityID, AnimationEventQueue& q) { q.clear(); });

        // -- Transform system (before animation so WorldTransformComponent is
        //    available for bone matrix computation) ----------------------------
        transformSys.update(reg);

        // -- Animation + IK system update -------------------------------------
        auto* arena = eng.frameArena().resource();

        // Phase 1: FK pose sampling.
        animSys.updatePoses(reg, dt, animRes, arena);

        // Phase 2: IK post-process (only if enabled).
        if (enableIk)
            ikSys.update(reg, animRes, arena);

        // Phase 3: Bone matrix computation.
        animSys.computeBoneMatrices(reg, animRes, arena);

        // -- Render -----------------------------------------------------------
        double frameStart = glfwGetTime();
        eng.renderer().beginFrame();

        glm::mat4 viewMat = cam.view();
        glm::vec3 camPos = cam.position();
        glm::mat4 projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 50.f);

        // Shadow pass
        eng.shadow().beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, eng.resources(),
                                          bgfx::ProgramHandle{eng.shadowProgram().idx}, 0);
        const engine::math::Mat4* shadowBones = animSys.boneBuffer();
        if (shadowBones)
        {
            drawCallSys.submitSkinnedShadowDrawCalls(
                reg, eng.resources(), bgfx::ProgramHandle{eng.skinnedShadowProgram().idx}, 0,
                shadowBones);
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

        drawCallSys.update(reg, eng.resources(), bgfx::ProgramHandle{eng.pbrProgram().idx},
                           eng.uniforms(), frame);

        const engine::math::Mat4* boneBuffer = animSys.boneBuffer();
        if (boneBuffer)
        {
            drawCallSys.updateSkinned(reg, eng.resources(),
                                      bgfx::ProgramHandle{eng.skinnedPbrProgram().idx},
                                      eng.uniforms(), frame, boneBuffer);
        }

        // -- Gather animation state for HUD -----------------------------------
        float currentTime = 0.0f;
        float clipDuration = 0.0f;
        float animSpeed = 1.0f;
        bool isPlaying = false;
        uint32_t jointCount = 0;
        const char* clipName = "N/A";
        bool isBlending = false;
        float blendProgress = 0.0f;
        const char* nextClipName = "N/A";

        EntityID selEntity = instances[selectedInstance].animatedEntity;
        if (selEntity != INVALID_ENTITY)
        {
            auto* ac = reg.get<AnimatorComponent>(selEntity);
            if (ac)
            {
                currentTime = ac->playbackTime;
                animSpeed = ac->speed;
                isPlaying = (ac->flags & AnimatorComponent::kFlagPlaying) != 0;
                isBlending = (ac->flags & AnimatorComponent::kFlagBlending) != 0;
                if (isBlending && ac->blendDuration > 0.0f)
                    blendProgress = ac->blendElapsed / ac->blendDuration;

                const AnimationClip* clip = animRes.getClip(ac->clipId);
                if (clip)
                {
                    clipDuration = clip->duration;
                    if (!clip->name.empty())
                        clipName = clip->name.c_str();
                }

                if (isBlending)
                {
                    const AnimationClip* nclip = animRes.getClip(ac->nextClipId);
                    if (nclip && !nclip->name.empty())
                        nextClipName = nclip->name.c_str();
                }
            }

            auto* sc = reg.get<SkeletonComponent>(selEntity);
            if (sc)
            {
                const Skeleton* skel = animRes.getSkeleton(sc->skeletonId);
                if (skel)
                    jointCount = skel->jointCount();
            }
        }

        // -- HUD (debug text overlay) -----------------------------------------
        hud.begin(eng.fbWidth(), eng.fbHeight());
        if (showHud)
        {
            int row = 1;
            hud.printf(1, row++, "Animation Demo  |  %.1f fps  |  %.3f ms  |  render %.3f ms",
                       dt > 0 ? 1.f / dt : 0.f, dt * 1000.f, renderMs);
            hud.printf(1, row++, "Arena: %zu KB / %zu KB", eng.frameArena().bytesUsed() / 1024,
                       eng.frameArena().capacity() / 1024);

            if (modelState == AssetState::Ready)
                hud.printf(1, row++, "Fox.glb -- Ready");
            else if (modelState == AssetState::Failed)
                hud.printf(1, row++, "Fox.glb -- FAILED");
            else
                hud.printf(1, row++, "Fox.glb -- Loading...");

            row++;
            hud.printf(1, row++, "Selected Instance: %d/%d  |  Clip: %s  |  Joints: %u",
                       selectedInstance + 1, kNumInstances, clipName, jointCount);
            hud.printf(1, row++, "Time: %.2f / %.2f s  |  Speed: %.2fx  |  %s", currentTime,
                       clipDuration, animSpeed, isPlaying ? "PLAYING" : "PAUSED");

            if (isBlending)
            {
                hud.printf(1, row++, "Blending -> %s  [%.0f%%]", nextClipName,
                           blendProgress * 100.0f);
            }
            hud.printf(1, row++, "IK: %s  |  Weight: %.2f", enableIk ? "ON" : "OFF", ikBlendWeight);
            hud.printf(1, row++, "Clips available: %u", totalClipCount);

            // Animation events display.
            if (!recentEvents.empty())
            {
                hud.printf(1, row++, "Events (last 3s):");
                int shown = 0;
                for (int ei = static_cast<int>(recentEvents.size()) - 1; ei >= 0 && shown < 5;
                     --ei, ++shown)
                {
                    hud.printf(3, row++, "  [%.1fs ago] %s", recentEvents[ei].age,
                               recentEvents[ei].name.c_str());
                }
            }

            // State machine display.
            if (stateMachineSetup)
            {
                EntityID smEntity = instances[2].animatedEntity;
                auto* smComp = reg.get<AnimStateMachineComponent>(smEntity);
                if (smComp && smComp->machine)
                {
                    const auto& state = smComp->machine->states[smComp->currentState];
                    hud.printf(1, row++, "State Machine (#3): %s  |  param speed=%.2f",
                               state.name.c_str(), smSpeedParam);
                    hud.printf(1, row++, "  Left/Right arrows to change speed param");
                }
            }

            row++;
            hud.printf(1, row++, "--- Controls ---");
            hud.printf(1, row++, "Space=play/pause  Up/Down=speed  B=blend");
            hud.printf(1, row++, "1-9=clip  I=toggle IK  R=reset  H=hide HUD");
            hud.printf(1, row++, "Left/Right=state machine speed  RMB=orbit  WASD=move");
        }
        hud.end();

        // -- ImGui panel (anchored to right side) --------------------------------
        ImGui::SetNextWindowPos(ImVec2(fbW - 340.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 460), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Animation Controls"))
        {
            // Instance selector
            ImGui::Text("Instance Selection");
            for (int i = 0; i < kNumInstances; ++i)
            {
                ImGui::SameLine();
                char label[16];
                snprintf(label, sizeof(label), "%d", i + 1);
                if (ImGui::RadioButton(label, selectedInstance == i))
                    selectedInstance = i;
            }
            ImGui::Separator();

            // Playback controls
            ImGui::Text("Clip: %s", clipName);
            ImGui::Text("Joints: %u  |  Clips: %u", jointCount, totalClipCount);
            ImGui::Separator();

            if (ImGui::Button(isPlaying ? "Pause" : "Play", ImVec2(80, 0)))
            {
                if (selEntity != INVALID_ENTITY)
                {
                    auto* ac = reg.get<AnimatorComponent>(selEntity);
                    if (ac)
                        ac->flags ^= AnimatorComponent::kFlagPlaying;
                }
            }
            ImGui::SameLine();

            if (ImGui::Button("Stop", ImVec2(80, 0)))
            {
                if (selEntity != INVALID_ENTITY)
                {
                    auto* ac = reg.get<AnimatorComponent>(selEntity);
                    if (ac)
                    {
                        ac->playbackTime = 0.0f;
                        ac->flags &= ~AnimatorComponent::kFlagPlaying;
                    }
                }
            }
            ImGui::SameLine();

            if (ImGui::Button("Reset All", ImVec2(80, 0)))
            {
                reg.view<AnimatorComponent>().each(
                    [](EntityID, AnimatorComponent& ac)
                    {
                        ac.playbackTime = 0.0f;
                        ac.speed = 1.0f;
                        ac.flags =
                            AnimatorComponent::kFlagPlaying | AnimatorComponent::kFlagLooping;
                        ac.blendFactor = 0.0f;
                        ac.blendDuration = 0.0f;
                        ac.blendElapsed = 0.0f;
                        ac.nextClipId = UINT32_MAX;
                    });
            }

            ImGui::Separator();

            // Speed slider (allows negative for reverse playback)
            float speedVal = animSpeed;
            if (ImGui::SliderFloat("Speed", &speedVal, -2.0f, 5.0f, "%.2f"))
            {
                if (selEntity != INVALID_ENTITY)
                {
                    auto* ac = reg.get<AnimatorComponent>(selEntity);
                    if (ac)
                        ac->speed = speedVal;
                }
            }

            ImGui::Separator();

            // Time and progress
            ImGui::Text("Time: %.2f / %.2f s", currentTime, clipDuration);
            float progress = (clipDuration > 0.0f) ? (currentTime / clipDuration) : 0.0f;
            ImGui::ProgressBar(progress, ImVec2(-1, 0));

            // Scrub slider
            float scrubTime = currentTime;
            if (ImGui::SliderFloat("Scrub", &scrubTime, 0.0f, clipDuration, "%.3f"))
            {
                if (selEntity != INVALID_ENTITY)
                {
                    auto* ac = reg.get<AnimatorComponent>(selEntity);
                    if (ac)
                        ac->playbackTime = scrubTime;
                }
            }

            ImGui::Separator();

            // Crossfade / blending
            ImGui::Text("Crossfade Blending");
            if (isBlending)
            {
                ImGui::Text("Blending: %s -> %s [%.0f%%]", clipName, nextClipName,
                            blendProgress * 100.0f);
            }
            else
            {
                ImGui::Text("Not blending");
            }
            if (totalClipCount > 1)
            {
                if (ImGui::Button("Blend to Next Clip", ImVec2(-1, 0)))
                {
                    if (selEntity != INVALID_ENTITY)
                    {
                        auto* ac = reg.get<AnimatorComponent>(selEntity);
                        if (ac && !(ac->flags & AnimatorComponent::kFlagBlending))
                        {
                            uint32_t nextClip = (ac->clipId + 1) % totalClipCount;
                            ac->nextClipId = nextClip;
                            ac->blendFactor = 0.0f;
                            ac->blendDuration = kDefaultBlendDuration;
                            ac->blendElapsed = 0.0f;
                            ac->flags |= AnimatorComponent::kFlagBlending;
                        }
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("Only 1 clip loaded (blend disabled)");
            }

            ImGui::Separator();

            // IK controls
            ImGui::Text("Inverse Kinematics");
            ImGui::Checkbox("Enable IK", &enableIk);
            if (enableIk)
            {
                ImGui::SliderFloat("IK Blend", &ikBlendWeight, 0.0f, 1.0f, "%.2f");
                ImGui::Text("IK Target:");
                ImGui::SliderFloat("Tgt X", &ikTargetPos.x, -3.0f, 3.0f, "%.2f");
                ImGui::SliderFloat("Tgt Y", &ikTargetPos.y, 0.0f, 3.0f, "%.2f");
                ImGui::SliderFloat("Tgt Z", &ikTargetPos.z, -3.0f, 3.0f, "%.2f");
            }

            // Show/hide target marker based on IK toggle.
            if (enableIk && !reg.has<VisibleTag>(targetMarker))
                reg.emplace<VisibleTag>(targetMarker);
            else if (!enableIk && reg.has<VisibleTag>(targetMarker))
                reg.remove<VisibleTag>(targetMarker);

            ImGui::Separator();

            // Per-instance speed overview
            ImGui::Text("Instance Speeds:");
            for (int i = 0; i < kNumInstances; ++i)
            {
                EntityID e = instances[i].animatedEntity;
                if (e != INVALID_ENTITY)
                {
                    auto* ac = reg.get<AnimatorComponent>(e);
                    if (ac)
                    {
                        ImGui::Text(
                            "  #%d: speed=%.2f  time=%.2f  %s", i + 1, ac->speed, ac->playbackTime,
                            (ac->flags & AnimatorComponent::kFlagPlaying) ? "PLAY" : "STOP");
                    }
                }
            }

            // State machine controls
            if (stateMachineSetup)
            {
                ImGui::Separator();
                ImGui::Text("State Machine (Instance #3)");
                EntityID smEntity = instances[2].animatedEntity;
                auto* smComp = reg.get<AnimStateMachineComponent>(smEntity);
                if (smComp && smComp->machine)
                {
                    const auto& state = smComp->machine->states[smComp->currentState];
                    ImGui::Text("Current State: %s", state.name.c_str());
                }
                if (ImGui::SliderFloat("SM Speed", &smSpeedParam, 0.0f, 3.0f, "%.2f"))
                {
                    // Parameter updated in the per-frame section above.
                }
            }

            // Recent events
            if (!recentEvents.empty())
            {
                ImGui::Separator();
                ImGui::Text("Recent Events:");
                int shown = 0;
                for (int ei = static_cast<int>(recentEvents.size()) - 1; ei >= 0 && shown < 5;
                     --ei, ++shown)
                {
                    ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "  [%.1fs] %s",
                                       recentEvents[ei].age, recentEvents[ei].name.c_str());
                }
            }

            ImGui::Separator();
            glm::vec3 cp = cam.position();
            ImGui::Text("Camera: (%.1f, %.1f, %.1f)", cp.x, cp.y, cp.z);
            ImGui::Text("Target: (%.1f, %.1f, %.1f)", cam.target.x, cam.target.y, cam.target.z);
        }
        ImGui::End();

        double frameEnd = glfwGetTime();
        renderMs = static_cast<float>((frameEnd - frameStart) * 1000.0);

        eng.endFrame();
    }

    // -- Cleanup --------------------------------------------------------------
    hud.shutdown();
    assets.release(modelHandle);
    ibl.shutdown();

    return 0;
}
