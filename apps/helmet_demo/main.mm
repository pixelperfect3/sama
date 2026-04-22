// Helmet Demo — macOS
//
// Loads the Damaged Helmet glTF (single GLB file) via the async AssetManager,
// spawns ECS entities from the loaded scene, and renders with PBR + directional
// shadow.  Camera orbits the helmet with drag-to-orbit and WASD target movement.
//
// Controls:
//   LMB / RMB drag  — orbit camera
//   WASD             — move camera target
//   Q / E            — move target down / up
//   Shift            — 3× speed
//   F                — toggle HUD

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/assets/AssetManager.h"
#include "engine/assets/GltfAsset.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/GltfSceneSpawner.h"
#include "engine/assets/StdFileSystem.h"
#include "engine/assets/TextureLoader.h"
#include "engine/core/Engine.h"
#include "engine/core/OrbitCamera.h"
#include "engine/debug/DebugTexturePanel.h"
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
#include "engine/ui/DebugHud.h"
#include "imgui.h"

using namespace engine::assets;
using namespace engine::core;
using namespace engine::ecs;
using namespace engine::input;
using namespace engine::rendering;
using namespace engine::threading;

// =============================================================================
// Entry point
// =============================================================================

int main()
{
    Engine eng;
    EngineDesc desc;
    desc.windowTitle = "Damaged Helmet";
    if (!eng.init(desc))
        return 1;

    // Override scroll callback to feed the engine's ImGui scroll accumulator.
    glfwSetScrollCallback(eng.glfwHandle(),
                          [](GLFWwindow* win, double /*xoff*/, double yoff)
                          {
                              auto* e = static_cast<Engine*>(glfwGetWindowUserPointer(win));
                              if (e)
                                  e->imguiScrollAccum() += static_cast<float>(yoff);
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

    // -- Start async helmet load ----------------------------------------------
    auto helmetHandle = assets.load<GltfAsset>("DamagedHelmet.glb");

    // -- ECS ------------------------------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;
    bool helmetSpawned = false;

    // -- Light indicator (small bright cube at the light position) ------------
    uint32_t lightMeshId = eng.resources().addMesh(buildMesh(makeCubeMeshData()));
    Material lightMat{};
    lightMat.albedo = {1.0f, 0.9f, 0.3f, 1.0f};
    lightMat.emissiveScale = 5.0f;
    lightMat.roughness = 1.0f;
    uint32_t lightMatId = eng.resources().addMaterial(lightMat);

    EntityID lightIndicator = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = {0.0f, 5.0f, 0.0f};
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {0.15f, 0.15f, 0.15f};
        reg.emplace<TransformComponent>(lightIndicator, tc);
        reg.emplace<MeshComponent>(lightIndicator, MeshComponent{lightMeshId});
        reg.emplace<MaterialComponent>(lightIndicator, MaterialComponent{lightMatId});
        reg.emplace<VisibleTag>(lightIndicator);
    }

    // -- Debug texture panel --------------------------------------------------
    engine::debug::DebugTexturePanel texPanel;

    bool mouseCaptured = false;
    bool skipMouseFrame = false;

    // -- Light ----------------------------------------------------------------
    constexpr float kLightIntens = 15.0f;
    constexpr float kLightOrbitPeriod = 6.0f;
    constexpr float kLightElevation = 0.65f;
    const glm::mat4 lightProj = glm::ortho(-3.f, 3.f, -3.f, 3.f, 0.1f, 30.f);
    float lightAngle = 0.f;

    float renderMs = 0.f;

    engine::ui::DebugHud hud;
    hud.init();

    // -- Main loop ------------------------------------------------------------
    engine::core::OrbitCamera cam;
    cam.distance = 3.5f;
    cam.yaw = 40.0f;
    cam.pitch = 8.0f;
    cam.target = {0, 0, 0};
    bool showHud = true;
    float dt = 0.f;

    while (eng.beginFrame(dt))
    {
        if (eng.fbWidth() == 0 || eng.fbHeight() == 0)
            continue;

        const auto& input = eng.inputState();
        const float fbW = static_cast<float>(eng.fbWidth());
        const float fbH = static_cast<float>(eng.fbHeight());

        if (input.isKeyPressed(Key::F))
            showHud = !showHud;

        // -- Camera orbit (right-drag) and zoom (scroll) ----------------------
        if (!eng.imguiWantsMouse())
        {
            bool rightDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            bool leftDown =
                glfwGetMouseButton(eng.glfwHandle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool dragging = rightDown || leftDown;
            if (dragging)
            {
                if (mouseCaptured)
                {
                    cam.orbit(static_cast<float>(input.mouseDeltaX()),
                              -static_cast<float>(input.mouseDeltaY()), 0.18f);
                }
                if (!mouseCaptured)
                {
                    mouseCaptured = true;
                    skipMouseFrame = true;
                    glfwSetInputMode(eng.glfwHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    if (glfwRawMouseMotionSupported())
                        glfwSetInputMode(eng.glfwHandle(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
                }
            }
            else if (mouseCaptured)
            {
                mouseCaptured = false;
                skipMouseFrame = false;
                glfwSetInputMode(eng.glfwHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }

        // WASD/QE target movement
        float moveSpeed = 3.0f * (input.isKeyHeld(Key::LeftShift) ? 3.5f : 1.0f);
        cam.moveTarget(input, dt, moveSpeed);

        // -- Asset uploads & spawn on Ready -----------------------------------
        assets.processUploads();

        const AssetState helmetState = assets.state(helmetHandle);
        if (!helmetSpawned && helmetState == AssetState::Ready)
        {
            const GltfAsset* helmet = assets.get<GltfAsset>(helmetHandle);
            GltfSceneSpawner::spawn(*helmet, reg, eng.resources());
            helmetSpawned = true;

            reg.view<MaterialComponent>().each(
                [&](EntityID, MaterialComponent& mc)
                {
                    auto* mat = eng.resources().getMaterialMut(mc.material);
                    if (mat)
                        mat->roughness = 0.55f;
                });

            static const char* kTexNames[] = {"Albedo", "ORM (G=rough B=metal)", "Emissive",
                                              "Occlusion", "Normal"};
            const uint32_t n = eng.resources().textureCount();
            for (uint32_t i = 1; i <= n; ++i)
            {
                bgfx::TextureHandle th = eng.resources().getTexture(i);
                const char* name = (i <= 5) ? kTexNames[i - 1] : "Texture";
                char label[64];
                snprintf(label, sizeof(label), "[%u] %s", i, name);
                texPanel.add(th, label);
            }
        }
        else if (helmetState == AssetState::Failed)
        {
            static bool printed = false;
            if (!printed)
            {
                printed = true;
                fprintf(stderr, "helmet_demo: asset load failed: %s\n",
                        assets.error(helmetHandle).c_str());
            }
        }

        // -- Transform system -------------------------------------------------
        transformSys.update(reg);

        // -- Rotating light ---------------------------------------------------
        lightAngle += dt * (2.0f * 3.14159265f / kLightOrbitPeriod);
        const float cosA = std::cos(lightAngle);
        const float sinA = std::sin(lightAngle);
        const float cosE = std::sqrt(1.0f - kLightElevation * kLightElevation);
        const glm::vec3 lightDir =
            glm::normalize(glm::vec3(cosE * sinA, kLightElevation, cosE * cosA));
        const float lightData[8] = {
            lightDir.x,          lightDir.y,           lightDir.z,           0.f,
            1.0f * kLightIntens, 0.95f * kLightIntens, 0.85f * kLightIntens, 0.f};
        const glm::vec3 lightPos = lightDir * 10.f;
        const glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));

        auto* ltc = reg.get<TransformComponent>(lightIndicator);
        if (ltc)
        {
            ltc->position = lightDir * 3.0f;
            ltc->flags |= 0x01;
        }

        // -- Render -----------------------------------------------------------
        double frameStart = glfwGetTime();
        eng.renderer().beginFrameDirect();

        // Shadow pass
        eng.shadow().beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, eng.resources(), eng.shadowProgram(), 0);

        // Opaque pass
        const auto W = eng.fbWidth();
        const auto H = eng.fbHeight();

        glm::mat4 view = cam.view();
        glm::mat4 proj = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 50.f);

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x1A1A2EFF)
            .transform(view, proj);

        const glm::mat4 shadowMat = eng.shadow().shadowMatrix(0);
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), eng.shadow().atlasTexture(), W, H, 0.05f, 50.f,
        };
        glm::vec3 camPos = cam.position();
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;

        if (ibl.isValid())
        {
            frame.iblEnabled = true;
            frame.maxMipLevels = 8.0f;
            frame.irradiance = ibl.irradiance();
            frame.prefiltered = ibl.prefiltered();
            frame.brdfLut = ibl.brdfLut();
        }
        drawCallSys.update(reg, eng.resources(), eng.pbrProgram(), eng.uniforms(), frame);

        // -- HUD --------------------------------------------------------------
        hud.begin(eng.fbWidth(), eng.fbHeight());
        if (showHud)
        {
            hud.printf(1, 1, "Helmet Demo  |  %.1f fps  |  %.3f ms  |  render %.3f ms",
                       dt > 0 ? 1.f / dt : 0.f, dt * 1000.f, renderMs);
            hud.printf(1, 4, "Arena: %zu KB / %zu KB used", eng.frameArena().bytesUsed() / 1024,
                       eng.frameArena().capacity() / 1024);

            if (helmetState == AssetState::Ready)
                hud.printf(1, 2, "DamagedHelmet.glb — Ready");
            else if (helmetState == AssetState::Failed)
                hud.printf(1, 2, "DamagedHelmet.glb — FAILED: %s",
                           assets.error(helmetHandle).c_str());
            else
                hud.printf(1, 2, "DamagedHelmet.glb — Loading...");

            hud.printf(1, 3, "Drag=orbit  |  WASD/QE=move  |  Shift=fast  |  F=HUD");
        }
        hud.end();

        // -- ImGui texture panel ----------------------------------------------
        texPanel.show();

        double frameEnd = glfwGetTime();
        renderMs = static_cast<float>((frameEnd - frameStart) * 1000.0);

        eng.endFrame();
    }

    // -- Cleanup --------------------------------------------------------------
    hud.shutdown();
    assets.release(helmetHandle);
    ibl.shutdown();

    return 0;
}
