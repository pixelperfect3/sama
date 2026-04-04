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

#include "editor/EditorLog.h"
#include "editor/EditorState.h"
#include "editor/gizmo/GizmoRenderer.h"
#include "editor/gizmo/TransformGizmo.h"
#include "editor/inspectors/LightInspector.h"
#include "editor/inspectors/MaterialInspector.h"
#include "editor/inspectors/NameInspector.h"
#include "editor/inspectors/TransformInspector.h"
#include "editor/panels/AssetBrowserPanel.h"
#include "editor/panels/ConsolePanel.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/PropertiesPanel.h"
#include "editor/platform/IEditorWindow.h"
#include "editor/platform/cocoa/CocoaConsoleView.h"
#include "editor/platform/cocoa/CocoaEditorWindow.h"
#include "editor/platform/cocoa/CocoaHierarchyView.h"
#include "editor/platform/cocoa/CocoaPropertiesView.h"
#include "editor/undo/CommandStack.h"
#include "editor/undo/CreateEntityCommand.h"
#include "editor/undo/DeleteEntityCommand.h"
#include "editor/undo/SetTransformCommand.h"
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
#include "engine/scene/SceneSerializer.h"
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
    bgfx::TextureHandle dummyShadowTex = BGFX_INVALID_HANDLE;  // 1x1 depth for shadow sampler

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

    // Asset browser
    std::unique_ptr<AssetBrowserPanel> assetBrowserPanel;

    // Console
    std::unique_ptr<ConsolePanel> consolePanel;

    // Gizmo
    std::unique_ptr<TransformGizmo> gizmo;
    GizmoRenderer gizmoRenderer;

    // Undo/redo
    CommandStack commandStack;

    // Scene serialization
    scene::SceneSerializer sceneSerializer;

    // Status message (shown briefly in HUD)
    char statusMsg[128] = {};
    float statusTimer = 0.0f;

    // Add component menu
    bool addComponentMenuOpen = false;

    // Selection highlight material
    uint32_t selectionMatId = 0;

    // Frame arena
    std::unique_ptr<engine::memory::FrameArena> frameArena;

    // Frame timing
    double prevTime = 0.0;
    uint16_t fbW = 0;
    uint16_t fbH = 0;
    bool initialized = false;

    // Dirty flags for native panel updates.
    bool hierarchyDirty = true;
    bool propertiesDirty = true;
    size_t lastLogCount = 0;
    EntityID lastSelectedEntity = INVALID_ENTITY;

    // Build entity info list for the hierarchy panel.
    void refreshHierarchyView();

    // Build property fields for the properties panel.
    void refreshPropertiesView();

    // Sync console log to the native console view.
    void syncConsoleView();
};

void EditorApp::Impl::refreshHierarchyView()
{
    auto* hView = window->hierarchyView();
    if (!hView)
        return;

    std::vector<CocoaHierarchyView::EntityInfo> entities;
    registry.forEachEntity(
        [&](EntityID e)
        {
            CocoaHierarchyView::EntityInfo info;
            info.entityId = e;

            const auto* name = registry.get<engine::scene::NameComponent>(e);
            if (name && !name->name.empty())
            {
                info.name = name->name;
            }
            else
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "Entity #%u", entityIndex(e));
                info.name = buf;
            }

            // Build tags string.
            std::string tags;
            if (registry.has<TransformComponent>(e))
                tags += "[T]";
            if (registry.has<MeshComponent>(e))
                tags += "[M]";
            if (registry.has<MaterialComponent>(e))
                tags += "[Mat]";
            if (registry.has<DirectionalLightComponent>(e))
                tags += "[DL]";
            if (registry.has<PointLightComponent>(e))
                tags += "[PL]";
            if (registry.has<CameraComponent>(e))
                tags += "[Cam]";
            info.tags = tags;

            entities.push_back(std::move(info));
        });

    hView->setEntities(entities);
    hView->setSelectedEntity(editorState.primarySelection());
    hierarchyDirty = false;
}

void EditorApp::Impl::refreshPropertiesView()
{
    auto* pView = window->propertiesView();
    if (!pView)
        return;

    EntityID entity = editorState.primarySelection();
    if (entity == INVALID_ENTITY)
    {
        pView->clear("No entity selected");
        propertiesDirty = false;
        lastSelectedEntity = entity;
        return;
    }

    std::vector<CocoaPropertiesView::PropertyField> fields;

    // Entity name / ID header.
    {
        CocoaPropertiesView::PropertyField hdr;
        hdr.type = CocoaPropertiesView::PropertyField::Type::Header;
        const auto* name = registry.get<engine::scene::NameComponent>(entity);
        if (name && !name->name.empty())
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s (id:%u)", name->name.c_str(), entityIndex(entity));
            hdr.label = buf;
        }
        else
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "Entity #%u", entityIndex(entity));
            hdr.label = buf;
        }
        fields.push_back(hdr);
    }

    // Transform inspector.
    auto* tc = registry.get<TransformComponent>(entity);
    if (tc)
    {
        {
            CocoaPropertiesView::PropertyField hdr;
            hdr.type = CocoaPropertiesView::PropertyField::Type::Header;
            hdr.label = "Transform";
            fields.push_back(hdr);
        }

        glm::vec3 euler = glm::degrees(glm::eulerAngles(tc->rotation));

        const char* posLabels[] = {"Pos X", "Pos Y", "Pos Z"};
        float posVals[] = {tc->position.x, tc->position.y, tc->position.z};
        for (int i = 0; i < 3; ++i)
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = posLabels[i];
            f.value = posVals[i];
            f.fieldId = 100 + i;
            fields.push_back(f);
        }

        const char* rotLabels[] = {"Rot X", "Rot Y", "Rot Z"};
        float rotVals[] = {euler.x, euler.y, euler.z};
        for (int i = 0; i < 3; ++i)
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = rotLabels[i];
            f.value = rotVals[i];
            f.fieldId = 103 + i;
            fields.push_back(f);
        }

        const char* sclLabels[] = {"Scale X", "Scale Y", "Scale Z"};
        float sclVals[] = {tc->scale.x, tc->scale.y, tc->scale.z};
        for (int i = 0; i < 3; ++i)
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = sclLabels[i];
            f.value = sclVals[i];
            f.fieldId = 106 + i;
            fields.push_back(f);
        }
    }

    // Material inspector.
    auto* mc = registry.get<MaterialComponent>(entity);
    if (mc)
    {
        Material* mat = resources.getMaterialMut(mc->material);
        if (mat)
        {
            {
                CocoaPropertiesView::PropertyField hdr;
                hdr.type = CocoaPropertiesView::PropertyField::Type::Header;
                hdr.label = "Material";
                fields.push_back(hdr);
            }

            // Albedo color.
            {
                CocoaPropertiesView::PropertyField f;
                f.type = CocoaPropertiesView::PropertyField::Type::ColorField;
                f.label = "Albedo";
                f.color[0] = mat->albedo.x;
                f.color[1] = mat->albedo.y;
                f.color[2] = mat->albedo.z;
                f.fieldId = 200;
                fields.push_back(f);
            }

            // Roughness slider.
            {
                CocoaPropertiesView::PropertyField f;
                f.type = CocoaPropertiesView::PropertyField::Type::SliderField;
                f.label = "Rough";
                f.value = mat->roughness;
                f.minVal = 0.0f;
                f.maxVal = 1.0f;
                f.fieldId = 201;
                fields.push_back(f);
            }

            // Metallic slider.
            {
                CocoaPropertiesView::PropertyField f;
                f.type = CocoaPropertiesView::PropertyField::Type::SliderField;
                f.label = "Metal";
                f.value = mat->metallic;
                f.minVal = 0.0f;
                f.maxVal = 1.0f;
                f.fieldId = 202;
                fields.push_back(f);
            }

            // Emissive.
            {
                CocoaPropertiesView::PropertyField f;
                f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
                f.label = "Emiss";
                f.value = mat->emissiveScale;
                f.fieldId = 203;
                fields.push_back(f);
            }
        }
    }

    // Directional light.
    auto* dl = registry.get<DirectionalLightComponent>(entity);
    if (dl)
    {
        {
            CocoaPropertiesView::PropertyField hdr;
            hdr.type = CocoaPropertiesView::PropertyField::Type::Header;
            hdr.label = "Directional Light";
            fields.push_back(hdr);
        }

        const char* dirLabels[] = {"Dir X", "Dir Y", "Dir Z"};
        float dirVals[] = {dl->direction.x, dl->direction.y, dl->direction.z};
        for (int i = 0; i < 3; ++i)
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = dirLabels[i];
            f.value = dirVals[i];
            f.fieldId = 300 + i;
            fields.push_back(f);
        }

        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = "Intens";
            f.value = dl->intensity;
            f.fieldId = 303;
            fields.push_back(f);
        }
    }

    // Point light.
    auto* pl = registry.get<PointLightComponent>(entity);
    if (pl)
    {
        {
            CocoaPropertiesView::PropertyField hdr;
            hdr.type = CocoaPropertiesView::PropertyField::Type::Header;
            hdr.label = "Point Light";
            fields.push_back(hdr);
        }

        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = "Intens";
            f.value = pl->intensity;
            f.fieldId = 400;
            fields.push_back(f);
        }

        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = "Radius";
            f.value = pl->radius;
            f.fieldId = 401;
            fields.push_back(f);
        }
    }

    pView->setProperties(fields);
    propertiesDirty = false;
    lastSelectedEntity = entity;
}

void EditorApp::Impl::syncConsoleView()
{
    auto* cView = window->consoleView();
    if (!cView)
        return;

    size_t currentCount = EditorLog::instance().entryCount();
    if (currentCount <= lastLogCount)
        return;

    // We need to send only new entries. Since EditorLog is a ring buffer,
    // collect all and send those beyond what we've already synced.
    struct LogEntry
    {
        LogLevel level;
        char message[256];
    };
    std::vector<LogEntry> allEntries;
    EditorLog::instance().forEach(
        [&](const EditorLog::Entry& entry)
        {
            LogEntry le;
            le.level = entry.level;
            memcpy(le.message, entry.message, sizeof(le.message));
            allEntries.push_back(le);
        });

    // If total count is small enough, we know exactly which are new.
    size_t startIdx = 0;
    if (lastLogCount < allEntries.size())
    {
        startIdx = lastLogCount;
    }

    for (size_t i = startIdx; i < allEntries.size(); ++i)
    {
        CocoaConsoleView::MessageLevel mlevel;
        switch (allEntries[i].level)
        {
            case LogLevel::Warning:
                mlevel = CocoaConsoleView::MessageLevel::Warning;
                break;
            case LogLevel::Error:
                mlevel = CocoaConsoleView::MessageLevel::Error;
                break;
            default:
                mlevel = CocoaConsoleView::MessageLevel::Info;
                break;
        }
        cView->appendMessage(mlevel, allEntries[i].message);
    }

    lastLogCount = currentCount;
}

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
    init.resolution.width = impl_->window->viewportFramebufferWidth();
    init.resolution.height = impl_->window->viewportFramebufferHeight();
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init))
    {
        fprintf(stderr, "EditorApp: bgfx::init() failed\n");
        return false;
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);

    impl_->fbW = static_cast<uint16_t>(impl_->window->viewportFramebufferWidth());
    impl_->fbH = static_cast<uint16_t>(impl_->window->viewportFramebufferHeight());

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

        // 1x1 dummy shadow map so the PBR shader has a valid sampler.
        const uint8_t kWhiteDepth[4] = {255, 255, 255, 255};
        impl_->dummyShadowTex =
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                  bgfx::copy(kWhiteDepth, sizeof(kWhiteDepth)));
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
    // Wire native hierarchy view selection callback.
    impl_->window->hierarchyView()->setSelectionCallback(
        [this](uint64_t entityId)
        {
            impl_->editorState.select(static_cast<EntityID>(entityId));
            impl_->propertiesDirty = true;
        });

    // Wire selection change to mark properties dirty.
    impl_->editorState.setSelectionChangedCallback(
        [this]()
        {
            impl_->propertiesDirty = true;
            // Also update hierarchy selection highlight.
            auto* hView = impl_->window->hierarchyView();
            if (hView)
            {
                hView->setSelectedEntity(impl_->editorState.primarySelection());
            }
        });

    impl_->hierarchyPanel =
        std::make_unique<HierarchyPanel>(impl_->registry, impl_->editorState, *impl_->window);
    impl_->hierarchyPanel->init();

    impl_->propertiesPanel =
        std::make_unique<PropertiesPanel>(impl_->registry, impl_->editorState, *impl_->window);
    impl_->propertiesPanel->addInspector(std::make_unique<NameInspector>(*impl_->window));
    impl_->propertiesPanel->addInspector(std::make_unique<TransformInspector>(*impl_->window));
    impl_->propertiesPanel->addInspector(
        std::make_unique<MaterialInspector>(*impl_->window, impl_->resources));
    impl_->propertiesPanel->addInspector(std::make_unique<LightInspector>(*impl_->window));
    impl_->propertiesPanel->init();

    impl_->assetBrowserPanel =
        std::make_unique<AssetBrowserPanel>(impl_->registry, impl_->editorState, *impl_->window);
    impl_->assetBrowserPanel->setAssetDirectory("assets");
    impl_->assetBrowserPanel->setVisible(false);  // hidden by default, toggle with Tab
    impl_->assetBrowserPanel->init();

    impl_->consolePanel = std::make_unique<ConsolePanel>();
    impl_->consolePanel->setVisible(false);  // hidden by default, toggle with ~
    impl_->consolePanel->init();

    impl_->gizmo =
        std::make_unique<TransformGizmo>(impl_->registry, impl_->editorState, *impl_->window);
    impl_->gizmoRenderer.init();

    // -- Camera ---------------------------------------------------------------
    impl_->camera.distance = 5.0f;
    impl_->camera.yaw = 45.0f;
    impl_->camera.pitch = 25.0f;
    impl_->camera.target = {0.0f, 0.5f, 0.0f};

    // -- Scene serializer -----------------------------------------------------
    impl_->sceneSerializer.registerEngineComponents();

    // -- Frame arena ----------------------------------------------------------
    impl_->frameArena = std::make_unique<engine::memory::FrameArena>(2 * 1024 * 1024);

    // -- Timing ---------------------------------------------------------------
    impl_->prevTime =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    // Flush initial resource uploads.
    bgfx::frame();

    EditorLog::instance().info("Sama Editor initialized");

    // Initial native panel refresh.
    impl_->hierarchyDirty = true;
    impl_->propertiesDirty = true;

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
        uint32_t fbW = impl_->window->viewportFramebufferWidth();
        uint32_t fbH = impl_->window->viewportFramebufferHeight();
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

        // -- Camera (only when mouse is over viewport) ------------------------
        if (impl_->window->isMouseOverViewport() && impl_->window->isRightMouseDown())
        {
            impl_->camera.orbit(static_cast<float>(impl_->window->mouseDeltaX()),
                                -static_cast<float>(impl_->window->mouseDeltaY()), 0.25f);
        }

        if (impl_->window->isMouseOverViewport())
        {
            double scrollY = impl_->window->scrollDeltaY();
            if (std::abs(scrollY) > 0.01)
            {
                impl_->camera.zoom(static_cast<float>(scrollY * 0.1), 1.0f, 1.0f, 100.0f);
            }
        }

        // -- Gizmo update (before transform system) ---------------------------
        {
            float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
            glm::mat4 gView = impl_->camera.view();
            glm::mat4 gProj = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 200.0f);
            impl_->gizmo->update(dt, gView, gProj);
        }

        // -- Update properties in real-time during gizmo drag -----------------
        if (impl_->gizmo->isDragging())
        {
            impl_->propertiesDirty = true;
        }

        // -- Gizmo undo command on drag-end ----------------------------------
        if (impl_->gizmo->dragJustEnded())
        {
            EntityID selE = impl_->editorState.primarySelection();
            if (selE != INVALID_ENTITY)
            {
                auto* tc = impl_->registry.get<TransformComponent>(selE);
                if (tc)
                {
                    auto cmd = std::make_unique<SetTransformCommand>(
                        impl_->registry, selE, impl_->gizmo->dragStartTransform(), *tc);
                    impl_->commandStack.execute(std::move(cmd));
                    impl_->propertiesDirty = true;
                }
            }
        }

        // -- Undo/Redo keyboard shortcuts -----------------------------------
        if (impl_->window->isKeyPressed('Z') && impl_->window->isCommandDown())
        {
            if (impl_->window->isShiftDown())
            {
                const char* desc = impl_->commandStack.redoDescription();
                impl_->commandStack.redo();
                char buf[128];
                snprintf(buf, sizeof(buf), "Redo: %s", desc);
                EditorLog::instance().info(buf);
            }
            else
            {
                const char* desc = impl_->commandStack.undoDescription();
                impl_->commandStack.undo();
                char buf[128];
                snprintf(buf, sizeof(buf), "Undo: %s", desc);
                EditorLog::instance().info(buf);
            }
            impl_->hierarchyDirty = true;
            impl_->propertiesDirty = true;
        }

        // -- Keyboard shortcuts -----------------------------------------------
        // Cmd+S = save scene
        if (impl_->window->isKeyPressed('S') && impl_->window->isCommandDown())
        {
            if (impl_->sceneSerializer.saveScene(impl_->registry, impl_->resources,
                                                 "editor_scene.json"))
            {
                snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                         "Scene saved to editor_scene.json");
                EditorLog::instance().info("Scene saved to editor_scene.json");
            }
            else
            {
                snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Failed to save scene!");
                EditorLog::instance().error("Failed to save scene!");
            }
            impl_->statusTimer = 3.0f;
        }

        // Cmd+N = create new entity
        if (impl_->window->isKeyPressed('N') && impl_->window->isCommandDown())
        {
            auto cmd = std::make_unique<CreateEntityCommand>(impl_->registry, impl_->editorState);
            impl_->commandStack.execute(std::move(cmd));
            snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Created new entity");
            impl_->statusTimer = 2.0f;
            EditorLog::instance().info("Created new entity");
            impl_->hierarchyDirty = true;
            impl_->propertiesDirty = true;
        }

        // Delete/Backspace = delete selected entity
        if (impl_->window->isKeyPressed(0x08) || impl_->window->isKeyPressed(0x7F))
        {
            EntityID selE = impl_->editorState.primarySelection();
            if (selE != INVALID_ENTITY)
            {
                auto cmd = std::make_unique<DeleteEntityCommand>(impl_->registry,
                                                                 impl_->editorState, selE);
                impl_->commandStack.execute(std::move(cmd));
                snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Deleted entity");
                impl_->statusTimer = 2.0f;
                EditorLog::instance().info("Deleted entity");
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
            }
        }

        // A = toggle add component menu
        if (impl_->window->isKeyPressed('A') && !impl_->window->isCommandDown())
        {
            EntityID selE = impl_->editorState.primarySelection();
            if (selE != INVALID_ENTITY)
            {
                impl_->addComponentMenuOpen = !impl_->addComponentMenuOpen;
            }
        }

        // Add component menu: number keys select component type.
        if (impl_->addComponentMenuOpen)
        {
            EntityID selE = impl_->editorState.primarySelection();
            if (selE != INVALID_ENTITY)
            {
                if (impl_->window->isKeyPressed('1'))
                {
                    if (!impl_->registry.has<DirectionalLightComponent>(selE))
                    {
                        DirectionalLightComponent dl{};
                        dl.direction = {0.0f, -1.0f, 0.0f};
                        dl.color = {1.0f, 1.0f, 1.0f};
                        dl.intensity = 1.0f;
                        dl.flags = 0;
                        impl_->registry.emplace<DirectionalLightComponent>(selE, dl);
                        snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                 "Added DirectionalLight");
                        impl_->statusTimer = 2.0f;
                        impl_->propertiesDirty = true;
                        impl_->hierarchyDirty = true;
                    }
                    impl_->addComponentMenuOpen = false;
                }
                if (impl_->window->isKeyPressed('2'))
                {
                    if (!impl_->registry.has<PointLightComponent>(selE))
                    {
                        PointLightComponent pl{};
                        pl.color = {1.0f, 1.0f, 1.0f};
                        pl.intensity = 1.0f;
                        pl.radius = 10.0f;
                        impl_->registry.emplace<PointLightComponent>(selE, pl);
                        snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Added PointLight");
                        impl_->statusTimer = 2.0f;
                        impl_->propertiesDirty = true;
                        impl_->hierarchyDirty = true;
                    }
                    impl_->addComponentMenuOpen = false;
                }
                if (impl_->window->isKeyPressed('3'))
                {
                    if (!impl_->registry.has<MeshComponent>(selE))
                    {
                        impl_->registry.emplace<MeshComponent>(selE,
                                                               MeshComponent{impl_->cubeMeshId});
                        snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                 "Added MeshComponent (cube)");
                        impl_->statusTimer = 2.0f;
                        impl_->propertiesDirty = true;
                        impl_->hierarchyDirty = true;
                    }
                    impl_->addComponentMenuOpen = false;
                }
                if (impl_->window->isKeyPressed(0x1B))  // Escape
                {
                    impl_->addComponentMenuOpen = false;
                }
            }
            else
            {
                impl_->addComponentMenuOpen = false;
            }
        }

        // ~ (backtick) = toggle console (no-op for now, console always visible in panel)
        // Tab = toggle asset browser (kept as debug text for now since it's rarely used)

        // Decrement status timer.
        if (impl_->statusTimer > 0.0f)
        {
            impl_->statusTimer -= dt;
        }

        // -- Transform system -------------------------------------------------
        impl_->transformSys.update(impl_->registry);

        // -- Render -----------------------------------------------------------
        const auto W = impl_->fbW;
        const auto H = impl_->fbH;
        float aspect = static_cast<float>(W) / static_cast<float>(H);

        glm::mat4 view = impl_->camera.view();
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 200.0f);

        // Touch views 1..8 with minimal 1x1 clear to prevent pink artifacts.
        bgfx::setViewRect(0, 0, 0, W, H);
        bgfx::setViewClear(0, BGFX_CLEAR_NONE);
        bgfx::touch(0);
        for (bgfx::ViewId v = 1; v < kViewOpaque; ++v)
        {
            bgfx::setViewRect(v, 0, 0, 1, 1);
            bgfx::setViewClear(v, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000FF, 1.0f, 0);
            bgfx::touch(v);
        }

        // Main scene on kViewOpaque (view 9).
        bgfx::setViewRect(kViewOpaque, 0, 0, W, H);
        bgfx::setViewClear(kViewOpaque, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030FF, 1.0f, 0);
        bgfx::setViewTransform(kViewOpaque, glm::value_ptr(view), glm::value_ptr(proj));

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
        frame.shadowAtlas = impl_->dummyShadowTex;
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
                        glm::mat4 outlineMtx = glm::scale(wt->matrix, glm::vec3(1.02f));
                        bgfx::setTransform(glm::value_ptr(outlineMtx));

                        const float matData[8] = {
                            1.0f, 0.8f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        };
                        bgfx::setUniform(impl_->uniforms.u_material, matData, 2);
                        bgfx::setUniform(impl_->uniforms.u_dirLight, lightData, 2);

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
                float gAspect = static_cast<float>(W) / static_cast<float>(H);
                glm::mat4 gView = impl_->camera.view();
                glm::mat4 gProj = glm::perspective(glm::radians(45.0f), gAspect, 0.05f, 200.0f);
                impl_->gizmoRenderer.render(*impl_->gizmo, gView, gProj, W, H);
            }
        }

        // -- Native panel updates (dirty-flag guarded) ------------------------
        if (impl_->hierarchyDirty)
        {
            impl_->refreshHierarchyView();
        }
        if (impl_->propertiesDirty ||
            impl_->editorState.primarySelection() != impl_->lastSelectedEntity)
        {
            impl_->refreshPropertiesView();
        }
        impl_->syncConsoleView();

        // -- HUD (viewport overlay only: FPS, gizmo mode, shortcuts) ----------
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(1, 1, 0x0f, "Sama Editor  |  %.1f fps  |  %.3f ms",
                            dt > 0.0f ? 1.0f / dt : 0.0f, dt * 1000.0f);
        const char* modeStr = "Translate";
        if (impl_->gizmo->mode() == GizmoMode::Rotate)
            modeStr = "Rotate";
        else if (impl_->gizmo->mode() == GizmoMode::Scale)
            modeStr = "Scale";
        bgfx::dbgTextPrintf(
            1, 2, 0x07,
            "Right-drag=orbit  Scroll=zoom  W/E/R=gizmo [%s]  Cmd+Z=undo  Cmd+Shift+Z=redo",
            modeStr);

        // Status message.
        if (impl_->statusTimer > 0.0f)
        {
            bgfx::dbgTextPrintf(40, 1, 0x0a, "%s", impl_->statusMsg);
        }

        // Add component menu (still as HUD overlay on viewport).
        if (impl_->addComponentMenuOpen)
        {
            bgfx::dbgTextPrintf(2, 4, 0x0f, "--- Add Component ---");
            bgfx::dbgTextPrintf(2, 5, 0x07, "1) DirectionalLight");
            bgfx::dbgTextPrintf(2, 6, 0x07, "2) PointLight");
            bgfx::dbgTextPrintf(2, 7, 0x07, "3) Mesh (cube)");
            bgfx::dbgTextPrintf(2, 8, 0x08, "Esc to cancel");
        }

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
    if (impl_->assetBrowserPanel)
    {
        impl_->assetBrowserPanel->shutdown();
    }
    if (impl_->consolePanel)
    {
        impl_->consolePanel->shutdown();
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
    if (bgfx::isValid(impl_->dummyShadowTex))
        bgfx::destroy(impl_->dummyShadowTex);

    impl_->resources.destroyAll();
    impl_->uniforms.destroy();

    bgfx::frame();
    bgfx::shutdown();

    impl_->window->shutdown();
    impl_->initialized = false;
}

}  // namespace engine::editor
