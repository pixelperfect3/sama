// Physics Demo — macOS
//
// Demonstrates Jolt physics integration.  Multiple colored cubes fall onto a
// tilting plane that the user controls with arrow keys.
//
// Controls:
//   Arrow keys          — tilt the ground plane (Left/Right=roll, Up/Down=pitch)
//   R                   — reset all cubes to random spawn positions
//   Right click + drag  — orbit camera
//   Scroll              — zoom

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <random>

#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/IPhysicsEngine.h"
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsSystem.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/TransformSystem.h"
#include "imgui.h"

using namespace engine::core;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::physics;
using namespace engine::platform;
using namespace engine::rendering;

// =============================================================================
// Orbit camera
// =============================================================================

struct OrbitCamera
{
    float distance = 25.0f;
    float yaw = 0.0f;
    float pitch = 30.0f;
    glm::vec3 target = {0, 0, 0};

    [[nodiscard]] glm::vec3 position() const
    {
        float r = glm::radians(pitch);
        float y = glm::radians(yaw);
        return target + glm::vec3(distance * std::cos(r) * std::sin(y), distance * std::sin(r),
                                  distance * std::cos(r) * std::cos(y));
    }

    [[nodiscard]] glm::mat4 view() const
    {
        return glm::lookAt(position(), target, glm::vec3(0, 1, 0));
    }
};

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

static constexpr int kCubeCount = sizeof(kCubeDefs) / sizeof(kCubeDefs[0]);

// =============================================================================
// Random helpers
// =============================================================================

static std::mt19937 s_rng(42);

static float randomFloat(float lo, float hi)
{
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(s_rng);
}

static glm::vec3 randomSpawnPos()
{
    return {randomFloat(-5.0f, 5.0f), randomFloat(8.0f, 15.0f), randomFloat(-5.0f, 5.0f)};
}

// =============================================================================
// Entry point
// =============================================================================

static float s_zoomScrollDelta = 0.f;

int main()
{
    Engine eng;
    EngineDesc desc;
    desc.windowTitle = "Physics Demo";
    if (!eng.init(desc))
        return 1;

    // Hook up zoom scroll (piggybacks on Engine's scroll callback via user pointer).
    glfwSetScrollCallback(eng.glfwHandle(),
                          [](GLFWwindow* win, double /*xoff*/, double yoff)
                          {
                              auto* e = static_cast<Engine*>(glfwGetWindowUserPointer(win));
                              if (e)
                                  e->imguiScrollAccum() += static_cast<float>(yoff);
                              s_zoomScrollDelta += static_cast<float>(yoff);
                          });

    // -- Physics engine -------------------------------------------------------
    JoltPhysicsEngine physics;
    if (!physics.init())
    {
        fprintf(stderr, "physics_demo: failed to initialize Jolt physics\n");
        return 1;
    }
    PhysicsSystem physicsSys;

    // -- ECS ------------------------------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;

    // -- Create cube mesh (shared by all entities) ----------------------------
    MeshData cubeData = makeCubeMeshData();
    Mesh cubeMesh = buildMesh(cubeData);
    uint32_t cubeMeshId = eng.resources().addMesh(std::move(cubeMesh));

    // -- Create materials -----------------------------------------------------
    // Ground plane material
    Material groundMat;
    groundMat.albedo = {0.3f, 0.3f, 0.3f, 1.0f};
    groundMat.roughness = 0.7f;
    groundMat.metallic = 0.0f;
    uint32_t groundMatId = eng.resources().addMaterial(groundMat);

    // Per-cube materials
    uint32_t cubeMatIds[kCubeCount];
    for (int i = 0; i < kCubeCount; i++)
    {
        Material mat;
        mat.albedo = kCubeDefs[i].color;
        mat.roughness = 0.5f;
        mat.metallic = 0.0f;
        cubeMatIds[i] = eng.resources().addMaterial(mat);
    }

    // -- Create ground plane entity -------------------------------------------
    EntityID groundEntity = reg.createEntity();
    {
        TransformComponent tc;
        tc.position = {0.0f, 0.0f, 0.0f};
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {10.0f, 0.2f, 10.0f};
        tc.flags = 1;  // dirty
        reg.emplace<TransformComponent>(groundEntity, tc);
        reg.emplace<WorldTransformComponent>(groundEntity);
        reg.emplace<MeshComponent>(groundEntity, cubeMeshId);
        reg.emplace<MaterialComponent>(groundEntity, groundMatId);
        reg.emplace<VisibleTag>(groundEntity);
        reg.emplace<ShadowVisibleTag>(groundEntity, ShadowVisibleTag{0xFF});

        RigidBodyComponent rb;
        rb.mass = 0.0f;
        rb.type = BodyType::Kinematic;
        rb.friction = 0.8f;
        rb.restitution = 0.2f;
        reg.emplace<RigidBodyComponent>(groundEntity, rb);

        ColliderComponent col;
        col.shape = ColliderShape::Box;
        col.halfExtents = {5.0f, 0.1f, 5.0f};  // half of (10, 0.2, 10)
        reg.emplace<ColliderComponent>(groundEntity, col);
    }

    // -- Create dynamic cubes -------------------------------------------------
    EntityID cubeEntities[kCubeCount];
    for (int i = 0; i < kCubeCount; i++)
    {
        const CubeDef& def = kCubeDefs[i];
        cubeEntities[i] = reg.createEntity();

        glm::vec3 spawnPos = randomSpawnPos();

        TransformComponent tc;
        tc.position = {spawnPos.x, spawnPos.y, spawnPos.z};
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {def.scale, def.scale, def.scale};
        tc.flags = 1;
        reg.emplace<TransformComponent>(cubeEntities[i], tc);
        reg.emplace<WorldTransformComponent>(cubeEntities[i]);
        reg.emplace<MeshComponent>(cubeEntities[i], cubeMeshId);
        reg.emplace<MaterialComponent>(cubeEntities[i], cubeMatIds[i]);
        reg.emplace<VisibleTag>(cubeEntities[i]);
        reg.emplace<ShadowVisibleTag>(cubeEntities[i], ShadowVisibleTag{0xFF});

        RigidBodyComponent rb;
        rb.mass = def.mass;
        rb.type = BodyType::Dynamic;
        rb.friction = def.friction;
        rb.restitution = def.restitution;
        rb.linearDamping = 0.05f;
        rb.angularDamping = 0.05f;
        reg.emplace<RigidBodyComponent>(cubeEntities[i], rb);

        ColliderComponent col;
        col.shape = ColliderShape::Box;
        float half = def.scale * 0.5f;
        col.halfExtents = {half, half, half};
        reg.emplace<ColliderComponent>(cubeEntities[i], col);
    }

    // -- Light ----------------------------------------------------------------
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
    constexpr float kLightIntens = 6.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};

    const glm::vec3 kLightPos = kLightDir * 20.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-12.f, 12.f, -12.f, 12.f, 0.1f, 50.f);

    // -- Light indicator (small bright cube showing the light position) --------
    Material lightMat{};
    lightMat.albedo = {1.0f, 0.9f, 0.3f, 1.0f};
    lightMat.emissiveScale = 5.0f;
    lightMat.roughness = 1.0f;
    uint32_t lightMatId = eng.resources().addMaterial(lightMat);

    EntityID lightIndicator = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = kLightDir * 12.0f;
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {0.3f, 0.3f, 0.3f};
        reg.emplace<TransformComponent>(lightIndicator, tc);
        reg.emplace<MeshComponent>(lightIndicator, MeshComponent{cubeMeshId});
        reg.emplace<MaterialComponent>(lightIndicator, MaterialComponent{lightMatId});
        reg.emplace<VisibleTag>(lightIndicator);
    }

    // -- Camera and interaction state -----------------------------------------
    OrbitCamera cam;
    bool rightDragging = false;
    double prevMouseX = 0.0, prevMouseY = 0.0;

    // Ground plane tilt state
    float planePitch = 0.0f;             // rotation around X axis, degrees
    float planeRoll = 0.0f;              // rotation around Z axis, degrees
    constexpr float kTiltSpeed = 30.0f;  // degrees per second
    constexpr float kMaxTilt = 30.0f;    // degrees

    float renderMs = 0.f;

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

        // -- Camera orbit (right-drag) and zoom (scroll) ----------------------
        if (!imguiWants)
        {
            bool rightDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            if (rightDown)
            {
                if (rightDragging)
                {
                    double dx = mx - prevMouseX;
                    double dy = my - prevMouseY;
                    cam.yaw += static_cast<float>(dx) * 0.3f;
                    cam.pitch += static_cast<float>(dy) * 0.3f;
                    cam.pitch = glm::clamp(cam.pitch, -89.0f, 89.0f);
                }
                rightDragging = true;
            }
            else
            {
                rightDragging = false;
            }

            if (std::abs(s_zoomScrollDelta) > 0.01f)
            {
                cam.distance -= s_zoomScrollDelta * 1.0f;
                cam.distance = glm::clamp(cam.distance, 5.0f, 60.0f);
            }
        }

        prevMouseX = mx;
        prevMouseY = my;
        s_zoomScrollDelta = 0.f;

        // -- Update plane tilt (arrow keys) -----------------------------------
        if (input.isKeyHeld(Key::Left))
            planeRoll += kTiltSpeed * dt;
        if (input.isKeyHeld(Key::Right))
            planeRoll -= kTiltSpeed * dt;
        if (input.isKeyHeld(Key::Up))
            planePitch -= kTiltSpeed * dt;
        if (input.isKeyHeld(Key::Down))
            planePitch += kTiltSpeed * dt;

        planePitch = glm::clamp(planePitch, -kMaxTilt, kMaxTilt);
        planeRoll = glm::clamp(planeRoll, -kMaxTilt, kMaxTilt);

        // Build rotation quaternion from euler angles
        glm::quat planeRot =
            glm::quat(glm::vec3(glm::radians(planePitch), 0.0f, glm::radians(planeRoll)));
        {
            auto* tc = reg.get<TransformComponent>(groundEntity);
            if (tc)
            {
                tc->rotation = planeRot;
                tc->flags |= 1;  // mark dirty
            }
        }

        // -- Reset cubes (R key) ----------------------------------------------
        if (input.isKeyPressed(Key::R))
        {
            for (int i = 0; i < kCubeCount; i++)
            {
                auto* rb = reg.get<RigidBodyComponent>(cubeEntities[i]);
                if (rb && rb->bodyID != ~0u)
                {
                    glm::vec3 spawnPos = randomSpawnPos();
                    physics.setBodyPosition(rb->bodyID, {spawnPos.x, spawnPos.y, spawnPos.z});
                    physics.setBodyRotation(rb->bodyID, {1.0f, 0.0f, 0.0f, 0.0f});
                    physics.setLinearVelocity(rb->bodyID, {0.0f, 0.0f, 0.0f});
                    physics.setAngularVelocity(rb->bodyID, {0.0f, 0.0f, 0.0f});
                }
            }
        }

        // -- Respawn cubes that fell below y=-10 ------------------------------
        for (int i = 0; i < kCubeCount; i++)
        {
            auto* tc = reg.get<TransformComponent>(cubeEntities[i]);
            auto* rb = reg.get<RigidBodyComponent>(cubeEntities[i]);
            if (tc && rb && rb->bodyID != ~0u && tc->position.y < -10.0f)
            {
                glm::vec3 spawnPos = randomSpawnPos();
                physics.setBodyPosition(rb->bodyID, {spawnPos.x, spawnPos.y, spawnPos.z});
                physics.setBodyRotation(rb->bodyID, {1.0f, 0.0f, 0.0f, 0.0f});
                physics.setLinearVelocity(rb->bodyID, {0.0f, 0.0f, 0.0f});
                physics.setAngularVelocity(rb->bodyID, {0.0f, 0.0f, 0.0f});
            }
        }

        // -- Physics step -----------------------------------------------------
        physicsSys.update(reg, physics, dt);

        // -- Transform system -------------------------------------------------
        transformSys.update(reg);

        // -- Render -----------------------------------------------------------
        double frameStart = glfwGetTime();
        eng.renderer().beginFrameDirect();

        glm::mat4 viewMat = cam.view();
        glm::vec3 camPos = cam.position();
        glm::mat4 projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 100.f);

        // Shadow pass
        eng.shadow().beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, eng.resources(), eng.shadowProgram(), 0);

        // Opaque pass
        const auto W = eng.fbWidth();
        const auto H = eng.fbHeight();

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x1A1A2EFF)
            .transform(viewMat, projMat);

        const glm::mat4 shadowMat = eng.shadow().shadowMatrix(0);
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), eng.shadow().atlasTexture(), W, H, 0.05f, 100.f,
        };
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;
        drawCallSys.update(reg, eng.resources(), eng.pbrProgram(), eng.uniforms(), frame);

        // -- HUD --------------------------------------------------------------
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(1, 1, 0x0f, "Physics Demo  |  %.1f fps  |  %.3f ms  |  render %.3f ms",
                            dt > 0 ? 1.f / dt : 0.f, dt * 1000.f, renderMs);
        bgfx::dbgTextPrintf(1, 2, 0x07, "Arrows=tilt  |  R=reset  |  RMB=orbit  |  Scroll=zoom");
        bgfx::dbgTextPrintf(1, 3, 0x07, "Arena: %zu KB / %zu KB used",
                            eng.frameArena().bytesUsed() / 1024,
                            eng.frameArena().capacity() / 1024);

        // -- ImGui panel ------------------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(260, 280), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Physics Demo"))
        {
            ImGui::Text("FPS: %.1f  |  Render: %.3f ms", dt > 0 ? 1.f / dt : 0.f, renderMs);
            ImGui::Text("Arena: %zu KB / %zu KB used", eng.frameArena().bytesUsed() / 1024,
                        eng.frameArena().capacity() / 1024);
            ImGui::Separator();

            ImGui::Text("Plane Tilt");
            ImGui::Text("  Pitch: %.1f deg", planePitch);
            ImGui::Text("  Roll:  %.1f deg", planeRoll);
            ImGui::Separator();

            ImGui::Text("Active cubes: %d", kCubeCount);
            ImGui::Separator();

            ImGui::Text("Press R to reset");
            ImGui::Separator();

            ImGui::Text("Cube Legend:");

            // Heavy — dark red swatch
            ImGui::ColorButton("##heavy", ImVec4(0.6f, 0.1f, 0.1f, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
            ImGui::SameLine();
            ImGui::Text("Heavy (mass 5.0, low bounce)");

            // Medium — orange swatch
            ImGui::ColorButton("##medium", ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
            ImGui::SameLine();
            ImGui::Text("Medium (mass 1.0)");

            // Light — yellow swatch
            ImGui::ColorButton("##light", ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
            ImGui::SameLine();
            ImGui::Text("Light (mass 0.2)");

            // Bouncy — cyan swatch
            ImGui::ColorButton("##bouncy", ImVec4(0.2f, 0.9f, 1.0f, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
            ImGui::SameLine();
            ImGui::Text("Bouncy (restitution 0.9)");
        }
        ImGui::End();

        eng.endFrame();

        double frameEnd = glfwGetTime();
        renderMs = static_cast<float>((frameEnd - frameStart) * 1000.0);
    }

    // -- Cleanup --------------------------------------------------------------
    physics.shutdown();

    return 0;
}
