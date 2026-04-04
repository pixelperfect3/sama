// Physics Demo — GameRunner migration
//
// Demonstrates Jolt physics integration via the IGame interface.
// Multiple colored cubes fall onto a tilting plane.
//
// Controls:
//   Arrow keys          — tilt the ground plane
//   R                   — reset all cubes
//   Right click + drag  — orbit camera
//   Scroll              — zoom

#define GLFW_INCLUDE_NONE
#include "PhysicsGame.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/input/InputState.h"
#include "engine/input/Key.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#include "imgui.h"

using namespace engine::core;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::physics;
using namespace engine::rendering;

// =============================================================================
// Cube category definitions
// =============================================================================

enum class CubeCategory
{
    Heavy,
    Medium,
    Light,
    Bouncy
};

struct CubeDef
{
    CubeCategory category;
    glm::vec4 color;
    float scale;
    float mass;
    float restitution;
    float friction;
};

// clang-format off
static const CubeDef kCubeDefs[] = {
    // Heavy — dark red
    {CubeCategory::Heavy,  {0.6f, 0.1f, 0.1f, 1.0f}, 0.8f, 5.0f, 0.1f, 0.8f},
    {CubeCategory::Heavy,  {0.6f, 0.1f, 0.1f, 1.0f}, 0.8f, 5.0f, 0.1f, 0.8f},
    {CubeCategory::Heavy,  {0.6f, 0.1f, 0.1f, 1.0f}, 0.8f, 5.0f, 0.1f, 0.8f},
    {CubeCategory::Heavy,  {0.6f, 0.1f, 0.1f, 1.0f}, 0.8f, 5.0f, 0.1f, 0.8f},
    // Medium — orange
    {CubeCategory::Medium, {1.0f, 0.6f, 0.2f, 1.0f}, 0.5f, 1.0f, 0.3f, 0.5f},
    {CubeCategory::Medium, {1.0f, 0.6f, 0.2f, 1.0f}, 0.5f, 1.0f, 0.3f, 0.5f},
    {CubeCategory::Medium, {1.0f, 0.6f, 0.2f, 1.0f}, 0.5f, 1.0f, 0.3f, 0.5f},
    {CubeCategory::Medium, {1.0f, 0.6f, 0.2f, 1.0f}, 0.5f, 1.0f, 0.3f, 0.5f},
    {CubeCategory::Medium, {1.0f, 0.6f, 0.2f, 1.0f}, 0.5f, 1.0f, 0.3f, 0.5f},
    // Light — yellow
    {CubeCategory::Light,  {1.0f, 1.0f, 0.3f, 1.0f}, 0.3f, 0.2f, 0.5f, 0.3f},
    {CubeCategory::Light,  {1.0f, 1.0f, 0.3f, 1.0f}, 0.3f, 0.2f, 0.5f, 0.3f},
    {CubeCategory::Light,  {1.0f, 1.0f, 0.3f, 1.0f}, 0.3f, 0.2f, 0.5f, 0.3f},
    {CubeCategory::Light,  {1.0f, 1.0f, 0.3f, 1.0f}, 0.3f, 0.2f, 0.5f, 0.3f},
    // Bouncy — cyan
    {CubeCategory::Bouncy, {0.2f, 0.9f, 1.0f, 1.0f}, 0.4f, 0.5f, 0.9f, 0.2f},
    {CubeCategory::Bouncy, {0.2f, 0.9f, 1.0f, 1.0f}, 0.4f, 0.5f, 0.9f, 0.2f},
    {CubeCategory::Bouncy, {0.2f, 0.9f, 1.0f, 1.0f}, 0.4f, 0.5f, 0.9f, 0.2f},
    {CubeCategory::Bouncy, {0.2f, 0.9f, 1.0f, 1.0f}, 0.4f, 0.5f, 0.9f, 0.2f},
};
// clang-format on

// =============================================================================
// Random helpers
// =============================================================================

float PhysicsGame::randomFloat(float lo, float hi)
{
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng_);
}

glm::vec3 PhysicsGame::randomSpawnPos()
{
    return {randomFloat(-5.0f, 5.0f), randomFloat(8.0f, 15.0f), randomFloat(-5.0f, 5.0f)};
}

// =============================================================================
// Scroll callback state
// =============================================================================

static float s_zoomScrollDelta = 0.f;

// =============================================================================
// IGame implementation
// =============================================================================

void PhysicsGame::onInit(Engine& engine, Registry& registry)
{
    registry_ = &registry;

    // Hook up zoom scroll.
    glfwSetScrollCallback(engine.glfwHandle(),
                          [](GLFWwindow* win, double /*xoff*/, double yoff)
                          {
                              auto* e = static_cast<Engine*>(glfwGetWindowUserPointer(win));
                              if (e)
                                  e->imguiScrollAccum() += static_cast<float>(yoff);
                              s_zoomScrollDelta += static_cast<float>(yoff);
                          });

    // -- IBL (procedural sky/ground) -----------------------------------------
    ibl_.generateDefault();

    // -- Create cube mesh (shared by all entities) ----------------------------
    MeshData cubeData = makeCubeMeshData();
    Mesh cubeMesh = buildMesh(cubeData);
    uint32_t cubeMeshId = engine.resources().addMesh(std::move(cubeMesh));

    // -- Create materials -----------------------------------------------------
    Material groundMat;
    groundMat.albedo = {0.3f, 0.3f, 0.3f, 1.0f};
    groundMat.roughness = 0.7f;
    groundMat.metallic = 0.0f;
    uint32_t groundMatId = engine.resources().addMaterial(groundMat);

    uint32_t cubeMatIds[kCubeCount];
    for (int i = 0; i < kCubeCount; i++)
    {
        Material mat;
        mat.albedo = kCubeDefs[i].color;
        mat.roughness = 0.5f;
        mat.metallic = 0.0f;
        cubeMatIds[i] = engine.resources().addMaterial(mat);
    }

    // -- Physics engine -------------------------------------------------------
    if (!physics_.init())
    {
        fprintf(stderr, "PhysicsGame: failed to initialize Jolt physics\n");
        return;
    }

    // -- Create ground plane entity -------------------------------------------
    groundEntity_ = registry.createEntity();
    {
        TransformComponent tc;
        tc.position = {0.0f, 0.0f, 0.0f};
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {10.0f, 0.2f, 10.0f};
        tc.flags = 1;
        registry.emplace<TransformComponent>(groundEntity_, tc);
        registry.emplace<WorldTransformComponent>(groundEntity_);
        registry.emplace<MeshComponent>(groundEntity_, cubeMeshId);
        registry.emplace<MaterialComponent>(groundEntity_, groundMatId);
        registry.emplace<VisibleTag>(groundEntity_);
        registry.emplace<ShadowVisibleTag>(groundEntity_, ShadowVisibleTag{0xFF});

        RigidBodyComponent rb;
        rb.mass = 0.0f;
        rb.type = BodyType::Kinematic;
        rb.friction = 0.8f;
        rb.restitution = 0.2f;
        registry.emplace<RigidBodyComponent>(groundEntity_, rb);

        ColliderComponent col;
        col.shape = ColliderShape::Box;
        col.halfExtents = {5.0f, 0.1f, 5.0f};
        registry.emplace<ColliderComponent>(groundEntity_, col);
    }

    // -- Create dynamic cubes -------------------------------------------------
    for (int i = 0; i < kCubeCount; i++)
    {
        const CubeDef& def = kCubeDefs[i];
        cubeEntities_[i] = registry.createEntity();

        glm::vec3 spawnPos = randomSpawnPos();

        TransformComponent tc;
        tc.position = {spawnPos.x, spawnPos.y, spawnPos.z};
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {def.scale, def.scale, def.scale};
        tc.flags = 1;
        registry.emplace<TransformComponent>(cubeEntities_[i], tc);
        registry.emplace<WorldTransformComponent>(cubeEntities_[i]);
        registry.emplace<MeshComponent>(cubeEntities_[i], cubeMeshId);
        registry.emplace<MaterialComponent>(cubeEntities_[i], cubeMatIds[i]);
        registry.emplace<VisibleTag>(cubeEntities_[i]);
        registry.emplace<ShadowVisibleTag>(cubeEntities_[i], ShadowVisibleTag{0xFF});

        RigidBodyComponent rb;
        rb.mass = def.mass;
        rb.type = BodyType::Dynamic;
        rb.friction = def.friction;
        rb.restitution = def.restitution;
        rb.linearDamping = 0.05f;
        rb.angularDamping = 0.05f;
        registry.emplace<RigidBodyComponent>(cubeEntities_[i], rb);

        ColliderComponent col;
        col.shape = ColliderShape::Box;
        float half = def.scale * 0.5f;
        col.halfExtents = {half, half, half};
        registry.emplace<ColliderComponent>(cubeEntities_[i], col);
    }

    // -- Light indicator ------------------------------------------------------
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
    Material lightMat{};
    lightMat.albedo = {1.0f, 0.9f, 0.3f, 1.0f};
    lightMat.emissiveScale = 5.0f;
    lightMat.roughness = 1.0f;
    uint32_t lightMatId = engine.resources().addMaterial(lightMat);

    lightIndicator_ = registry.createEntity();
    {
        TransformComponent tc{};
        tc.position = kLightDir * 12.0f;
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {0.3f, 0.3f, 0.3f};
        registry.emplace<TransformComponent>(lightIndicator_, tc);
        registry.emplace<MeshComponent>(lightIndicator_, MeshComponent{cubeMeshId});
        registry.emplace<MaterialComponent>(lightIndicator_, MaterialComponent{lightMatId});
        registry.emplace<VisibleTag>(lightIndicator_);
    }

    // -- Camera ---------------------------------------------------------------
    cam_.distance = 25.0f;
    cam_.pitch = 30.0f;
}

void PhysicsGame::onFixedUpdate(Engine& engine, Registry& registry, float fixedDt)
{
    physicsSys_.update(registry, physics_, fixedDt);
}

void PhysicsGame::onUpdate(Engine& engine, Registry& registry, float dt)
{
    const auto& input = engine.inputState();
    double mx, my;
    glfwGetCursorPos(engine.glfwHandle(), &mx, &my);

    bool imguiWants = engine.imguiWantsMouse();

    // -- Camera orbit (right-drag) and zoom (scroll) ----------------------
    if (!imguiWants)
    {
        bool rightDown =
            glfwGetMouseButton(engine.glfwHandle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (rightDown)
        {
            if (rightDragging_)
            {
                float dx = static_cast<float>(mx - prevMouseX_);
                float dy = static_cast<float>(my - prevMouseY_);
                cam_.orbit(dx, dy);
            }
            rightDragging_ = true;
        }
        else
        {
            rightDragging_ = false;
        }

        if (std::abs(s_zoomScrollDelta) > 0.01f)
        {
            cam_.zoom(s_zoomScrollDelta, 1.0f, 5.0f, 60.0f);
        }
    }

    prevMouseX_ = mx;
    prevMouseY_ = my;
    s_zoomScrollDelta = 0.f;

    // -- Update plane tilt (arrow keys) -----------------------------------
    constexpr float kTiltSpeed = 30.0f;
    constexpr float kMaxTilt = 30.0f;

    if (input.isKeyHeld(Key::Left))
        planeRoll_ += kTiltSpeed * dt;
    if (input.isKeyHeld(Key::Right))
        planeRoll_ -= kTiltSpeed * dt;
    if (input.isKeyHeld(Key::Up))
        planePitch_ -= kTiltSpeed * dt;
    if (input.isKeyHeld(Key::Down))
        planePitch_ += kTiltSpeed * dt;

    planePitch_ = glm::clamp(planePitch_, -kMaxTilt, kMaxTilt);
    planeRoll_ = glm::clamp(planeRoll_, -kMaxTilt, kMaxTilt);

    glm::quat planeRot =
        glm::quat(glm::vec3(glm::radians(planePitch_), 0.0f, glm::radians(planeRoll_)));
    {
        auto* tc = registry.get<TransformComponent>(groundEntity_);
        if (tc)
        {
            tc->rotation = planeRot;
            tc->flags |= 1;
        }
    }

    // -- Reset cubes (R key) ----------------------------------------------
    if (input.isKeyPressed(Key::R))
    {
        resetCubes(registry);
    }

    // -- Respawn cubes that fell below y=-10 ------------------------------
    respawnFallenCubes(registry);
}

void PhysicsGame::onRender(Engine& engine)
{
    double frameStart = glfwGetTime();
    engine.renderer().beginFrameDirect();

    const float fbW = static_cast<float>(engine.fbWidth());
    const float fbH = static_cast<float>(engine.fbHeight());

    glm::mat4 viewMat = cam_.view();
    glm::vec3 camPos = cam_.position();
    glm::mat4 projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 100.f);

    // Light
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
    constexpr float kLightIntens = 6.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};

    const glm::vec3 kLightPos = kLightDir * 20.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-12.f, 12.f, -12.f, 12.f, 0.1f, 50.f);

    // Shadow pass
    engine.shadow().beginCascade(0, lightView, lightProj);

    // Registry pointer was stored during onInit for use in render callbacks.
    if (registry_)
    {
        drawCallSys_.submitShadowDrawCalls(*registry_, engine.resources(), engine.shadowProgram(),
                                           0);

        // Opaque pass
        const auto W = engine.fbWidth();
        const auto H = engine.fbHeight();

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x1A1A2EFF)
            .transform(viewMat, projMat);

        const glm::mat4 shadowMat = engine.shadow().shadowMatrix(0);
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), engine.shadow().atlasTexture(), W, H, 0.05f,
            100.f,
        };
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;

        if (ibl_.isValid())
        {
            frame.iblEnabled = true;
            frame.maxMipLevels = 7.0f;
            frame.irradiance = ibl_.irradiance();
            frame.prefiltered = ibl_.prefiltered();
            frame.brdfLut = ibl_.brdfLut();
        }
        drawCallSys_.update(*registry_, engine.resources(), engine.pbrProgram(), engine.uniforms(),
                            frame);
    }

    // -- HUD --------------------------------------------------------------
    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(1, 1, 0x0f, "Physics Demo v2  |  %.1f fps  |  render %.3f ms",
                        renderMs_ > 0 ? 1000.f / renderMs_ : 0.f, renderMs_);
    bgfx::dbgTextPrintf(1, 2, 0x07, "Arrows=tilt  |  R=reset  |  RMB=orbit  |  Scroll=zoom");
    bgfx::dbgTextPrintf(1, 3, 0x07, "Arena: %zu KB / %zu KB used",
                        engine.frameArena().bytesUsed() / 1024,
                        engine.frameArena().capacity() / 1024);

    // -- ImGui panel ------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 280), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Physics Demo v2"))
    {
        ImGui::Text("Render: %.3f ms", renderMs_);
        ImGui::Text("Arena: %zu KB / %zu KB used", engine.frameArena().bytesUsed() / 1024,
                    engine.frameArena().capacity() / 1024);
        ImGui::Separator();

        ImGui::Text("Plane Tilt");
        ImGui::Text("  Pitch: %.1f deg", planePitch_);
        ImGui::Text("  Roll:  %.1f deg", planeRoll_);
        ImGui::Separator();

        ImGui::Text("Active cubes: %d", kCubeCount);
        ImGui::Separator();

        ImGui::Text("Press R to reset");
        ImGui::Separator();

        ImGui::Text("Cube Legend:");

        ImGui::ColorButton("##heavy", ImVec4(0.6f, 0.1f, 0.1f, 1.0f), ImGuiColorEditFlags_NoTooltip,
                           ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::Text("Heavy (mass 5.0, low bounce)");

        ImGui::ColorButton("##medium", ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::Text("Medium (mass 1.0)");

        ImGui::ColorButton("##light", ImVec4(1.0f, 1.0f, 0.3f, 1.0f), ImGuiColorEditFlags_NoTooltip,
                           ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::Text("Light (mass 0.2)");

        ImGui::ColorButton("##bouncy", ImVec4(0.2f, 0.9f, 1.0f, 1.0f),
                           ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::Text("Bouncy (restitution 0.9)");
    }
    ImGui::End();

    double frameEnd = glfwGetTime();
    renderMs_ = static_cast<float>((frameEnd - frameStart) * 1000.0);
}

void PhysicsGame::onShutdown(Engine& engine, Registry& registry)
{
    ibl_.shutdown();
    physics_.shutdown();
}

void PhysicsGame::resetCubes(Registry& registry)
{
    for (int i = 0; i < kCubeCount; i++)
    {
        auto* rb = registry.get<RigidBodyComponent>(cubeEntities_[i]);
        if (rb && rb->bodyID != ~0u)
        {
            glm::vec3 spawnPos = randomSpawnPos();
            physics_.setBodyPosition(rb->bodyID, {spawnPos.x, spawnPos.y, spawnPos.z});
            physics_.setBodyRotation(rb->bodyID, {1.0f, 0.0f, 0.0f, 0.0f});
            physics_.setLinearVelocity(rb->bodyID, {0.0f, 0.0f, 0.0f});
            physics_.setAngularVelocity(rb->bodyID, {0.0f, 0.0f, 0.0f});
        }
    }
}

void PhysicsGame::respawnFallenCubes(Registry& registry)
{
    for (int i = 0; i < kCubeCount; i++)
    {
        auto* tc = registry.get<TransformComponent>(cubeEntities_[i]);
        auto* rb = registry.get<RigidBodyComponent>(cubeEntities_[i]);
        if (tc && rb && rb->bodyID != ~0u && tc->position.y < -10.0f)
        {
            glm::vec3 spawnPos = randomSpawnPos();
            physics_.setBodyPosition(rb->bodyID, {spawnPos.x, spawnPos.y, spawnPos.z});
            physics_.setBodyRotation(rb->bodyID, {1.0f, 0.0f, 0.0f, 0.0f});
            physics_.setLinearVelocity(rb->bodyID, {0.0f, 0.0f, 0.0f});
            physics_.setAngularVelocity(rb->bodyID, {0.0f, 0.0f, 0.0f});
        }
    }
}
