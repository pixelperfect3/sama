// Hierarchy Demo — macOS
//
// Demonstrates the scene graph with 9 cubes arranged in a tree hierarchy.
// The user can click and drag cubes; children follow their parent.
//
// Controls:
//   Left click + drag   — pick and move a cube (children follow)
//   Right click + drag  — orbit camera around origin
//   Scroll              — zoom in/out
//
// Tree structure:
//   Root (center)
//   ├─ Child1 (left)
//   │  ├─ Node3   ├─ Node4   └─ Node5
//   └─ Child2 (right)
//      ├─ Node6   ├─ Node7   └─ Node8

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/ecs/Registry.h"
#include "engine/input/InputState.h"
#include "engine/input/InputSystem.h"
#include "engine/input/Key.h"
#include "engine/input/desktop/GlfwInputBackend.h"
#include "engine/platform/Window.h"
#include "engine/platform/desktop/GlfwWindow.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShadowRenderer.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/SceneGraph.h"
#include "engine/scene/TransformSystem.h"
#include "imgui.h"

using namespace engine::ecs;
using namespace engine::input;
using namespace engine::platform;
using namespace engine::rendering;

// =============================================================================
// Orbit camera
// =============================================================================

struct OrbitCamera
{
    float distance = 15.0f;
    float yaw = 0.0f;
    float pitch = 20.0f;  // degrees, looking slightly down
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
    float ndcY = 1.0f - (2.0f * my / fbH);                   // flip Y
    glm::vec4 clipNear = glm::vec4(ndcX, ndcY, 0.0f, 1.0f);  // bgfx uses [0,1] depth
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
    int parentIdx;   // -1 = root (no parent)
    int colorLevel;  // 0=white, 1=orange, 2=cyan
};

static constexpr int kNodeCount = 9;

// clang-format off
static const NodeInfo kNodes[kNodeCount] = {
    {"Root",    {0.0f,  2.0f,  0.0f}, 0.5f, -1, 0},  // 0
    {"Child 1", {-3.0f, -2.0f, 0.0f}, 0.4f,  0, 1},  // 1
    {"Child 2", {3.0f,  -2.0f, 0.0f}, 0.4f,  0, 1},  // 2
    {"Node 3",  {-2.0f, -2.0f, 0.0f}, 0.3f,  1, 2},  // 3
    {"Node 4",  {0.0f,  -2.0f, 0.0f}, 0.3f,  1, 2},  // 4
    {"Node 5",  {2.0f,  -2.0f, 0.0f}, 0.3f,  1, 2},  // 5
    {"Node 6",  {-2.0f, -2.0f, 0.0f}, 0.3f,  2, 2},  // 6
    {"Node 7",  {0.0f,  -2.0f, 0.0f}, 0.3f,  2, 2},  // 7
    {"Node 8",  {2.0f,  -2.0f, 0.0f}, 0.3f,  2, 2},  // 8
};
// clang-format on

// Level colors: white, orange, cyan
static const glm::vec4 kLevelColors[3] = {
    {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 0.6f, 0.2f, 1.0f},
    {0.2f, 0.8f, 1.0f, 1.0f},
};
static const glm::vec4 kSelectedColor = {1.0f, 1.0f, 0.2f, 1.0f};

// =============================================================================
// Entry point
// =============================================================================

static float s_imguiScrollF = 0.f;
static float s_zoomScrollDelta = 0.f;

int main()
{
    constexpr uint32_t kInitW = 1280;
    constexpr uint32_t kInitH = 720;

    // -- Window ---------------------------------------------------------------
    auto window = createWindow({kInitW, kInitH, "Hierarchy Demo"});
    if (!window)
        return 1;

    auto* glfwWin = static_cast<GlfwWindow*>(window.get());
    GLFWwindow* glfwHandle = glfwWin->glfwHandle();

    // -- Renderer -------------------------------------------------------------
    Renderer renderer;
    {
        RendererDesc rd;
        rd.nativeWindowHandle = window->nativeWindowHandle();
        rd.nativeDisplayHandle = window->nativeDisplayHandle();
        rd.width = kInitW;
        rd.height = kInitH;
        rd.headless = false;
        if (!renderer.init(rd))
            return 1;
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);

    // -- GPU resources --------------------------------------------------------
    bgfx::ProgramHandle shadowProg = loadShadowProgram();
    bgfx::ProgramHandle pbrProg = loadPbrProgram();

    RenderResources res;

    const uint8_t kWhite[4] = {255, 255, 255, 255};
    bgfx::TextureHandle whiteTex =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                              bgfx::copy(kWhite, sizeof(kWhite)));
    res.setWhiteTexture(whiteTex);

    const uint8_t kNeutralNormal[4] = {128, 128, 255, 255};
    bgfx::TextureHandle neutralNormalTex =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                              bgfx::copy(kNeutralNormal, sizeof(kNeutralNormal)));
    res.setNeutralNormalTexture(neutralNormalTex);

    uint8_t cubeFaces[6 * 4];
    for (int i = 0; i < 6 * 4; ++i)
        cubeFaces[i] = 255;
    bgfx::TextureHandle whiteCubeTex =
        bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                bgfx::copy(cubeFaces, sizeof(cubeFaces)));
    res.setWhiteCubeTexture(whiteCubeTex);

    ShadowRenderer shadow;
    {
        ShadowDesc sd;
        sd.resolution = 2048;
        sd.cascadeCount = 1;
        shadow.init(sd);
    }

    // -- Create cube mesh (shared by all 9 entities) --------------------------
    MeshData cubeData = makeCubeMeshData();
    Mesh cubeMesh = buildMesh(cubeData);
    uint32_t cubeMeshId = res.addMesh(std::move(cubeMesh));

    // -- Create materials (one per entity so we can swap color on selection) ---
    // Also create a yellow material for highlighting the selected node.
    uint32_t materialIds[kNodeCount];
    uint32_t yellowMatIds[kNodeCount];
    for (int i = 0; i < kNodeCount; i++)
    {
        Material mat;
        mat.albedo = {kLevelColors[kNodes[i].colorLevel].x, kLevelColors[kNodes[i].colorLevel].y,
                      kLevelColors[kNodes[i].colorLevel].z, 1.0f};
        mat.roughness = 0.5f;
        mat.metallic = 0.0f;
        materialIds[i] = res.addMaterial(mat);

        Material ymat;
        ymat.albedo = {kSelectedColor.x, kSelectedColor.y, kSelectedColor.z, 1.0f};
        ymat.roughness = 0.5f;
        ymat.metallic = 0.0f;
        yellowMatIds[i] = res.addMaterial(ymat);
    }

    // -- ImGui ----------------------------------------------------------------
    imguiCreate(16.f);
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
        io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
        io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
        io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
        io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
        io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
        io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
        io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
        io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
        io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
        io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
        io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
        io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    glfwSetScrollCallback(glfwHandle,
                          [](GLFWwindow*, double /*xoff*/, double yoff)
                          {
                              s_imguiScrollF += static_cast<float>(yoff);
                              s_zoomScrollDelta += static_cast<float>(yoff);
                          });

    float s_contentScaleX = 1.f, s_contentScaleY = 1.f;
    glfwGetWindowContentScale(glfwHandle, &s_contentScaleX, &s_contentScaleY);

    renderer.endFrame();  // flush resource uploads

    // -- ECS ------------------------------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;

    // Create 9 entities with hierarchy
    EntityID entities[kNodeCount];
    for (int i = 0; i < kNodeCount; i++)
    {
        entities[i] = reg.createEntity();

        // Transform
        TransformComponent tc;
        tc.position = {kNodes[i].localPos.x, kNodes[i].localPos.y, kNodes[i].localPos.z};
        tc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = {kNodes[i].scale, kNodes[i].scale, kNodes[i].scale};
        tc.flags = 1;  // dirty
        reg.emplace<TransformComponent>(entities[i], tc);

        // World transform (will be computed by TransformSystem)
        reg.emplace<WorldTransformComponent>(entities[i]);

        // Mesh and material
        reg.emplace<MeshComponent>(entities[i], cubeMeshId);
        reg.emplace<MaterialComponent>(entities[i], materialIds[i]);

        // Visible tag (no frustum culling in this demo)
        reg.emplace<VisibleTag>(entities[i]);
    }

    // Set up hierarchy
    for (int i = 0; i < kNodeCount; i++)
    {
        if (kNodes[i].parentIdx >= 0)
        {
            engine::scene::setParent(reg, entities[i], entities[kNodes[i].parentIdx]);
        }
    }

    // -- Input ----------------------------------------------------------------
    GlfwInputBackend inputBackend(glfwHandle);
    InputSystem inputSys(inputBackend);
    InputState inputState;

    // -- Light ----------------------------------------------------------------
    const glm::vec3 kLightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
    constexpr float kLightIntens = 6.0f;
    const float lightData[8] = {
        kLightDir.x,         kLightDir.y,          kLightDir.z,          0.f,
        1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};

    const glm::vec3 kLightPos = kLightDir * 20.f;
    const glm::mat4 lightView = glm::lookAt(kLightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-12.f, 12.f, -8.f, 8.f, 0.1f, 50.f);

    // -- Camera and interaction state -----------------------------------------
    OrbitCamera cam;
    int prevFbW = 0, prevFbH = 0;
    double prevTime = glfwGetTime();

    int selectedIdx = -1;
    bool dragging = false;
    glm::vec3 dragOffset = {0, 0, 0};  // offset from hit point to cube world pos
    bool rightDragging = false;
    double prevMouseX = 0.0, prevMouseY = 0.0;

    // -- Recursive ImGui hierarchy drawer -------------------------------------
    // Forward-declared lambda workaround using std::function.
    std::function<void(int)> drawHierarchyNode = [&](int idx)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;

        // Check if this node has children
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

        // Click to select
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

    // -- Main loop ------------------------------------------------------------
    while (!window->shouldClose())
    {
        double now = glfwGetTime();
        float dt = static_cast<float>(glm::min(now - prevTime, 0.05));
        prevTime = now;

        window->pollEvents();

        int fbW, fbH;
        glfwGetFramebufferSize(glfwHandle, &fbW, &fbH);
        if ((fbW != prevFbW || fbH != prevFbH) && fbW > 0 && fbH > 0)
        {
            renderer.resize(static_cast<uint32_t>(fbW), static_cast<uint32_t>(fbH));
            prevFbW = fbW;
            prevFbH = fbH;
        }
        if (fbW <= 0 || fbH <= 0)
        {
            renderer.endFrame();
            continue;
        }

        // -- Input ------------------------------------------------------------
        inputSys.update(inputState);

        double mx, my;
        glfwGetCursorPos(glfwHandle, &mx, &my);
        float physMx = static_cast<float>(mx * s_contentScaleX);
        float physMy = static_cast<float>(my * s_contentScaleY);

        // -- ImGui begin frame ------------------------------------------------
        {
            uint8_t imguiButtons = 0;
            if (glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
                imguiButtons |= IMGUI_MBUT_LEFT;
            if (glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
                imguiButtons |= IMGUI_MBUT_RIGHT;
            if (glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
                imguiButtons |= IMGUI_MBUT_MIDDLE;

            ImGuiIO& io = ImGui::GetIO();
            static const int kNavKeys[] = {GLFW_KEY_UP,        GLFW_KEY_DOWN, GLFW_KEY_PAGE_UP,
                                           GLFW_KEY_PAGE_DOWN, GLFW_KEY_HOME, GLFW_KEY_END};
            for (int k : kNavKeys)
                io.KeysDown[k] = (glfwGetKey(glfwHandle, k) == GLFW_PRESS);

            imguiBeginFrame(static_cast<int32_t>(physMx), static_cast<int32_t>(physMy),
                            imguiButtons, static_cast<int32_t>(s_imguiScrollF),
                            static_cast<uint16_t>(fbW), static_cast<uint16_t>(fbH), -1, kViewImGui);
        }

        bool imguiWantsMouse = ImGui::GetIO().WantCaptureMouse;

        // -- Camera matrices (needed for picking) -----------------------------
        glm::mat4 viewMat = cam.view();
        glm::mat4 projMat = glm::perspective(
            glm::radians(45.f), static_cast<float>(fbW) / static_cast<float>(fbH), 0.05f, 100.f);
        glm::vec3 camPos = cam.position();

        // -- Left click: picking / dragging -----------------------------------
        if (!imguiWantsMouse)
        {
            bool leftDown = glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool leftJustPressed = inputState.isMouseButtonPressed(MouseButton::Left);

            if (leftJustPressed)
            {
                // Cast ray and find closest intersection
                glm::vec3 nearPt = screenToWorld(physMx, physMy, static_cast<float>(fbW),
                                                 static_cast<float>(fbH), viewMat, projMat);
                glm::vec3 rayDir = glm::normalize(nearPt - camPos);

                float bestT = 1e30f;
                int bestIdx = -1;

                for (int i = 0; i < kNodeCount; i++)
                {
                    auto* wtc = reg.get<WorldTransformComponent>(entities[i]);
                    if (!wtc)
                        continue;

                    const Mesh* mesh = res.getMesh(cubeMeshId);
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

                // Update selection and material highlighting
                if (selectedIdx >= 0 && selectedIdx != bestIdx)
                {
                    // Restore previous selection's material
                    auto* mc = reg.get<MaterialComponent>(entities[selectedIdx]);
                    if (mc)
                        mc->material = materialIds[selectedIdx];
                }

                selectedIdx = bestIdx;

                if (selectedIdx >= 0)
                {
                    // Highlight selected
                    auto* mc = reg.get<MaterialComponent>(entities[selectedIdx]);
                    if (mc)
                        mc->material = yellowMatIds[selectedIdx];

                    // Compute drag offset
                    auto* wtc = reg.get<WorldTransformComponent>(entities[selectedIdx]);
                    glm::vec3 cubeWorldPos =
                        glm::vec3(wtc->matrix[3][0], wtc->matrix[3][1], wtc->matrix[3][2]);
                    glm::vec3 nearPt2 = screenToWorld(physMx, physMy, static_cast<float>(fbW),
                                                      static_cast<float>(fbH), viewMat, projMat);
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
                // Continue dragging — project mouse onto plane through cube
                auto* wtc = reg.get<WorldTransformComponent>(entities[selectedIdx]);
                glm::vec3 cubeWorldPos =
                    glm::vec3(wtc->matrix[3][0], wtc->matrix[3][1], wtc->matrix[3][2]);

                glm::vec3 nearPt2 = screenToWorld(physMx, physMy, static_cast<float>(fbW),
                                                  static_cast<float>(fbH), viewMat, projMat);
                glm::vec3 rayDir2 = glm::normalize(nearPt2 - camPos);
                glm::vec3 planeNormal = glm::normalize(camPos - cubeWorldPos);
                float denom = glm::dot(planeNormal, rayDir2);
                if (std::abs(denom) > 1e-6f)
                {
                    float t = glm::dot(cubeWorldPos - camPos, planeNormal) / denom;
                    glm::vec3 hitPoint = camPos + rayDir2 * t;
                    glm::vec3 newWorldPos = hitPoint + dragOffset;

                    // Convert world delta to local: multiply by inverse of parent's world matrix
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
                        tc->position = {localPos.x, localPos.y, localPos.z};
                        tc->flags |= 1;  // mark dirty
                    }
                }
            }
            else if (!leftDown)
            {
                dragging = false;
            }

            // -- Right click: orbit camera ------------------------------------
            bool rightDown = glfwGetMouseButton(glfwHandle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
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

            // -- Scroll: zoom -------------------------------------------------
            if (std::abs(s_zoomScrollDelta) > 0.01f)
            {
                cam.distance -= s_zoomScrollDelta * 1.0f;
                cam.distance = glm::clamp(cam.distance, 3.0f, 50.0f);
            }
        }

        prevMouseX = mx;
        prevMouseY = my;
        s_zoomScrollDelta = 0.f;

        // -- Update material highlighting for selected node -------------------
        // (Ensure material stays correct even when selected via ImGui panel)
        for (int i = 0; i < kNodeCount; i++)
        {
            auto* mc = reg.get<MaterialComponent>(entities[i]);
            if (!mc)
                continue;
            if (i == selectedIdx)
                mc->material = yellowMatIds[i];
            else
                mc->material = materialIds[i];
        }

        // -- Transform system: compose local TRS -> world matrices ------------
        transformSys.update(reg);

        // -- Render -----------------------------------------------------------
        renderer.beginFrameDirect();

        // Recompute camera matrices after transform update
        viewMat = cam.view();
        camPos = cam.position();

        // Shadow pass (view 0)
        shadow.beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, res, shadowProg, 0);

        // Opaque pass (view 9) to backbuffer
        const auto W = static_cast<uint16_t>(fbW);
        const auto H = static_cast<uint16_t>(fbH);

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x1A1A2EFF)
            .transform(viewMat, projMat);

        const glm::mat4 shadowMat = shadow.shadowMatrix(0);
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), shadow.atlasTexture(), W, H, 0.05f, 100.f,
        };
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;
        drawCallSys.update(reg, res, pbrProg, renderer.uniforms(), frame);

        // -- HUD --------------------------------------------------------------
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(1, 1, 0x0f, "Hierarchy Demo  |  %.1f fps  |  %.3f ms",
                            dt > 0 ? 1.f / dt : 0.f, dt * 1000.f);
        bgfx::dbgTextPrintf(1, 2, 0x07, "LMB=pick/drag  |  RMB=orbit  |  Scroll=zoom");

        // -- ImGui hierarchy panel --------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene Hierarchy"))
        {
            // Draw root nodes (those with parentIdx == -1)
            for (int i = 0; i < kNodeCount; i++)
            {
                if (kNodes[i].parentIdx == -1)
                    drawHierarchyNode(i);
            }

            ImGui::Separator();

            // Show selected node info
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

        imguiEndFrame();

        // -- Flip -------------------------------------------------------------
        renderer.endFrame();
    }

    // -- Cleanup --------------------------------------------------------------
    imguiDestroy();

    shadow.shutdown();
    if (bgfx::isValid(shadowProg))
        bgfx::destroy(shadowProg);
    if (bgfx::isValid(pbrProg))
        bgfx::destroy(pbrProg);
    if (bgfx::isValid(whiteTex))
        bgfx::destroy(whiteTex);
    if (bgfx::isValid(whiteCubeTex))
        bgfx::destroy(whiteCubeTex);
    res.destroyAll();

    renderer.endFrame();
    renderer.shutdown();

    return 0;
}
