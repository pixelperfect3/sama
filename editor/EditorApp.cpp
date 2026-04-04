#include "editor/EditorApp.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "editor/EditorState.h"
#include "editor/gizmo/GizmoRenderer.h"
#include "editor/gizmo/TransformGizmo.h"
#include "editor/inspectors/TransformInspector.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/PropertiesPanel.h"
#include "editor/platform/IEditorWindow.h"
#include "editor/platform/cocoa/CocoaEditorWindow.h"
#include "engine/core/OrbitCamera.h"
#include "engine/ecs/Registry.h"
#include "engine/memory/FrameArena.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/NameComponent.h"
#include "engine/scene/TransformSystem.h"

using engine::ecs::EntityID;
using engine::ecs::INVALID_ENTITY;
using namespace engine::core;
using namespace engine::ecs;
using namespace engine::rendering;

namespace engine::editor
{

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct EditorApp::Impl
{
    std::unique_ptr<CocoaEditorWindow> window;

    // Renderer state (not using engine::rendering::Renderer to avoid GLFW dependency
    // through engine_core; instead we init bgfx directly).
    ShaderUniforms uniforms;
    RenderResources resources;
    bgfx::ProgramHandle pbrProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle shadowProgram = BGFX_INVALID_HANDLE;

    // Default textures
    bgfx::TextureHandle whiteTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle neutralNormalTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteCubeTex = BGFX_INVALID_HANDLE;

    // ECS
    Registry registry;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;

    // Scene entities
    EntityID cubeEntity = 0;
    EntityID groundEntity = 0;
    uint32_t cubeMeshId = 0;

    // Camera
    OrbitCamera camera;

    // Editor state and panels
    EditorState editorState;
    std::unique_ptr<HierarchyPanel> hierarchyPanel;
    std::unique_ptr<PropertiesPanel> propertiesPanel;

    // Gizmo
    std::unique_ptr<TransformGizmo> gizmo;
    GizmoRenderer gizmoRenderer;

    // Selection highlight material
    uint32_t selectionMatId = 0;

    // Frame arena
    std::unique_ptr<engine::memory::FrameArena> frameArena;

    // Frame timing
    double prevTime = 0.0;
    uint16_t fbW = 0;
    uint16_t fbH = 0;
    bool initialized = false;
};

EditorApp::EditorApp() : impl_(std::make_unique<Impl>()) {}

EditorApp::~EditorApp()
{
    shutdown();
}

bool EditorApp::init(uint32_t width, uint32_t height)
{
    // -- Window ---------------------------------------------------------------
    impl_->window = std::make_unique<CocoaEditorWindow>();
    if (!impl_->window->init(width, height, "Sama Editor"))
    {
        fprintf(stderr, "EditorApp: failed to create window\n");
        return false;
    }

    // -- bgfx init ------------------------------------------------------------
    // Single-threaded mode: call renderFrame before init.
    bgfx::renderFrame();

    bgfx::Init init;
    init.type = bgfx::RendererType::Metal;
    init.platformData.nwh = impl_->window->nativeLayer();
    init.resolution.width = impl_->window->framebufferWidth();
    init.resolution.height = impl_->window->framebufferHeight();
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init))
    {
        fprintf(stderr, "EditorApp: bgfx::init() failed\n");
        return false;
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);

    impl_->fbW = static_cast<uint16_t>(impl_->window->framebufferWidth());
    impl_->fbH = static_cast<uint16_t>(impl_->window->framebufferHeight());

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, impl_->fbW, impl_->fbH);

    // -- Uniforms -------------------------------------------------------------
    impl_->uniforms.init();

    // -- Shader programs ------------------------------------------------------
    impl_->pbrProgram = loadPbrProgram();
    impl_->shadowProgram = loadShadowProgram();

    // -- Default textures -----------------------------------------------------
    {
        const uint8_t kWhite[4] = {255, 255, 255, 255};
        impl_->whiteTex =
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                  bgfx::copy(kWhite, sizeof(kWhite)));
        impl_->resources.setWhiteTexture(impl_->whiteTex);

        const uint8_t kNeutralNormal[4] = {128, 128, 255, 255};
        impl_->neutralNormalTex =
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                  bgfx::copy(kNeutralNormal, sizeof(kNeutralNormal)));
        impl_->resources.setNeutralNormalTexture(impl_->neutralNormalTex);

        uint8_t cubeFaces[6 * 4];
        for (int i = 0; i < 6 * 4; ++i)
            cubeFaces[i] = 255;
        impl_->whiteCubeTex =
            bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                    bgfx::copy(cubeFaces, sizeof(cubeFaces)));
        impl_->resources.setWhiteCubeTexture(impl_->whiteCubeTex);
    }

    // -- Scene ----------------------------------------------------------------
    impl_->cubeMeshId = impl_->resources.addMesh(buildMesh(makeCubeMeshData()));

    // Colored cube in the center.
    {
        Material cubeMat{};
        cubeMat.albedo = {0.8f, 0.2f, 0.2f, 1.0f};
        cubeMat.roughness = 0.4f;
        cubeMat.metallic = 0.0f;
        uint32_t cubMatId = impl_->resources.addMaterial(cubeMat);

        impl_->cubeEntity = impl_->registry.createEntity();
        TransformComponent tc{};
        tc.position = {0.0f, 0.5f, 0.0f};
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {1.0f, 1.0f, 1.0f};
        tc.flags = 0x01;  // dirty
        impl_->registry.emplace<TransformComponent>(impl_->cubeEntity, tc);
        impl_->registry.emplace<WorldTransformComponent>(impl_->cubeEntity);
        impl_->registry.emplace<MeshComponent>(impl_->cubeEntity, MeshComponent{impl_->cubeMeshId});
        impl_->registry.emplace<MaterialComponent>(impl_->cubeEntity, MaterialComponent{cubMatId});
        impl_->registry.emplace<VisibleTag>(impl_->cubeEntity);
        impl_->registry.emplace<engine::scene::NameComponent>(
            impl_->cubeEntity, engine::scene::NameComponent{"Red Cube"});
    }

    // Ground plane (large flat cube).
    {
        Material groundMat{};
        groundMat.albedo = {0.4f, 0.4f, 0.4f, 1.0f};
        groundMat.roughness = 0.9f;
        groundMat.metallic = 0.0f;
        uint32_t gndMatId = impl_->resources.addMaterial(groundMat);

        impl_->groundEntity = impl_->registry.createEntity();
        TransformComponent tc{};
        tc.position = {0.0f, -0.005f, 0.0f};
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {20.0f, 0.01f, 20.0f};
        tc.flags = 0x01;
        impl_->registry.emplace<TransformComponent>(impl_->groundEntity, tc);
        impl_->registry.emplace<WorldTransformComponent>(impl_->groundEntity);
        impl_->registry.emplace<MeshComponent>(impl_->groundEntity,
                                               MeshComponent{impl_->cubeMeshId});
        impl_->registry.emplace<MaterialComponent>(impl_->groundEntity,
                                                   MaterialComponent{gndMatId});
        impl_->registry.emplace<VisibleTag>(impl_->groundEntity);
        impl_->registry.emplace<engine::scene::NameComponent>(
            impl_->groundEntity, engine::scene::NameComponent{"Ground"});
    }

    // -- Selection highlight material -----------------------------------------
    {
        Material selMat{};
        selMat.albedo = {1.0f, 0.8f, 0.0f, 1.0f};  // bright yellow
        selMat.roughness = 1.0f;
        selMat.metallic = 0.0f;
        impl_->selectionMatId = impl_->resources.addMaterial(selMat);
    }

    // -- Editor state and panels ----------------------------------------------
    impl_->hierarchyPanel =
        std::make_unique<HierarchyPanel>(impl_->registry, impl_->editorState, *impl_->window);
    impl_->hierarchyPanel->init();

    impl_->propertiesPanel =
        std::make_unique<PropertiesPanel>(impl_->registry, impl_->editorState, *impl_->window);
    impl_->propertiesPanel->addInspector(std::make_unique<TransformInspector>(*impl_->window));
    impl_->propertiesPanel->init();

    impl_->gizmo =
        std::make_unique<TransformGizmo>(impl_->registry, impl_->editorState, *impl_->window);
    impl_->gizmoRenderer.init();

    // -- Camera ---------------------------------------------------------------
    impl_->camera.distance = 5.0f;
    impl_->camera.yaw = 45.0f;
    impl_->camera.pitch = 25.0f;
    impl_->camera.target = {0.0f, 0.5f, 0.0f};

    // -- Frame arena ----------------------------------------------------------
    impl_->frameArena = std::make_unique<engine::memory::FrameArena>(2 * 1024 * 1024);

    // -- Timing ---------------------------------------------------------------
    impl_->prevTime =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    // Flush initial resource uploads.
    bgfx::frame();

    impl_->initialized = true;
    return true;
}

void EditorApp::run()
{
    if (!impl_->initialized)
        return;

    while (!impl_->window->shouldClose())
    {
        // -- Timing -----------------------------------------------------------
        double now =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        float dt = static_cast<float>(std::min(now - impl_->prevTime, 0.05));
        impl_->prevTime = now;

        // -- Events -----------------------------------------------------------
        impl_->window->pollEvents();

        // -- Resize -----------------------------------------------------------
        uint32_t fbW = impl_->window->framebufferWidth();
        uint32_t fbH = impl_->window->framebufferHeight();
        if ((fbW != impl_->fbW || fbH != impl_->fbH) && fbW > 0 && fbH > 0)
        {
            bgfx::reset(fbW, fbH, BGFX_RESET_VSYNC);
            bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(fbW), static_cast<uint16_t>(fbH));
            impl_->fbW = static_cast<uint16_t>(fbW);
            impl_->fbH = static_cast<uint16_t>(fbH);
        }

        if (fbW == 0 || fbH == 0)
        {
            bgfx::frame();
            continue;
        }

        // -- Camera -----------------------------------------------------------
        if (impl_->window->isRightMouseDown())
        {
            impl_->camera.orbit(static_cast<float>(impl_->window->mouseDeltaX()),
                                -static_cast<float>(impl_->window->mouseDeltaY()), 0.25f);
        }

        double scrollY = impl_->window->scrollDeltaY();
        if (std::abs(scrollY) > 0.01)
        {
            // macOS scroll is in content units, scale down for smooth zoom.
            impl_->camera.zoom(static_cast<float>(scrollY * 0.1), 1.0f, 1.0f, 100.0f);
        }

        // -- Gizmo update (before transform system) ---------------------------
        {
            float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
            glm::mat4 gView = impl_->camera.view();
            glm::mat4 gProj = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 200.0f);
            impl_->gizmo->update(dt, gView, gProj);
        }

        // -- Transform system -------------------------------------------------
        impl_->transformSys.update(impl_->registry);

        // -- Render -----------------------------------------------------------
        // Touch shadow view to avoid bgfx warnings.
        RenderPass(kViewShadowBase).touch();

        // Opaque pass (direct to backbuffer, no post-processing).
        RenderPass(kViewOpaque).framebuffer(BGFX_INVALID_HANDLE);

        const auto W = impl_->fbW;
        const auto H = impl_->fbH;
        float aspect = static_cast<float>(W) / static_cast<float>(H);

        glm::mat4 view = impl_->camera.view();
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 200.0f);

        RenderPass(kViewOpaque)
            .rect(0, 0, W, H)
            .clearColorAndDepth(0x303030FF)
            .transform(view, proj);

        // Directional light (fixed sun direction).
        const glm::vec3 lightDir = glm::normalize(glm::vec3(0.4f, 0.7f, 0.5f));
        const float lightIntensity = 6.0f;
        const float lightData[8] = {lightDir.x,
                                    lightDir.y,
                                    lightDir.z,
                                    0.0f,
                                    1.0f * lightIntensity,
                                    0.95f * lightIntensity,
                                    0.85f * lightIntensity,
                                    0.0f};

        // Dummy shadow matrix (identity -- no shadows in Phase 1).
        glm::mat4 identMat(1.0f);
        PbrFrameParams frame{};
        frame.lightData = lightData;
        frame.shadowMatrix = glm::value_ptr(identMat);
        frame.shadowAtlas = BGFX_INVALID_HANDLE;
        frame.viewportW = W;
        frame.viewportH = H;
        frame.nearPlane = 0.05f;
        frame.farPlane = 200.0f;

        glm::vec3 camPos = impl_->camera.position();
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;

        impl_->drawCallSys.update(impl_->registry, impl_->resources, impl_->pbrProgram,
                                  impl_->uniforms, frame);

        // -- Selection highlight -------------------------------------------------
        {
            EntityID selE = impl_->editorState.primarySelection();
            if (selE != INVALID_ENTITY)
            {
                auto* wt = impl_->registry.get<WorldTransformComponent>(selE);
                auto* mc = impl_->registry.get<MeshComponent>(selE);
                if (wt && mc)
                {
                    const Mesh* mesh = impl_->resources.getMesh(mc->mesh);
                    if (mesh && mesh->isValid())
                    {
                        // Scale slightly larger to create outline effect.
                        glm::mat4 outlineMtx = glm::scale(wt->matrix, glm::vec3(1.02f));
                        bgfx::setTransform(glm::value_ptr(outlineMtx));

                        // Yellow selection material.
                        const float matData[8] = {
                            1.0f, 0.8f, 0.0f, 1.0f,  // albedo
                            0.0f, 0.0f, 0.0f, 0.0f,  // metallic etc
                        };
                        bgfx::setUniform(impl_->uniforms.u_material, matData, 2);

                        // Directional light (same as scene).
                        bgfx::setUniform(impl_->uniforms.u_dirLight, lightData, 2);

                        // Bind default textures.
                        bgfx::setTexture(0, impl_->uniforms.s_albedo,
                                         impl_->resources.whiteTexture());
                        bgfx::setTexture(1, impl_->uniforms.s_normal,
                                         impl_->resources.neutralNormalTexture());
                        bgfx::setTexture(2, impl_->uniforms.s_orm, impl_->resources.whiteTexture());

                        bgfx::setVertexBuffer(0, mesh->positionVbh);
                        if (bgfx::isValid(mesh->surfaceVbh))
                        {
                            bgfx::setVertexBuffer(1, mesh->surfaceVbh);
                        }
                        bgfx::setIndexBuffer(mesh->ibh);

                        bgfx::setState(BGFX_STATE_DEFAULT | BGFX_STATE_PT_LINES);

                        bgfx::submit(kViewOpaque, impl_->pbrProgram);
                    }
                }
            }
        }

        // -- Gizmo rendering --------------------------------------------------
        {
            EntityID selE = impl_->editorState.primarySelection();
            if (selE != INVALID_ENTITY)
            {
                float aspect = static_cast<float>(W) / static_cast<float>(H);
                glm::mat4 gView = impl_->camera.view();
                glm::mat4 gProj = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 200.0f);
                impl_->gizmoRenderer.render(*impl_->gizmo, gView, gProj, W, H);
            }
        }

        // -- Panels -----------------------------------------------------------
        impl_->hierarchyPanel->update(dt);
        impl_->propertiesPanel->update(dt);

        // -- HUD --------------------------------------------------------------
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(1, 1, 0x0f, "Sama Editor  |  %.1f fps  |  %.3f ms",
                            dt > 0.0f ? 1.0f / dt : 0.0f, dt * 1000.0f);
        const char* modeStr = "Translate";
        if (impl_->gizmo->mode() == GizmoMode::Rotate)
            modeStr = "Rotate";
        else if (impl_->gizmo->mode() == GizmoMode::Scale)
            modeStr = "Scale";
        bgfx::dbgTextPrintf(1, 2, 0x07, "Right-drag=orbit  Scroll=zoom  W/E/R=gizmo [%s]", modeStr);

        // Render panels (hierarchy, properties) as debug text.
        impl_->hierarchyPanel->render();
        impl_->propertiesPanel->render();

        // -- End frame --------------------------------------------------------
        impl_->frameArena->reset();
        bgfx::frame();
    }
}

void EditorApp::shutdown()
{
    if (!impl_->initialized)
        return;

    // Shutdown panels.
    if (impl_->hierarchyPanel)
    {
        impl_->hierarchyPanel->shutdown();
    }
    if (impl_->propertiesPanel)
    {
        impl_->propertiesPanel->shutdown();
    }

    impl_->gizmoRenderer.shutdown();

    // Destroy shader programs.
    if (bgfx::isValid(impl_->pbrProgram))
        bgfx::destroy(impl_->pbrProgram);
    if (bgfx::isValid(impl_->shadowProgram))
        bgfx::destroy(impl_->shadowProgram);

    // Destroy default textures.
    if (bgfx::isValid(impl_->whiteTex))
        bgfx::destroy(impl_->whiteTex);
    if (bgfx::isValid(impl_->neutralNormalTex))
        bgfx::destroy(impl_->neutralNormalTex);
    if (bgfx::isValid(impl_->whiteCubeTex))
        bgfx::destroy(impl_->whiteCubeTex);

    impl_->resources.destroyAll();
    impl_->uniforms.destroy();

    bgfx::frame();
    bgfx::shutdown();

    impl_->window->shutdown();
    impl_->initialized = false;
}

}  // namespace engine::editor
