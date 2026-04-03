// IK Hand Demo -- macOS
//
// Interactive IK demo: left-click drag moves the right hand IK target in 3D
// space, and the two-bone IK solver drives the arm chain in real-time.
// The model starts in T-pose (bind pose, no animation playing).
//
// Controls:
//   Left-drag   -- move right hand IK target
//   Right-drag  -- orbit camera
//   Scroll      -- zoom
//   WASD        -- move camera target
//   ImGui panel -- IK blend weight, arm toggles, reset

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSystem.h"
#include "engine/animation/IkComponents.h"
#include "engine/animation/IkSolvers.h"
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
// Helpers
// =============================================================================

// Find a joint index by substring match in debug joint names.
// Returns -1 if not found.
static int32_t findJointBySubstring(const Skeleton& skel, const std::string& substr)
{
#if !defined(NDEBUG)
    for (uint32_t i = 0; i < skel.debugJointNames.size(); ++i)
    {
        if (skel.debugJointNames[i].find(substr) != std::string::npos)
            return static_cast<int32_t>(i);
    }
#else
    (void)skel;
    (void)substr;
#endif
    return -1;
}

// Find a joint index by exact debug name match.
static int32_t findJointByName(const Skeleton& skel, const std::string& name)
{
#if !defined(NDEBUG)
    for (uint32_t i = 0; i < skel.debugJointNames.size(); ++i)
    {
        if (skel.debugJointNames[i] == name)
            return static_cast<int32_t>(i);
    }
#else
    (void)skel;
    (void)name;
#endif
    return -1;
}

// Unproject a screen-space point (in pixels) to a world-space ray direction.
static glm::vec3 unprojectScreenToWorld(float screenX, float screenY, float fbW, float fbH,
                                        const glm::mat4& invViewProj)
{
    // Convert to NDC [-1, 1].
    float ndcX = (2.0f * screenX / fbW) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenY / fbH);  // flip Y

    glm::vec4 nearPt = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPt = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPt /= nearPt.w;
    farPt /= farPt.w;

    return glm::normalize(glm::vec3(farPt) - glm::vec3(nearPt));
}

// Intersect a ray with a plane. Returns the intersection point.
// planePoint is a point on the plane, planeNormal is the plane normal.
static glm::vec3 rayPlaneIntersection(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                      const glm::vec3& planePoint, const glm::vec3& planeNormal)
{
    float denom = glm::dot(planeNormal, rayDir);
    if (std::abs(denom) < 1e-6f)
    {
        // Ray parallel to plane -- return the plane point as fallback.
        return planePoint;
    }
    float t = glm::dot(planePoint - rayOrigin, planeNormal) / denom;
    return rayOrigin + rayDir * t;
}

// Helper to create a small marker cube entity.
static EntityID createMarkerCube(Registry& reg, uint32_t meshId, uint32_t matId, glm::vec3 pos,
                                 float size)
{
    EntityID e = reg.createEntity();
    TransformComponent tc{};
    tc.position = pos;
    tc.rotation = glm::quat(1, 0, 0, 0);
    tc.scale = glm::vec3(size);
    tc.flags = 1;
    reg.emplace<TransformComponent>(e, tc);
    reg.emplace<WorldTransformComponent>(e);
    reg.emplace<MeshComponent>(e, MeshComponent{meshId});
    reg.emplace<MaterialComponent>(e, MaterialComponent{matId});
    reg.emplace<VisibleTag>(e);
    return e;
}

// =============================================================================
// Entry point
// =============================================================================

static float s_zoomScrollDelta = 0.f;

int main()
{
    Engine eng;
    EngineDesc desc;
    desc.windowTitle = "IK Hand Demo";
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

    // -- Marker materials -----------------------------------------------------
    Material targetMarkerMat;
    targetMarkerMat.albedo = {1.0f, 1.0f, 0.0f, 1.0f};
    targetMarkerMat.emissiveScale = 3.0f;
    targetMarkerMat.roughness = 1.0f;
    uint32_t targetMatId = eng.resources().addMaterial(targetMarkerMat);

    Material jointMarkerMat;
    jointMarkerMat.albedo = {0.0f, 1.0f, 0.3f, 1.0f};
    jointMarkerMat.emissiveScale = 2.0f;
    jointMarkerMat.roughness = 1.0f;
    uint32_t jointMatId = eng.resources().addMaterial(jointMarkerMat);

    Material leftJointMarkerMat;
    leftJointMarkerMat.albedo = {0.3f, 0.5f, 1.0f, 1.0f};
    leftJointMarkerMat.emissiveScale = 2.0f;
    leftJointMarkerMat.roughness = 1.0f;
    uint32_t leftJointMatId = eng.resources().addMaterial(leftJointMarkerMat);

    // -- IK target markers ----------------------------------------------------
    glm::vec3 rightHandTarget{0.8f, 1.2f, 0.0f};
    glm::vec3 leftHandTarget{-0.8f, 1.2f, 0.0f};
    EntityID rightTargetMarker =
        createMarkerCube(reg, cubeMeshId, targetMatId, rightHandTarget, 0.04f);
    EntityID leftTargetMarker =
        createMarkerCube(reg, cubeMeshId, targetMatId, leftHandTarget, 0.04f);

    // Joint debug markers (6 joints: shoulder/elbow/hand x2 arms).
    EntityID rightJointMarkers[3] = {};
    EntityID leftJointMarkers[3] = {};
    for (int i = 0; i < 3; ++i)
    {
        rightJointMarkers[i] =
            createMarkerCube(reg, cubeMeshId, jointMatId, glm::vec3(0.0f), 0.03f);
        leftJointMarkers[i] =
            createMarkerCube(reg, cubeMeshId, leftJointMatId, glm::vec3(0.0f), 0.03f);
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
    cam.distance = 4.0f;
    cam.pitch = 10.0f;
    cam.yaw = 0.0f;
    cam.target = {0, 0.8f, 0};
    bool rightDragging = false;
    bool leftDragging = false;
    double prevMouseX = 0.0, prevMouseY = 0.0;

    float renderMs = 0.f;

    // -- IK state -------------------------------------------------------------
    float ikBlendWeight = 1.0f;
    bool enableRightIk = true;
    bool enableLeftIk = false;

    // Arm joint indices (discovered at spawn time from skeleton).
    int32_t rightShoulderIdx = -1, rightElbowIdx = -1, rightHandIdx = -1;
    int32_t leftShoulderIdx = -1, leftElbowIdx = -1, leftHandIdx = -1;
    EntityID skinnedEntity = INVALID_ENTITY;

    // Bind pose storage for reset.
    Pose bindPose;
    bool bindPoseStored = false;

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

        // Build view/projection for mouse unprojection.
        glm::mat4 viewMat = cam.view();
        glm::vec3 camPos = cam.position();
        glm::mat4 projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 50.f);
        glm::mat4 invViewProj = glm::inverse(projMat * viewMat);

        // -- Mouse input: left-drag = IK target, right-drag = orbit -----------
        if (!imguiWants)
        {
            bool leftDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool rightDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            // Right-drag: orbit camera.
            if (rightDown)
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

            // Left-drag: move right hand IK target.
            if (leftDown && enableRightIk)
            {
                // Unproject mouse to a ray and intersect with a plane at
                // the model's depth (perpendicular to camera forward).
                glm::vec3 rayDir = unprojectScreenToWorld(
                    static_cast<float>(mx), static_cast<float>(my), fbW, fbH, invViewProj);
                glm::vec3 camFwd = glm::normalize(cam.target - camPos);
                glm::vec3 hitPos = rayPlaneIntersection(camPos, rayDir, rightHandTarget, camFwd);
                rightHandTarget = hitPos;
                leftDragging = true;
            }
            else
            {
                leftDragging = false;
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

            // Stop all animations (T-pose): clear the playing flag.
            reg.view<AnimatorComponent>().each(
                [&](EntityID, AnimatorComponent& ac)
                {
                    ac.flags &= ~AnimatorComponent::kFlagPlaying;
                    ac.playbackTime = 0.0f;
                });

            // Find arm joints and set up IK chains.
            reg.view<SkeletonComponent, AnimatorComponent>().each(
                [&](EntityID entity, const SkeletonComponent& sc, AnimatorComponent& /*ac*/)
                {
                    const Skeleton* skel = animRes.getSkeleton(sc.skeletonId);
                    if (!skel || skel->jointCount() < 3)
                        return;

                    skinnedEntity = entity;

                // Discover joint indices by name.
                // BrainStem uses joint names from the glTF hierarchy.
                // We search for common arm joint name patterns.
#if !defined(NDEBUG)
                    // Print all joint names for discovery.
                    fprintf(stderr, "ik_hand_demo: skeleton has %u joints:\n", skel->jointCount());
                    for (uint32_t i = 0; i < skel->jointCount(); ++i)
                    {
                        fprintf(stderr, "  [%u] %s (parent=%d)\n", i,
                                skel->debugJointNames[i].c_str(), skel->joints[i].parentIndex);
                    }
#endif

                    // Try common naming patterns for BrainStem humanoid.
                    // Right arm chain.
                    rightShoulderIdx = findJointBySubstring(*skel, "RightArm");
                    if (rightShoulderIdx < 0)
                        rightShoulderIdx = findJointBySubstring(*skel, "rightArm");
                    if (rightShoulderIdx < 0)
                        rightShoulderIdx = findJointBySubstring(*skel, "R_Arm");
                    if (rightShoulderIdx < 0)
                        rightShoulderIdx = findJointBySubstring(*skel, "right_arm");

                    rightElbowIdx = findJointBySubstring(*skel, "RightForeArm");
                    if (rightElbowIdx < 0)
                        rightElbowIdx = findJointBySubstring(*skel, "rightForeArm");
                    if (rightElbowIdx < 0)
                        rightElbowIdx = findJointBySubstring(*skel, "R_ForeArm");
                    if (rightElbowIdx < 0)
                        rightElbowIdx = findJointBySubstring(*skel, "right_forearm");

                    rightHandIdx = findJointBySubstring(*skel, "RightHand");
                    if (rightHandIdx < 0)
                        rightHandIdx = findJointBySubstring(*skel, "rightHand");
                    if (rightHandIdx < 0)
                        rightHandIdx = findJointBySubstring(*skel, "R_Hand");
                    if (rightHandIdx < 0)
                        rightHandIdx = findJointBySubstring(*skel, "right_hand");

                    // Left arm chain.
                    leftShoulderIdx = findJointBySubstring(*skel, "LeftArm");
                    if (leftShoulderIdx < 0)
                        leftShoulderIdx = findJointBySubstring(*skel, "leftArm");
                    if (leftShoulderIdx < 0)
                        leftShoulderIdx = findJointBySubstring(*skel, "L_Arm");
                    if (leftShoulderIdx < 0)
                        leftShoulderIdx = findJointBySubstring(*skel, "left_arm");

                    leftElbowIdx = findJointBySubstring(*skel, "LeftForeArm");
                    if (leftElbowIdx < 0)
                        leftElbowIdx = findJointBySubstring(*skel, "leftForeArm");
                    if (leftElbowIdx < 0)
                        leftElbowIdx = findJointBySubstring(*skel, "L_ForeArm");
                    if (leftElbowIdx < 0)
                        leftElbowIdx = findJointBySubstring(*skel, "left_forearm");

                    leftHandIdx = findJointBySubstring(*skel, "LeftHand");
                    if (leftHandIdx < 0)
                        leftHandIdx = findJointBySubstring(*skel, "leftHand");
                    if (leftHandIdx < 0)
                        leftHandIdx = findJointBySubstring(*skel, "L_Hand");
                    if (leftHandIdx < 0)
                        leftHandIdx = findJointBySubstring(*skel, "left_hand");

                    fprintf(stderr, "ik_hand_demo: Right arm: shoulder=%d elbow=%d hand=%d\n",
                            rightShoulderIdx, rightElbowIdx, rightHandIdx);
                    fprintf(stderr, "ik_hand_demo: Left arm:  shoulder=%d elbow=%d hand=%d\n",
                            leftShoulderIdx, leftElbowIdx, leftHandIdx);

                    // Build IK chains for discovered arms.
                    IkChainsComponent chains;
                    IkTargetsComponent targets;

                    bool hasRight =
                        (rightShoulderIdx >= 0 && rightElbowIdx >= 0 && rightHandIdx >= 0);
                    bool hasLeft = (leftShoulderIdx >= 0 && leftElbowIdx >= 0 && leftHandIdx >= 0);

                    if (hasRight)
                    {
                        IkChainDef chain;
                        chain.rootJoint = static_cast<uint32_t>(rightShoulderIdx);
                        chain.midJoint = static_cast<uint32_t>(rightElbowIdx);
                        chain.endEffectorJoint = static_cast<uint32_t>(rightHandIdx);
                        chain.solverType = IkSolverType::TwoBone;
                        // Pole vector pointing backward for natural elbow bend.
                        chain.poleVector = engine::math::Vec3{0, 0, -1};
                        chain.weight = ikBlendWeight;
                        chain.enabled = enableRightIk ? 1 : 0;
                        chains.chains.push_back(chain);

                        IkTarget target;
                        target.position = rightHandTarget;
                        targets.targets.push_back(target);
                    }

                    if (hasLeft)
                    {
                        IkChainDef chain;
                        chain.rootJoint = static_cast<uint32_t>(leftShoulderIdx);
                        chain.midJoint = static_cast<uint32_t>(leftElbowIdx);
                        chain.endEffectorJoint = static_cast<uint32_t>(leftHandIdx);
                        chain.solverType = IkSolverType::TwoBone;
                        chain.poleVector = engine::math::Vec3{0, 0, -1};
                        chain.weight = ikBlendWeight;
                        chain.enabled = enableLeftIk ? 1 : 0;
                        chains.chains.push_back(chain);

                        IkTarget target;
                        target.position = leftHandTarget;
                        targets.targets.push_back(target);
                    }

                    if (!chains.chains.empty())
                    {
                        reg.emplace<IkChainsComponent>(entity, std::move(chains));
                        reg.emplace<IkTargetsComponent>(entity, std::move(targets));
                    }

                    // Store bind pose for reset.
                    // The bind pose is identity transforms for each joint --
                    // this is what produces the T-pose since inverse bind
                    // matrices are applied during bone matrix computation.
                    bindPose.jointPoses.resize(skel->jointCount());
                    for (uint32_t i = 0; i < skel->jointCount(); ++i)
                    {
                        bindPose.jointPoses[i] = JointPose{};  // identity
                    }
                    bindPoseStored = true;
                });
        }
        else if (modelState == AssetState::Failed)
        {
            static bool printed = false;
            if (!printed)
            {
                printed = true;
                fprintf(stderr, "ik_hand_demo: asset load failed: %s\n",
                        assets.error(modelHandle).c_str());
            }
        }

        // -- Update IK chain parameters from ImGui state ----------------------
        int rightChainIdx = -1;
        int leftChainIdx = -1;

        // Figure out which chain index corresponds to which arm.
        {
            int idx = 0;
            bool hasRight = (rightShoulderIdx >= 0 && rightElbowIdx >= 0 && rightHandIdx >= 0);
            bool hasLeft = (leftShoulderIdx >= 0 && leftElbowIdx >= 0 && leftHandIdx >= 0);
            if (hasRight)
            {
                rightChainIdx = idx++;
            }
            if (hasLeft)
            {
                leftChainIdx = idx++;
            }
        }

        reg.view<IkChainsComponent, IkTargetsComponent>().each(
            [&](EntityID, IkChainsComponent& chainsComp, IkTargetsComponent& targetsComp)
            {
                if (rightChainIdx >= 0 &&
                    rightChainIdx < static_cast<int>(chainsComp.chains.size()))
                {
                    chainsComp.chains[rightChainIdx].enabled = enableRightIk ? 1 : 0;
                    chainsComp.chains[rightChainIdx].weight = ikBlendWeight;
                }
                if (leftChainIdx >= 0 && leftChainIdx < static_cast<int>(chainsComp.chains.size()))
                {
                    chainsComp.chains[leftChainIdx].enabled = enableLeftIk ? 1 : 0;
                    chainsComp.chains[leftChainIdx].weight = ikBlendWeight;
                }

                if (rightChainIdx >= 0 &&
                    rightChainIdx < static_cast<int>(targetsComp.targets.size()))
                {
                    targetsComp.targets[rightChainIdx].position = rightHandTarget;
                }
                if (leftChainIdx >= 0 &&
                    leftChainIdx < static_cast<int>(targetsComp.targets.size()))
                {
                    targetsComp.targets[leftChainIdx].position = leftHandTarget;
                }
            });

        // Update target marker positions.
        auto* rtc = reg.get<TransformComponent>(rightTargetMarker);
        if (rtc)
        {
            rtc->position = rightHandTarget;
            rtc->flags |= 1;
        }
        auto* ltc = reg.get<TransformComponent>(leftTargetMarker);
        if (ltc)
        {
            ltc->position = leftHandTarget;
            ltc->flags |= 1;
        }

        // -- Animation + IK system update -------------------------------------
        auto* arena = eng.frameArena().resource();

        // Phase 1: FK pose sampling.
        // Since the animator is not playing, updatePoses will still sample
        // the clip at time 0 (the rest/bind pose frame).
        animSys.updatePoses(reg, dt, animRes, arena);

        // For T-pose: if PoseComponent was not created by updatePoses
        // (e.g., if clip is invalid), manually create one from bind pose.
        if (modelSpawned && bindPoseStored && skinnedEntity != INVALID_ENTITY)
        {
            auto* existingPose = reg.get<PoseComponent>(skinnedEntity);
            if (!existingPose || !existingPose->pose)
            {
                auto* alloc = arena ? arena : std::pmr::get_default_resource();
                auto* posePtr = static_cast<Pose*>(alloc->allocate(sizeof(Pose), alignof(Pose)));
                new (posePtr) Pose(bindPose);

                if (existingPose)
                {
                    existingPose->pose = posePtr;
                }
                else
                {
                    reg.emplace<PoseComponent>(skinnedEntity, PoseComponent{posePtr});
                }
            }
        }

        // Phase 2: IK post-process.
        ikSys.update(reg, animRes, arena);

        // Phase 3: Bone matrix computation.
        animSys.computeBoneMatrices(reg, animRes, arena);

        // -- Update joint debug marker positions from bone world positions -----
        if (modelSpawned && skinnedEntity != INVALID_ENTITY)
        {
            auto* poseComp = reg.get<PoseComponent>(skinnedEntity);
            auto* skelComp = reg.get<SkeletonComponent>(skinnedEntity);
            if (poseComp && poseComp->pose && skelComp)
            {
                const Skeleton* skel = animRes.getSkeleton(skelComp->skeletonId);
                if (skel)
                {
                    // Compute world positions for joint markers.
                    std::vector<engine::math::Vec3> worldPos(skel->jointCount());
                    computeWorldPositions(*skel, *poseComp->pose, worldPos.data());

                    int32_t rightJoints[3] = {rightShoulderIdx, rightElbowIdx, rightHandIdx};
                    int32_t leftJoints[3] = {leftShoulderIdx, leftElbowIdx, leftHandIdx};

                    for (int i = 0; i < 3; ++i)
                    {
                        if (rightJoints[i] >= 0)
                        {
                            auto* jtc = reg.get<TransformComponent>(rightJointMarkers[i]);
                            if (jtc)
                            {
                                jtc->position = worldPos[rightJoints[i]];
                                jtc->flags |= 1;
                            }
                        }
                        if (leftJoints[i] >= 0)
                        {
                            auto* jtc = reg.get<TransformComponent>(leftJointMarkers[i]);
                            if (jtc)
                            {
                                jtc->position = worldPos[leftJoints[i]];
                                jtc->flags |= 1;
                            }
                        }
                    }
                }
            }
        }

        // -- Transform system -------------------------------------------------
        transformSys.update(reg);

        // -- Render -----------------------------------------------------------
        double frameStart = glfwGetTime();
        eng.renderer().beginFrameDirect();

        // Recompute view/proj (camera may have moved from orbit/WASD).
        viewMat = cam.view();
        camPos = cam.position();
        projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 50.f);

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
        bgfx::dbgTextPrintf(1, 1, 0x0f, "IK Hand Demo  |  %.1f fps  |  %.3f ms  |  render %.3f ms",
                            dt > 0 ? 1.f / dt : 0.f, dt * 1000.f, renderMs);
        bgfx::dbgTextPrintf(1, 2, 0x07,
                            "LMB=move hand  |  RMB=orbit  |  Scroll=zoom  |  WASD=move");

        if (modelState == AssetState::Ready)
            bgfx::dbgTextPrintf(1, 3, 0x0a, "BrainStem.glb — Ready");
        else if (modelState == AssetState::Failed)
            bgfx::dbgTextPrintf(1, 3, 0x0c, "BrainStem.glb — FAILED");
        else
            bgfx::dbgTextPrintf(1, 3, 0x0e, "BrainStem.glb — Loading...");

        // -- ImGui panel ------------------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(10, 80), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 380), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("IK Hand Controls"))
        {
            ImGui::SliderFloat("IK Blend Weight", &ikBlendWeight, 0.0f, 1.0f, "%.2f");

            ImGui::Separator();
            ImGui::Checkbox("Right Arm IK", &enableRightIk);
            ImGui::Checkbox("Left Arm IK", &enableLeftIk);

            ImGui::Separator();
            ImGui::Text("Right Hand Target:");
            ImGui::Text("  (%.2f, %.2f, %.2f)", rightHandTarget.x, rightHandTarget.y,
                        rightHandTarget.z);
            ImGui::SliderFloat("R.X", &rightHandTarget.x, -2.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("R.Y", &rightHandTarget.y, -1.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("R.Z", &rightHandTarget.z, -2.0f, 2.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("Left Hand Target:");
            ImGui::Text("  (%.2f, %.2f, %.2f)", leftHandTarget.x, leftHandTarget.y,
                        leftHandTarget.z);
            ImGui::SliderFloat("L.X", &leftHandTarget.x, -2.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("L.Y", &leftHandTarget.y, -1.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("L.Z", &leftHandTarget.z, -2.0f, 2.0f, "%.2f");

            ImGui::Separator();
            if (ImGui::Button("Reset to T-Pose", ImVec2(-1, 0)))
            {
                rightHandTarget = glm::vec3{0.8f, 1.2f, 0.0f};
                leftHandTarget = glm::vec3{-0.8f, 1.2f, 0.0f};
                ikBlendWeight = 1.0f;
            }

            ImGui::Separator();
            ImGui::Text("Joints: R_shoulder=%d R_elbow=%d R_hand=%d", rightShoulderIdx,
                        rightElbowIdx, rightHandIdx);
            ImGui::Text("Joints: L_shoulder=%d L_elbow=%d L_hand=%d", leftShoulderIdx, leftElbowIdx,
                        leftHandIdx);

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
