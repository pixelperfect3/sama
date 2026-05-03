// Hierarchy Demo -- macOS
//
// Demonstrates the scene graph with 9 cubes arranged in a tree hierarchy.
// The user can click and drag cubes; children follow their parent.
//
// Controls:
//   Left click + drag   -- pick and move a cube (children follow)
//   Right click + drag  -- orbit camera around origin
//   Scroll              -- zoom in/out
//
// Tree structure:
//   Root (center)
//   +- Child1 (left)
//   |  +- Node3   +- Node4   +- Node5
//   +- Child2 (right)
//      +- Node6   +- Node7   +- Node8

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Engine.h"
#include "engine/core/OrbitCamera.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/IblResources.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/SceneGraph.h"
#include "engine/scene/TransformSystem.h"
#include "engine/ui/DebugHud.h"
#include "imgui.h"

using namespace engine::core;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::platform;
using namespace engine::rendering;

// =============================================================================
// Ray-AABB intersection (slab method)
// =============================================================================

static bool rayAABB(glm::vec3 origin, glm::vec3 dir, glm::vec3 bmin, glm::vec3 bmax, float& tOut)
{
    float tmin = 0.0f;
    float tmax = 1e30f;
    for (int i = 0; i < 3; i++)
    {
        if (std::abs(dir[i]) < 1e-8f)
        {
            if (origin[i] < bmin[i] || origin[i] > bmax[i])
                return false;
        }
        else
        {
            float t1 = (bmin[i] - origin[i]) / dir[i];
            float t2 = (bmax[i] - origin[i]) / dir[i];
            if (t1 > t2)
                std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
                return false;
        }
    }
    tOut = tmin;
    return true;
}

// =============================================================================
// Unproject mouse position to a world-space point on the near plane
// =============================================================================

static glm::vec3 screenToWorld(float mx, float my, float fbW, float fbH, const glm::mat4& view,
                               const glm::mat4& proj)
{
    float ndcX = (2.0f * mx / fbW) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / fbH);
    glm::vec4 clipNear = glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 worldNear = invVP * clipNear;
    worldNear /= worldNear.w;
    return glm::vec3(worldNear);
}

// =============================================================================
// Compute world-space AABB by transforming all 8 corners of a local AABB
// =============================================================================

static void worldAABB(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& world,
                      glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);
    for (int i = 0; i < 8; i++)
    {
        glm::vec3 corner((i & 1) ? localMax.x : localMin.x, (i & 2) ? localMax.y : localMin.y,
                         (i & 4) ? localMax.z : localMin.z);
        glm::vec3 w = glm::vec3(world * glm::vec4(corner, 1.0f));
        outMin = glm::min(outMin, w);
        outMax = glm::max(outMax, w);
    }
}

// =============================================================================
// Node info for the 9-cube hierarchy
// =============================================================================

struct NodeInfo
{
    const char* name;
    glm::vec3 localPos;
    float scale;
    int parentIdx;
    int colorLevel;
};

static constexpr int kNodeCount = 9;

// clang-format off
static const NodeInfo kNodes[kNodeCount] = {
    {"Root",    {0.0f,  2.0f,  0.0f}, 0.5f, -1, 0},
    {"Child 1", {-3.0f, -2.0f, 0.0f}, 0.4f,  0, 1},
    {"Child 2", {3.0f,  -2.0f, 0.0f}, 0.4f,  0, 1},
    {"Node 3",  {-2.0f, -2.0f, 0.0f}, 0.3f,  1, 2},
    {"Node 4",  {0.0f,  -2.0f, 0.0f}, 0.3f,  1, 2},
    {"Node 5",  {2.0f,  -2.0f, 0.0f}, 0.3f,  1, 2},
    {"Node 6",  {-2.0f, -2.0f, 0.0f}, 0.3f,  2, 2},
    {"Node 7",  {0.0f,  -2.0f, 0.0f}, 0.3f,  2, 2},
    {"Node 8",  {2.0f,  -2.0f, 0.0f}, 0.3f,  2, 2},
};
// clang-format on

static const glm::vec4 kLevelColors[3] = {
    {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 0.6f, 0.2f, 1.0f},
    {0.2f, 0.8f, 1.0f, 1.0f},
};
static const glm::vec4 kSelectedColor = {1.0f, 1.0f, 0.2f, 1.0f};

// =============================================================================
// Entry point
// =============================================================================

static float s_zoomScrollDelta = 0.f;

int main()
{
    Engine eng;
    EngineDesc desc;
    desc.windowTitle = "Hierarchy Demo";
    if (!eng.init(desc))
        return 1;

    // Hook up zoom scroll (piggybacks on Engine's scroll callback via user pointer).
    // We override the scroll callback to also track zoom delta.
    glfwSetScrollCallback(eng.glfwHandle(),
                          [](GLFWwindow* win, double /*xoff*/, double yoff)
                          {
                              auto* e = static_cast<Engine*>(glfwGetWindowUserPointer(win));
                              if (e)
                                  e->imguiScrollAccum() += static_cast<float>(yoff);
                              s_zoomScrollDelta += static_cast<float>(yoff);
                          });

    // -- IBL (procedural sky/ground) -----------------------------------------
    IblResources ibl;
    ibl.generateDefault();

    // -- ECS & rendering systems ------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;

    // -- Create cube mesh -------------------------------------------------
    MeshData cubeData = makeCubeMeshData();
    Mesh cubeMesh = buildMesh(cubeData);
    uint32_t cubeMeshId = eng.resources().addMesh(std::move(cubeMesh));

    // -- Create materials -------------------------------------------------
    uint32_t materialIds[kNodeCount];
    uint32_t yellowMatIds[kNodeCount];
    for (int i = 0; i < kNodeCount; i++)
    {
        Material mat;
        mat.albedo = kLevelColors[kNodes[i].colorLevel];
        mat.roughness = 0.5f;
        mat.metallic = 0.0f;
        materialIds[i] = eng.resources().addMaterial(mat);

        Material ymat;
        ymat.albedo = kSelectedColor;
        ymat.roughness = 0.5f;
        ymat.metallic = 0.0f;
        yellowMatIds[i] = eng.resources().addMaterial(ymat);
    }

    // -- Create entities with hierarchy -----------------------------------
    EntityID entities[kNodeCount];
    for (int i = 0; i < kNodeCount; i++)
    {
        entities[i] = reg.createEntity();

        TransformComponent tc;
        tc.position = kNodes[i].localPos;
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {kNodes[i].scale, kNodes[i].scale, kNodes[i].scale};
        tc.flags = 1;
        reg.emplace<TransformComponent>(entities[i], tc);
        reg.emplace<WorldTransformComponent>(entities[i]);
        reg.emplace<MeshComponent>(entities[i], cubeMeshId);
        reg.emplace<MaterialComponent>(entities[i], materialIds[i]);
        reg.emplace<VisibleTag>(entities[i]);
    }
    for (int i = 0; i < kNodeCount; i++)
    {
        if (kNodes[i].parentIdx >= 0)
            engine::scene::setParent(reg, entities[i], entities[kNodes[i].parentIdx]);
    }

    // -- Light ------------------------------------------------------------
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
    constexpr float kLightIntens = 6.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};
    const glm::vec3 kLightPos = kLightDir * 20.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-12.f, 12.f, -8.f, 8.f, 0.1f, 50.f);

    // -- Camera and interaction state -------------------------------------
    engine::core::OrbitCamera cam;
    cam.distance = 15.0f;
    int selectedIdx = -1;
    bool dragging = false;
    glm::vec3 dragOffset = {0, 0, 0};
    bool rightDragging = false;
    double prevMouseX = 0.0, prevMouseY = 0.0;

    // -- Recursive ImGui hierarchy drawer ---------------------------------
    std::function<void(int)> drawHierarchyNode = [&](int idx)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
        bool hasChildren = false;
        for (int c = 0; c < kNodeCount; c++)
        {
            if (kNodes[c].parentIdx == idx)
            {
                hasChildren = true;
                break;
            }
        }
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (idx == selectedIdx)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx(kNodes[idx].name, flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            selectedIdx = idx;

        if (open)
        {
            for (int c = 0; c < kNodeCount; c++)
            {
                if (kNodes[c].parentIdx == idx)
                    drawHierarchyNode(c);
            }
            ImGui::TreePop();
        }
    };

    engine::ui::DebugHud hud;
    hud.init();

    // -- Main loop --------------------------------------------------------
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
        float physMx = static_cast<float>(mx * eng.contentScaleX());
        float physMy = static_cast<float>(my * eng.contentScaleY());

        bool imguiWants = eng.imguiWantsMouse();

        // -- Camera matrices ----------------------------------------------
        glm::mat4 viewMat = cam.view();
        glm::mat4 projMat = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 100.f);
        glm::vec3 camPos = cam.position();

        // -- Left click: picking / dragging -------------------------------
        if (!imguiWants)
        {
            bool leftDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool leftJustPressed = input.isMouseButtonPressed(MouseButton::Left);

            if (leftJustPressed)
            {
                glm::vec3 nearPt = screenToWorld(physMx, physMy, fbW, fbH, viewMat, projMat);
                glm::vec3 rayDir = glm::normalize(nearPt - camPos);

                float bestT = 1e30f;
                int bestIdx = -1;

                for (int i = 0; i < kNodeCount; i++)
                {
                    auto* wtc = reg.get<WorldTransformComponent>(entities[i]);
                    if (!wtc)
                        continue;
                    const Mesh* mesh = eng.resources().getMesh(cubeMeshId);
                    if (!mesh)
                        continue;

                    glm::vec3 wMin, wMax;
                    glm::vec3 lMin = {mesh->boundsMin.x, mesh->boundsMin.y, mesh->boundsMin.z};
                    glm::vec3 lMax = {mesh->boundsMax.x, mesh->boundsMax.y, mesh->boundsMax.z};
                    worldAABB(lMin, lMax, glm::make_mat4(glm::value_ptr(wtc->matrix)), wMin, wMax);

                    float t;
                    if (rayAABB(camPos, rayDir, wMin, wMax, t) && t < bestT)
                    {
                        bestT = t;
                        bestIdx = i;
                    }
                }

                if (selectedIdx >= 0 && selectedIdx != bestIdx)
                {
                    auto* mc = reg.get<MaterialComponent>(entities[selectedIdx]);
                    if (mc)
                        mc->material = materialIds[selectedIdx];
                }

                selectedIdx = bestIdx;

                if (selectedIdx >= 0)
                {
                    auto* mc = reg.get<MaterialComponent>(entities[selectedIdx]);
                    if (mc)
                        mc->material = yellowMatIds[selectedIdx];

                    auto* wtc = reg.get<WorldTransformComponent>(entities[selectedIdx]);
                    glm::vec3 cubeWorldPos =
                        glm::vec3(wtc->matrix[3][0], wtc->matrix[3][1], wtc->matrix[3][2]);
                    glm::vec3 nearPt2 = screenToWorld(physMx, physMy, fbW, fbH, viewMat, projMat);
                    glm::vec3 rayDir2 = glm::normalize(nearPt2 - camPos);
                    glm::vec3 planeNormal = glm::normalize(camPos - cubeWorldPos);
                    float denom = glm::dot(planeNormal, rayDir2);
                    if (std::abs(denom) > 1e-6f)
                    {
                        float t = glm::dot(cubeWorldPos - camPos, planeNormal) / denom;
                        glm::vec3 hitPoint = camPos + rayDir2 * t;
                        dragOffset = cubeWorldPos - hitPoint;
                    }
                    dragging = true;
                }
                else
                {
                    dragging = false;
                }
            }
            else if (leftDown && dragging && selectedIdx >= 0)
            {
                auto* wtc = reg.get<WorldTransformComponent>(entities[selectedIdx]);
                glm::vec3 cubeWorldPos =
                    glm::vec3(wtc->matrix[3][0], wtc->matrix[3][1], wtc->matrix[3][2]);

                glm::vec3 nearPt2 = screenToWorld(physMx, physMy, fbW, fbH, viewMat, projMat);
                glm::vec3 rayDir2 = glm::normalize(nearPt2 - camPos);
                glm::vec3 planeNormal = glm::normalize(camPos - cubeWorldPos);
                float denom = glm::dot(planeNormal, rayDir2);
                if (std::abs(denom) > 1e-6f)
                {
                    float t = glm::dot(cubeWorldPos - camPos, planeNormal) / denom;
                    glm::vec3 hitPoint = camPos + rayDir2 * t;
                    glm::vec3 newWorldPos = hitPoint + dragOffset;

                    glm::vec3 localPos;
                    EntityID parentEntity = engine::scene::getParent(reg, entities[selectedIdx]);
                    if (parentEntity != INVALID_ENTITY)
                    {
                        auto* parentWtc = reg.get<WorldTransformComponent>(parentEntity);
                        if (parentWtc)
                        {
                            glm::mat4 invParent = glm::inverse(parentWtc->matrix);
                            glm::vec4 lp = invParent * glm::vec4(newWorldPos, 1.0f);
                            localPos = glm::vec3(lp);
                        }
                        else
                        {
                            localPos = newWorldPos;
                        }
                    }
                    else
                    {
                        localPos = newWorldPos;
                    }

                    auto* tc = reg.get<TransformComponent>(entities[selectedIdx]);
                    if (tc)
                    {
                        tc->position = localPos;
                        tc->flags |= 1;
                    }
                }
            }
            else if (!leftDown)
            {
                dragging = false;
            }

            // -- Right click: orbit camera --------------------------------
            bool rightDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
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

            // -- Scroll: zoom ---------------------------------------------
            if (std::abs(s_zoomScrollDelta) > 0.01f)
            {
                cam.zoom(s_zoomScrollDelta, 1.0f, 3.0f, 50.0f);
            }
        }

        prevMouseX = mx;
        prevMouseY = my;
        s_zoomScrollDelta = 0.f;

        // -- Update material highlighting ---------------------------------
        for (int i = 0; i < kNodeCount; i++)
        {
            auto* mc = reg.get<MaterialComponent>(entities[i]);
            if (!mc)
                continue;
            mc->material = (i == selectedIdx) ? yellowMatIds[i] : materialIds[i];
        }

        // -- Transform system ---------------------------------------------
        transformSys.update(reg);

        // -- Render -------------------------------------------------------
        eng.renderer().beginFrameDirect();

        viewMat = cam.view();
        camPos = cam.position();

        // Shadow pass
        eng.shadow().beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, eng.resources(),
                                          bgfx::ProgramHandle{eng.shadowProgram().idx}, 0);

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

        // -- HUD ----------------------------------------------------------
        hud.begin(eng.fbWidth(), eng.fbHeight());
        hud.printf(1, 1, "Hierarchy Demo  |  %.1f fps  |  %.3f ms", dt > 0 ? 1.f / dt : 0.f,
                   dt * 1000.f);
        hud.printf(1, 2, "LMB=pick/drag  |  RMB=orbit  |  Scroll=zoom");
        hud.end();

        // -- ImGui hierarchy panel ----------------------------------------
        ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene Hierarchy"))
        {
            for (int i = 0; i < kNodeCount; i++)
            {
                if (kNodes[i].parentIdx == -1)
                    drawHierarchyNode(i);
            }

            ImGui::Separator();

            if (selectedIdx >= 0)
            {
                ImGui::Text("Selected: %s", kNodes[selectedIdx].name);
                auto* tc = reg.get<TransformComponent>(entities[selectedIdx]);
                if (tc)
                {
                    ImGui::Text("Local Pos: (%.2f, %.2f, %.2f)", tc->position.x, tc->position.y,
                                tc->position.z);
                }
                auto* wtc = reg.get<WorldTransformComponent>(entities[selectedIdx]);
                if (wtc)
                {
                    ImGui::Text("World Pos: (%.2f, %.2f, %.2f)", wtc->matrix[3][0],
                                wtc->matrix[3][1], wtc->matrix[3][2]);
                }
            }
            else
            {
                ImGui::Text("No node selected");
            }
        }
        ImGui::End();

        eng.endFrame();
    }

    hud.shutdown();
    ibl.shutdown();

    return 0;
}
