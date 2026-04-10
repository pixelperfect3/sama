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
#include "editor/inspectors/ColliderInspector.h"
#include "editor/inspectors/LightInspector.h"
#include "editor/inspectors/MaterialInspector.h"
#include "editor/inspectors/NameInspector.h"
#include "editor/inspectors/RigidBodyInspector.h"
#include "editor/inspectors/TransformInspector.h"
#include "editor/panels/AssetBrowserPanel.h"
#include "editor/panels/ConsolePanel.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/PropertiesPanel.h"
#include "editor/panels/ResourcePanel.h"
#include "editor/platform/IEditorWindow.h"
#include "editor/platform/cocoa/CocoaConsoleView.h"
#include "editor/platform/cocoa/CocoaEditorWindow.h"
#include "editor/platform/cocoa/CocoaHierarchyView.h"
#include "editor/platform/cocoa/CocoaPropertiesView.h"
#include "editor/platform/cocoa/CocoaResourceView.h"
#include "editor/undo/CommandStack.h"
#include "editor/undo/CreateEntityCommand.h"
#include "editor/undo/DeleteEntityCommand.h"
#include "editor/undo/SetTransformCommand.h"
#include "engine/assets/GltfAsset.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/GltfSceneSpawner.h"
#include "engine/assets/ObjLoader.h"
#include "engine/assets/StdFileSystem.h"
#include "engine/core/OrbitCamera.h"
#include "engine/ecs/Registry.h"
#include "engine/memory/FrameArena.h"
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsSystem.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/NameComponent.h"
#include "engine/scene/SceneGraph.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/TransformSystem.h"
#include "engine/threading/ThreadPool.h"

using engine::ecs::EntityID;
using engine::ecs::INVALID_ENTITY;
using namespace engine::core;
using namespace engine::ecs;
using namespace engine::rendering;

namespace engine::editor
{

// Static pointer to the pending menu action string in EditorApp::Impl.
// Set during init(); the menu callback writes the action name here,
// and the run loop reads it each frame.
static std::string* s_pendingMenuAction = nullptr;

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

    // Physics (stepped only in Play mode).
    engine::physics::JoltPhysicsEngine physics;
    engine::physics::PhysicsSystem physicsSys;

    // Reset all Jolt bodies and clear ECS body state so PhysicsSystem will
    // recreate fresh bodies from authored components on the next update().
    // Used on Editing<->Play transitions because Jolt's internal state
    // (velocities, sleep, contacts) cannot be cleanly snapshotted.
    void resetPhysicsBodies();

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

    // Resource inspector
    ResourcePanel resourcePanel;

    // Gizmo
    std::unique_ptr<TransformGizmo> gizmo;
    GizmoRenderer gizmoRenderer;

    // Undo/redo
    CommandStack commandStack;

    // Asset management (needed for scene deserialization).
    std::unique_ptr<engine::threading::ThreadPool> threadPool;
    std::unique_ptr<engine::assets::StdFileSystem> fileSystem;
    std::unique_ptr<engine::assets::AssetManager> assetManager;

    // Scene serialization
    scene::SceneSerializer sceneSerializer;
    std::string currentScenePath;

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

    // Viewport click-to-select tracking.
    bool prevLeftDown = false;

    // Dirty flags for native panel updates.
    bool hierarchyDirty = true;
    bool propertiesDirty = true;
    size_t lastLogCount = 0;
    EntityID lastSelectedEntity = INVALID_ENTITY;

    // Menu action pending from native menu click (set by static callback).
    std::string pendingMenuAction;

    // Build entity info list for the hierarchy panel.
    void refreshHierarchyView();

    // Build property fields for the properties panel.
    void refreshPropertiesView();

    // Sync console log to the native console view.
    void syncConsoleView();

    // Add a component to the currently primary-selected entity. Single source
    // of truth shared by the keyboard 'A' menu, the macOS Component menu, and
    // the AppKit Properties panel "+ Add Component" button. Returns true if a
    // component was actually added (selection valid + component not already
    // present). `type` is one of: "directional_light", "point_light", "mesh",
    // "rigid_body", "box_collider".
    bool addComponentToSelection(const std::string& type);
};

void EditorApp::Impl::resetPhysicsBodies()
{
    // Destroy all Jolt-side bodies in one pass.
    physics.destroyAllBodies();

    // Clear ECS-side body state: reset bodyID sentinels and strip the
    // PhysicsBodyCreatedTag so PhysicsSystem::registerNewBodies re-creates
    // bodies from scratch on the next update().
    registry.view<engine::physics::RigidBodyComponent>().each(
        [&](EntityID e, engine::physics::RigidBodyComponent& rb)
        {
            rb.bodyID = ~0u;
            if (registry.has<engine::physics::PhysicsBodyCreatedTag>(e))
            {
                registry.remove<engine::physics::PhysicsBodyCreatedTag>(e);
            }
        });
}

bool EditorApp::Impl::addComponentToSelection(const std::string& type)
{
    EntityID selE = editorState.primarySelection();
    if (selE == INVALID_ENTITY)
    {
        snprintf(statusMsg, sizeof(statusMsg), "No entity selected");
        statusTimer = 2.0f;
        return false;
    }

    auto setStatus = [&](const char* msg)
    {
        snprintf(statusMsg, sizeof(statusMsg), "%s", msg);
        statusTimer = 2.0f;
    };

    if (type == "directional_light")
    {
        if (registry.has<DirectionalLightComponent>(selE))
        {
            setStatus("DirectionalLight already present");
            return false;
        }
        DirectionalLightComponent dl{};
        dl.direction = {0.0f, -1.0f, 0.0f};
        dl.color = {1.0f, 1.0f, 1.0f};
        dl.intensity = 1.0f;
        dl.flags = 0;
        registry.emplace<DirectionalLightComponent>(selE, dl);
        setStatus("Added DirectionalLight");
    }
    else if (type == "point_light")
    {
        if (registry.has<PointLightComponent>(selE))
        {
            setStatus("PointLight already present");
            return false;
        }
        PointLightComponent pl{};
        pl.color = {1.0f, 1.0f, 1.0f};
        pl.intensity = 1.0f;
        pl.radius = 10.0f;
        registry.emplace<PointLightComponent>(selE, pl);
        setStatus("Added PointLight");
    }
    else if (type == "mesh")
    {
        if (registry.has<MeshComponent>(selE))
        {
            setStatus("MeshComponent already present");
            return false;
        }
        registry.emplace<MeshComponent>(selE, MeshComponent{cubeMeshId});
        setStatus("Added MeshComponent (cube)");
    }
    else if (type == "rigid_body")
    {
        if (registry.has<engine::physics::RigidBodyComponent>(selE))
        {
            setStatus("RigidBody already present");
            return false;
        }
        registry.emplace<engine::physics::RigidBodyComponent>(
            selE, engine::physics::RigidBodyComponent{});
        setStatus("Added RigidBody");
    }
    else if (type == "box_collider")
    {
        if (registry.has<engine::physics::ColliderComponent>(selE))
        {
            setStatus("Collider already present");
            return false;
        }
        engine::physics::ColliderComponent cc{};
        cc.shape = engine::physics::ColliderShape::Box;
        cc.halfExtents = {0.5f, 0.5f, 0.5f};
        registry.emplace<engine::physics::ColliderComponent>(selE, cc);
        setStatus("Added Box Collider");
    }
    else
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Unknown component type: %s", type.c_str());
        setStatus(buf);
        return false;
    }

    propertiesDirty = true;
    hierarchyDirty = true;
    return true;
}

void EditorApp::Impl::refreshHierarchyView()
{
    auto* hView = window->hierarchyView();
    if (!hView)
        return;

    std::vector<CocoaHierarchyView::EntityInfo> entities;

    // Helper to build an EntityInfo for a single entity.
    auto buildInfo = [&](EntityID e, uint32_t depth) -> CocoaHierarchyView::EntityInfo
    {
        CocoaHierarchyView::EntityInfo info;
        info.entityId = e;
        info.depth = depth;

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

        return info;
    };

    // Collect root entities (those without a HierarchyComponent / no parent).
    std::vector<EntityID> roots;
    registry.forEachEntity(
        [&](EntityID e)
        {
            if (!registry.has<engine::scene::HierarchyComponent>(e))
                roots.push_back(e);
        });

    // DFS walk to build hierarchy-ordered list with depth.
    std::function<void(EntityID, uint32_t)> walk = [&](EntityID e, uint32_t depth)
    {
        entities.push_back(buildInfo(e, depth));

        const auto* cc = registry.get<engine::scene::ChildrenComponent>(e);
        if (cc)
        {
            for (EntityID child : cc->children)
            {
                walk(child, depth + 1);
            }
        }
    };

    for (EntityID root : roots)
        walk(root, 0);

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

    // Rigid body.
    auto* rb = registry.get<engine::physics::RigidBodyComponent>(entity);
    if (rb)
    {
        {
            CocoaPropertiesView::PropertyField hdr;
            hdr.type = CocoaPropertiesView::PropertyField::Type::Header;
            hdr.label = "Rigid Body";
            fields.push_back(hdr);
        }
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::DropdownField;
            f.label = "Type";
            f.options = {"Static", "Dynamic", "Kinematic"};
            f.currentIndex = static_cast<int>(rb->type);
            f.fieldId = 505;
            fields.push_back(f);
        }
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = "Mass";
            f.value = rb->mass;
            f.fieldId = 500;
            fields.push_back(f);
        }
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = "LinDamp";
            f.value = rb->linearDamping;
            f.fieldId = 501;
            fields.push_back(f);
        }
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = "AngDamp";
            f.value = rb->angularDamping;
            f.fieldId = 502;
            fields.push_back(f);
        }
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = "Friction";
            f.value = rb->friction;
            f.fieldId = 503;
            fields.push_back(f);
        }
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = "Restit";
            f.value = rb->restitution;
            f.fieldId = 504;
            fields.push_back(f);
        }
    }

    // Collider.
    auto* cc = registry.get<engine::physics::ColliderComponent>(entity);
    if (cc)
    {
        {
            CocoaPropertiesView::PropertyField hdr;
            hdr.type = CocoaPropertiesView::PropertyField::Type::Header;
            hdr.label = "Collider";
            fields.push_back(hdr);
        }
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::DropdownField;
            f.label = "Shape";
            f.options = {"Box", "Sphere", "Capsule", "Mesh"};
            f.currentIndex = static_cast<int>(cc->shape);
            f.fieldId = 514;
            fields.push_back(f);
        }
        const char* extLabels[] = {"HalfX", "HalfY", "HalfZ"};
        float extVals[] = {cc->halfExtents.x, cc->halfExtents.y, cc->halfExtents.z};
        for (int i = 0; i < 3; ++i)
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = extLabels[i];
            f.value = extVals[i];
            f.fieldId = 510 + i;
            fields.push_back(f);
        }
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::FloatField;
            f.label = "Radius";
            f.value = cc->radius;
            f.fieldId = 513;
            fields.push_back(f);
        }
        {
            CocoaPropertiesView::PropertyField f;
            f.type = CocoaPropertiesView::PropertyField::Type::Label;
            f.label = cc->isSensor ? "Sensor: yes" : "Sensor: no";
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

    // Wire name editing in the hierarchy panel to update NameComponent.
    impl_->window->hierarchyView()->setNameChangedCallback(
        [this](uint64_t entityId, const char* newName)
        {
            if (impl_->editorState.playState() != EditorPlayState::Editing)
                return;
            auto eid = static_cast<EntityID>(entityId);
            auto* nc = impl_->registry.get<engine::scene::NameComponent>(eid);
            if (nc)
            {
                nc->name = newName;
            }
            else
            {
                impl_->registry.emplace<engine::scene::NameComponent>(
                    eid, engine::scene::NameComponent{newName});
            }
            impl_->hierarchyDirty = true;
            impl_->propertiesDirty = true;
            EditorLog::instance().info((std::string("Renamed entity to: ") + newName).c_str());
        });

    // Wire drag-and-drop reparenting in the hierarchy panel.
    impl_->window->hierarchyView()->setReparentCallback(
        [this](uint64_t childId, uint64_t parentId)
        {
            if (impl_->editorState.playState() != EditorPlayState::Editing)
                return;
            auto child = static_cast<EntityID>(childId);
            auto parent = static_cast<EntityID>(parentId);

            if (parent == INVALID_ENTITY)
            {
                engine::scene::detach(impl_->registry, child);
                EditorLog::instance().info("Detached entity from parent");
            }
            else
            {
                if (engine::scene::setParent(impl_->registry, child, parent))
                {
                    EditorLog::instance().info("Reparented entity");
                }
                else
                {
                    EditorLog::instance().error("Cannot reparent: would create a cycle");
                }
            }
            impl_->hierarchyDirty = true;
            impl_->propertiesDirty = true;
        });

    // Wire context menu: create child entity.
    impl_->window->hierarchyView()->setCreateChildCallback(
        [this](uint64_t parentId)
        {
            auto parent = static_cast<EntityID>(parentId);
            EntityID child = impl_->registry.createEntity();
            impl_->registry.emplace<engine::scene::NameComponent>(
                child, engine::scene::NameComponent{"New Child"});
            impl_->registry.emplace<TransformComponent>(child);
            engine::scene::setParent(impl_->registry, child, parent);
            impl_->editorState.select(child);
            impl_->hierarchyDirty = true;
            impl_->propertiesDirty = true;
            EditorLog::instance().info("Created child entity");
        });

    // Wire context menu: detach from parent.
    impl_->window->hierarchyView()->setDetachCallback(
        [this](uint64_t entityId)
        {
            auto eid = static_cast<EntityID>(entityId);
            engine::scene::detach(impl_->registry, eid);
            impl_->hierarchyDirty = true;
            impl_->propertiesDirty = true;
            EditorLog::instance().info("Detached entity from parent");
        });

    // Wire context menu: delete entity.
    impl_->window->hierarchyView()->setDeleteCallback(
        [this](uint64_t entityId)
        {
            auto eid = static_cast<EntityID>(entityId);
            engine::scene::destroyHierarchy(impl_->registry, eid);
            if (impl_->editorState.primarySelection() == eid)
            {
                impl_->editorState.select(INVALID_ENTITY);
            }
            impl_->hierarchyDirty = true;
            impl_->propertiesDirty = true;
            EditorLog::instance().info("Deleted entity and its children");
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

    // Wire native properties view value-changed callback.
    impl_->window->propertiesView()->setValueChangedCallback(
        [this](int fieldId, float value)
        {
            // Disallow user-initiated writes while the simulation is running;
            // PhysicsSystem::syncDynamicBodies would otherwise fight them.
            if (impl_->editorState.playState() != EditorPlayState::Editing)
                return;

            EntityID entity = impl_->editorState.primarySelection();
            if (entity == INVALID_ENTITY)
                return;

            auto* tc = impl_->registry.get<TransformComponent>(entity);
            switch (fieldId)
            {
                // Position X/Y/Z
                case 100:
                    if (tc)
                    {
                        tc->position.x = value;
                        tc->flags |= 1;
                    }
                    break;
                case 101:
                    if (tc)
                    {
                        tc->position.y = value;
                        tc->flags |= 1;
                    }
                    break;
                case 102:
                    if (tc)
                    {
                        tc->position.z = value;
                        tc->flags |= 1;
                    }
                    break;
                // Rotation X/Y/Z (euler degrees -> quaternion)
                case 103:
                case 104:
                case 105:
                    if (tc)
                    {
                        glm::vec3 euler = glm::degrees(glm::eulerAngles(tc->rotation));
                        euler[fieldId - 103] = value;
                        glm::vec3 rad = glm::radians(euler);
                        // Build quaternion from individual axis rotations to avoid
                        // gimbal lock issues with glm::quat(vec3).
                        tc->rotation = glm::quat(glm::vec3(rad.x, 0, 0)) *
                                       glm::quat(glm::vec3(0, rad.y, 0)) *
                                       glm::quat(glm::vec3(0, 0, rad.z));
                        tc->rotation = glm::normalize(tc->rotation);
                        tc->flags |= 1;
                    }
                    break;
                // Scale X/Y/Z
                case 106:
                    if (tc)
                    {
                        tc->scale.x = value;
                        tc->flags |= 1;
                    }
                    break;
                case 107:
                    if (tc)
                    {
                        tc->scale.y = value;
                        tc->flags |= 1;
                    }
                    break;
                case 108:
                    if (tc)
                    {
                        tc->scale.z = value;
                        tc->flags |= 1;
                    }
                    break;
                // Material roughness
                case 201:
                {
                    auto* mc = impl_->registry.get<MaterialComponent>(entity);
                    if (mc)
                    {
                        Material* mat = impl_->resources.getMaterialMut(mc->material);
                        if (mat)
                            mat->roughness = value;
                    }
                    break;
                }
                // Material metallic
                case 202:
                {
                    auto* mc = impl_->registry.get<MaterialComponent>(entity);
                    if (mc)
                    {
                        Material* mat = impl_->resources.getMaterialMut(mc->material);
                        if (mat)
                            mat->metallic = value;
                    }
                    break;
                }
                // Material emissive scale
                case 203:
                {
                    auto* mc = impl_->registry.get<MaterialComponent>(entity);
                    if (mc)
                    {
                        Material* mat = impl_->resources.getMaterialMut(mc->material);
                        if (mat)
                            mat->emissiveScale = value;
                    }
                    break;
                }
                // Directional light direction X/Y/Z
                case 300:
                case 301:
                case 302:
                {
                    auto* dl = impl_->registry.get<DirectionalLightComponent>(entity);
                    if (dl)
                        (&dl->direction.x)[fieldId - 300] = value;
                    break;
                }
                // Directional light intensity
                case 303:
                {
                    auto* dl = impl_->registry.get<DirectionalLightComponent>(entity);
                    if (dl)
                        dl->intensity = value;
                    break;
                }
                // Point light intensity
                case 400:
                {
                    auto* pl = impl_->registry.get<PointLightComponent>(entity);
                    if (pl)
                        pl->intensity = value;
                    break;
                }
                // Point light radius
                case 401:
                {
                    auto* pl = impl_->registry.get<PointLightComponent>(entity);
                    if (pl)
                        pl->radius = value;
                    break;
                }
                // Rigid body fields
                case 500:
                case 501:
                case 502:
                case 503:
                case 504:
                {
                    auto* rb = impl_->registry.get<engine::physics::RigidBodyComponent>(entity);
                    if (rb)
                    {
                        switch (fieldId)
                        {
                            case 500:
                                rb->mass = value;
                                break;
                            case 501:
                                rb->linearDamping = value;
                                break;
                            case 502:
                                rb->angularDamping = value;
                                break;
                            case 503:
                                rb->friction = value;
                                break;
                            case 504:
                                rb->restitution = value;
                                break;
                        }
                    }
                    break;
                }
                // Collider half-extents X/Y/Z, radius
                case 510:
                case 511:
                case 512:
                case 513:
                {
                    auto* cc = impl_->registry.get<engine::physics::ColliderComponent>(entity);
                    if (cc)
                    {
                        if (fieldId == 513)
                            cc->radius = value;
                        else
                            (&cc->halfExtents.x)[fieldId - 510] = value;
                    }
                    break;
                }
                default:
                    break;
            }
            // Don't set propertiesDirty here — rebuilding the panel while the user
            // is editing fires controlTextDidEndEditing on the old field, causing a
            // re-entrant callback loop that corrupts entity state.
        });

    // Wire native properties view color-changed callback.
    impl_->window->propertiesView()->setColorChangedCallback(
        [this](int fieldId, float r, float g, float b)
        {
            if (impl_->editorState.playState() != EditorPlayState::Editing)
                return;

            EntityID entity = impl_->editorState.primarySelection();
            if (entity == INVALID_ENTITY)
                return;

            if (fieldId == 200)
            {
                auto* mc = impl_->registry.get<MaterialComponent>(entity);
                if (mc)
                {
                    Material* mat = impl_->resources.getMaterialMut(mc->material);
                    if (mat)
                        mat->albedo = {r, g, b, mat->albedo.w};
                }
            }
            // Don't set propertiesDirty — same re-entrancy issue as above.
        });

    // Wire native properties view int-changed callback (dropdowns).
    impl_->window->propertiesView()->setIntChangedCallback(
        [this](int fieldId, int newIndex)
        {
            if (impl_->editorState.playState() != EditorPlayState::Editing)
                return;
            EntityID entity = impl_->editorState.primarySelection();
            if (entity == INVALID_ENTITY)
                return;

            if (fieldId == 505)
            {
                auto* rb = impl_->registry.get<engine::physics::RigidBodyComponent>(entity);
                if (rb && newIndex >= 0 && newIndex <= 2)
                {
                    rb->type = static_cast<engine::physics::BodyType>(newIndex);
                }
            }
            else if (fieldId == 514)
            {
                auto* cc = impl_->registry.get<engine::physics::ColliderComponent>(entity);
                if (cc && newIndex >= 0 && newIndex <= 3)
                {
                    cc->shape = static_cast<engine::physics::ColliderShape>(newIndex);
                }
            }
        });

    // Wire native properties view "+ Add Component" button.
    impl_->window->propertiesView()->setAddComponentCallback(
        [this](const std::string& type)
        {
            if (impl_->editorState.playState() != EditorPlayState::Editing)
                return;
            impl_->addComponentToSelection(type);
            // The add will set propertiesDirty; refreshPropertiesView() will
            // run on the next frame and rebuild the panel including the new
            // component's display block.
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
    impl_->propertiesPanel->addInspector(std::make_unique<RigidBodyInspector>(*impl_->window));
    impl_->propertiesPanel->addInspector(std::make_unique<ColliderInspector>(*impl_->window));
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

    // -- Asset manager (for scene loading) -------------------------------------
    impl_->threadPool = std::make_unique<engine::threading::ThreadPool>(2);
    impl_->fileSystem = std::make_unique<engine::assets::StdFileSystem>(".");
    impl_->assetManager =
        std::make_unique<engine::assets::AssetManager>(*impl_->threadPool, *impl_->fileSystem);
    impl_->assetManager->registerLoader(std::make_unique<engine::assets::GltfLoader>());
    impl_->assetManager->registerLoader(std::make_unique<engine::assets::ObjLoader>());

    // -- Scene serializer -----------------------------------------------------
    impl_->sceneSerializer.registerEngineComponents();

    // -- Frame arena ----------------------------------------------------------
    impl_->frameArena = std::make_unique<engine::memory::FrameArena>(2 * 1024 * 1024);

    // -- Physics --------------------------------------------------------------
    // Initialized once and shared across Play sessions.  The simulation is
    // only stepped while editorState.playState() == Playing; on Play/Stop
    // transitions we wipe all bodies so PhysicsSystem re-creates them fresh
    // from the authored components.
    if (!impl_->physics.init())
    {
        fprintf(stderr, "EditorApp: physics.init() failed\n");
    }

    // -- Timing ---------------------------------------------------------------
    impl_->prevTime =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    // Flush initial resource uploads.
    bgfx::frame();

    EditorLog::instance().info("Sama Editor initialized");

    // Initial native panel refresh.
    impl_->hierarchyDirty = true;
    impl_->propertiesDirty = true;

    // Wire native menu bar to EditorApp via static callback.
    s_pendingMenuAction = &impl_->pendingMenuAction;
    impl_->window->setMenuCallback(
        [](const char* action)
        {
            if (s_pendingMenuAction)
            {
                *s_pendingMenuAction = action;
            }
        });

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
        float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
        glm::mat4 viewMtx = impl_->camera.view();
        glm::mat4 projMtx = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 200.0f);
        impl_->gizmo->update(dt, viewMtx, projMtx);

        // -- Viewport click-to-select (ray-AABB picking) ----------------------
        // Only fire on left-click when mouse is over the viewport, gizmo is
        // not interacting, and right-button is not held (orbit).
        {
            bool leftPressed = impl_->window->isLeftMouseDown() && !impl_->prevLeftDown;
            if (leftPressed && impl_->window->isMouseOverViewport() &&
                !impl_->gizmo->isDragging() && impl_->gizmo->hoveredAxis() == GizmoAxis::None &&
                !impl_->window->isRightMouseDown())
            {
                // Build ray from mouse position.
                float scale = impl_->window->contentScale();
                float mx = static_cast<float>(impl_->window->mouseX()) * scale;
                float my = static_cast<float>(impl_->window->mouseY()) * scale;
                float fw = static_cast<float>(fbW);
                float fh = static_cast<float>(fbH);

                float ndcX = (2.0f * mx / fw) - 1.0f;
                float ndcY = 1.0f - (2.0f * my / fh);

                glm::mat4 invVP = glm::inverse(projMtx * viewMtx);
                glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                nearPt /= nearPt.w;
                farPt /= farPt.w;

                glm::vec3 rayO = glm::vec3(nearPt);
                glm::vec3 rayD = glm::normalize(glm::vec3(farPt - nearPt));

                // Test ray against every entity with Mesh + WorldTransform.
                float bestT = 1e30f;
                EntityID bestEntity = INVALID_ENTITY;

                auto meshView = impl_->registry.view<MeshComponent, WorldTransformComponent>();
                meshView.each(
                    [&](EntityID e, const MeshComponent& mc, const WorldTransformComponent& wt)
                    {
                        const Mesh* mesh = impl_->resources.getMesh(mc.mesh);
                        if (!mesh)
                            return;

                        // Local-space AABB from mesh bounds.
                        glm::vec3 lMin = {mesh->boundsMin.x, mesh->boundsMin.y, mesh->boundsMin.z};
                        glm::vec3 lMax = {mesh->boundsMax.x, mesh->boundsMax.y, mesh->boundsMax.z};

                        // Transform AABB corners to world space.
                        glm::vec3 wMin(1e30f);
                        glm::vec3 wMax(-1e30f);
                        for (int i = 0; i < 8; i++)
                        {
                            glm::vec3 corner((i & 1) ? lMax.x : lMin.x, (i & 2) ? lMax.y : lMin.y,
                                             (i & 4) ? lMax.z : lMin.z);
                            glm::vec3 w = glm::vec3(wt.matrix * glm::vec4(corner, 1.0f));
                            wMin = glm::min(wMin, w);
                            wMax = glm::max(wMax, w);
                        }

                        // Ray-AABB slab test.
                        float tmin = 0.0f;
                        float tmax = 1e30f;
                        bool hit = true;
                        for (int i = 0; i < 3; i++)
                        {
                            if (std::abs(rayD[i]) < 1e-8f)
                            {
                                if (rayO[i] < wMin[i] || rayO[i] > wMax[i])
                                {
                                    hit = false;
                                    break;
                                }
                            }
                            else
                            {
                                float t1 = (wMin[i] - rayO[i]) / rayD[i];
                                float t2 = (wMax[i] - rayO[i]) / rayD[i];
                                if (t1 > t2)
                                    std::swap(t1, t2);
                                tmin = std::max(tmin, t1);
                                tmax = std::min(tmax, t2);
                                if (tmin > tmax)
                                {
                                    hit = false;
                                    break;
                                }
                            }
                        }

                        if (hit && tmin < bestT)
                        {
                            bestT = tmin;
                            bestEntity = e;
                        }
                    });

                if (bestEntity != INVALID_ENTITY)
                {
                    impl_->editorState.select(bestEntity);
                }
                else
                {
                    impl_->editorState.clearSelection();
                }
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
            }
            impl_->prevLeftDown = impl_->window->isLeftMouseDown();
        }

        // -- Update properties in real-time during gizmo drag -----------------
        if (impl_->gizmo->isDragging())
        {
            impl_->propertiesDirty = true;

            // Sync directional light direction from rotation when rotating a light entity.
            EntityID selE = impl_->editorState.primarySelection();
            if (selE != INVALID_ENTITY)
            {
                auto* dl = impl_->registry.get<DirectionalLightComponent>(selE);
                auto* tc = impl_->registry.get<TransformComponent>(selE);
                if (dl && tc)
                {
                    // Light direction = entity's forward vector (default -Z rotated by quat).
                    dl->direction = tc->rotation * glm::vec3(0.0f, 0.0f, -1.0f);
                }
            }
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

        // -- Process menu actions (from native menu bar clicks) ---------------
        if (!impl_->pendingMenuAction.empty())
        {
            const auto& action = impl_->pendingMenuAction;
            if (action == "save_scene")
            {
                impl_->window->isKeyPressed('S');  // trigger same path below
                // Direct save: reuse the Cmd+S logic inline.
            }
            if (action == "save_scene_as" || action == "save_scene")
            {
                std::string path;
                if (action == "save_scene_as" || impl_->currentScenePath.empty())
                {
                    path = impl_->window->showSaveDialog("scene.json", "json");
                }
                else
                {
                    path = impl_->currentScenePath;
                }
                if (!path.empty())
                {
                    if (impl_->sceneSerializer.saveScene(impl_->registry, impl_->resources,
                                                         path.c_str()))
                    {
                        impl_->currentScenePath = path;
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Scene saved to %s", path.c_str());
                        EditorLog::instance().info(buf);
                        impl_->window->setWindowTitle(
                            ("Sama Editor — " + path.substr(path.find_last_of('/') + 1)).c_str());
                    }
                }
            }
            else if (action == "open_scene")
            {
                std::string path = impl_->window->showOpenDialog("json");
                if (!path.empty())
                {
                    // Clear existing entities.
                    std::vector<EntityID> toDelete;
                    impl_->registry.forEachEntity([&](EntityID e) { toDelete.push_back(e); });
                    for (auto e : toDelete)
                    {
                        impl_->registry.destroyEntity(e);
                    }
                    impl_->sceneSerializer.loadScene(path.c_str(), impl_->registry,
                                                     impl_->resources, *impl_->assetManager);
                    impl_->currentScenePath = path;
                    impl_->hierarchyDirty = true;
                    impl_->propertiesDirty = true;
                    impl_->commandStack.clear();
                    EditorLog::instance().info(
                        ("Opened " + path.substr(path.find_last_of('/') + 1)).c_str());
                    impl_->window->setWindowTitle(
                        ("Sama Editor — " + path.substr(path.find_last_of('/') + 1)).c_str());
                }
            }
            else if (action == "new_scene")
            {
                std::vector<EntityID> toDelete;
                impl_->registry.forEachEntity([&](EntityID e) { toDelete.push_back(e); });
                for (auto e : toDelete)
                {
                    impl_->registry.destroyEntity(e);
                }
                impl_->currentScenePath.clear();
                impl_->commandStack.clear();
                impl_->editorState.clearSelection();
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
                impl_->window->setWindowTitle("Sama Editor");
                EditorLog::instance().info("New scene");
            }
            else if (action == "undo")
            {
                impl_->commandStack.undo();
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
            }
            else if (action == "redo")
            {
                impl_->commandStack.redo();
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
            }
            else if (action == "delete")
            {
                EntityID selE = impl_->editorState.primarySelection();
                if (selE != INVALID_ENTITY)
                {
                    auto cmd = std::make_unique<DeleteEntityCommand>(impl_->registry,
                                                                     impl_->editorState, selE);
                    impl_->commandStack.execute(std::move(cmd));
                    impl_->hierarchyDirty = true;
                    impl_->propertiesDirty = true;
                }
            }
            else if (action == "create_empty")
            {
                auto cmd =
                    std::make_unique<CreateEntityCommand>(impl_->registry, impl_->editorState);
                impl_->commandStack.execute(std::move(cmd));
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
            }
            else if (action == "create_cube")
            {
                auto cmd =
                    std::make_unique<CreateEntityCommand>(impl_->registry, impl_->editorState);
                impl_->commandStack.execute(std::move(cmd));
                // Add mesh + material to the new entity.
                EntityID newE = impl_->editorState.primarySelection();
                if (newE != INVALID_ENTITY)
                {
                    uint32_t meshId = impl_->resources.addMesh(buildMesh(makeCubeMeshData()));
                    impl_->registry.emplace<MeshComponent>(newE, MeshComponent{meshId});
                    Material mat{};
                    mat.albedo = {0.6f, 0.6f, 0.6f, 1.0f};
                    mat.roughness = 0.5f;
                    uint32_t matId = impl_->resources.addMaterial(mat);
                    impl_->registry.emplace<MaterialComponent>(newE, MaterialComponent{matId});
                    impl_->registry.emplace<VisibleTag>(newE);
                }
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
            }
            else if (action == "create_light")
            {
                auto cmd =
                    std::make_unique<CreateEntityCommand>(impl_->registry, impl_->editorState);
                impl_->commandStack.execute(std::move(cmd));
                EntityID newE = impl_->editorState.primarySelection();
                if (newE != INVALID_ENTITY)
                {
                    DirectionalLightComponent dl{};
                    dl.direction = {0.4f, -0.7f, 0.5f};
                    dl.color = {1.0f, 0.95f, 0.85f};
                    dl.intensity = 6.0f;
                    impl_->registry.emplace<DirectionalLightComponent>(newE, dl);
                }
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
            }
            else if (action.rfind("add_component:", 0) == 0)
            {
                impl_->addComponentToSelection(action.substr(14));
            }
            else if (action == "import_asset")
            {
                std::string path = impl_->window->showImportDialog();
                if (!path.empty())
                {
                    auto handle = impl_->assetManager->load<engine::assets::GltfAsset>(path);

                    // Synchronous wait: pump uploads until the asset is ready.
                    while (impl_->assetManager->state(handle) ==
                           engine::assets::AssetState::Loading)
                    {
                        impl_->assetManager->processUploads();
                    }

                    if (impl_->assetManager->state(handle) == engine::assets::AssetState::Ready)
                    {
                        const auto* asset =
                            impl_->assetManager->get<engine::assets::GltfAsset>(handle);

                        // Record entity count before spawn to find new entities.
                        std::vector<EntityID> entitiesBefore;
                        impl_->registry.forEachEntity([&](EntityID e)
                                                      { entitiesBefore.push_back(e); });

                        engine::assets::GltfSceneSpawner::spawn(*asset, impl_->registry,
                                                                impl_->resources);

                        // Find newly spawned entities and add NameComponents.
                        std::vector<EntityID> newEntities;
                        impl_->registry.forEachEntity(
                            [&](EntityID e)
                            {
                                if (std::find(entitiesBefore.begin(), entitiesBefore.end(), e) ==
                                    entitiesBefore.end())
                                {
                                    newEntities.push_back(e);
                                }
                            });

                        // Derive a display name from the file path.
                        std::string filename = path.substr(path.find_last_of('/') + 1);
                        for (size_t i = 0; i < newEntities.size(); ++i)
                        {
                            if (!impl_->registry.has<engine::scene::NameComponent>(newEntities[i]))
                            {
                                std::string name = (newEntities.size() == 1)
                                                       ? filename
                                                       : filename + " [" + std::to_string(i) + "]";
                                impl_->registry.emplace<engine::scene::NameComponent>(
                                    newEntities[i], engine::scene::NameComponent{name});
                            }
                        }

                        // Select the first spawned entity.
                        if (!newEntities.empty())
                        {
                            impl_->editorState.clearSelection();
                            impl_->editorState.select(newEntities[0]);
                        }

                        char buf[256];
                        snprintf(buf, sizeof(buf), "Imported %s (%zu entities)", filename.c_str(),
                                 newEntities.size());
                        EditorLog::instance().info(buf);
                    }
                    else
                    {
                        const auto& err = impl_->assetManager->error(handle);
                        char buf[512];
                        snprintf(buf, sizeof(buf), "Import failed: %s", err.c_str());
                        EditorLog::instance().error(buf);
                    }

                    impl_->hierarchyDirty = true;
                    impl_->propertiesDirty = true;
                }
            }
            impl_->pendingMenuAction.clear();
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
        // Cmd+Shift+S = save scene as (always show dialog)
        // Cmd+S       = save scene (use current path, or show dialog if none)
        if (impl_->window->isKeyPressed('S') && impl_->window->isCommandDown())
        {
            std::string path = impl_->currentScenePath;

            if (impl_->window->isShiftDown() || path.empty())
            {
                path = impl_->window->showSaveDialog("scene.json", "json");
            }

            if (!path.empty())
            {
                if (impl_->sceneSerializer.saveScene(impl_->registry, impl_->resources,
                                                     path.c_str()))
                {
                    impl_->currentScenePath = path;
                    snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Scene saved to %s",
                             path.substr(path.find_last_of('/') + 1).c_str());
                    EditorLog::instance().info(impl_->statusMsg);

                    // Update window title.
                    std::string title = "Sama Editor — " + path.substr(path.find_last_of('/') + 1);
                    impl_->window->setWindowTitle(title.c_str());
                }
                else
                {
                    snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Failed to save scene!");
                    EditorLog::instance().error("Failed to save scene!");
                }
                impl_->statusTimer = 3.0f;
            }
        }

        // Cmd+O = open scene
        if (impl_->window->isKeyPressed('O') && impl_->window->isCommandDown())
        {
            std::string path = impl_->window->showOpenDialog("json");
            if (!path.empty())
            {
                // Clear existing entities before loading.
                std::vector<EntityID> toDelete;
                impl_->registry.forEachEntity([&](EntityID e) { toDelete.push_back(e); });
                for (auto e : toDelete)
                {
                    impl_->registry.destroyEntity(e);
                }

                if (impl_->sceneSerializer.loadScene(path.c_str(), impl_->registry,
                                                     impl_->resources, *impl_->assetManager))
                {
                    impl_->currentScenePath = path;
                    impl_->commandStack.clear();
                    impl_->editorState.clearSelection();
                    impl_->hierarchyDirty = true;
                    impl_->propertiesDirty = true;

                    snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Opened %s",
                             path.substr(path.find_last_of('/') + 1).c_str());
                    EditorLog::instance().info(impl_->statusMsg);

                    std::string title = "Sama Editor — " + path.substr(path.find_last_of('/') + 1);
                    impl_->window->setWindowTitle(title.c_str());
                }
                else
                {
                    snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Failed to open scene!");
                    EditorLog::instance().error("Failed to open scene!");
                }
                impl_->statusTimer = 3.0f;
            }
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

        // Cmd+I = import 3D asset
        if (impl_->window->isKeyPressed('I') && impl_->window->isCommandDown())
        {
            impl_->pendingMenuAction = "import_asset";
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

        // Add component menu: number keys select component type. All branches
        // route through Impl::addComponentToSelection to keep behavior in sync
        // with the macOS Component menu and the AppKit "+ Add Component" button.
        if (impl_->addComponentMenuOpen)
        {
            EntityID selE = impl_->editorState.primarySelection();
            if (selE != INVALID_ENTITY)
            {
                if (impl_->window->isKeyPressed('1'))
                {
                    impl_->addComponentToSelection("directional_light");
                    impl_->addComponentMenuOpen = false;
                }
                if (impl_->window->isKeyPressed('2'))
                {
                    impl_->addComponentToSelection("point_light");
                    impl_->addComponentMenuOpen = false;
                }
                if (impl_->window->isKeyPressed('3'))
                {
                    impl_->addComponentToSelection("mesh");
                    impl_->addComponentMenuOpen = false;
                }
                if (impl_->window->isKeyPressed('4'))
                {
                    impl_->addComponentToSelection("rigid_body");
                    impl_->addComponentMenuOpen = false;
                }
                if (impl_->window->isKeyPressed('5'))
                {
                    impl_->addComponentToSelection("box_collider");
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

        // -- Play/Pause/Stop controls ----------------------------------------
        // Space = play/pause toggle, Escape = stop (when playing/paused).
        if (impl_->window->isKeyPressed(' ') && !impl_->addComponentMenuOpen)
        {
            auto ps = impl_->editorState.playState();
            if (ps == EditorPlayState::Editing)
            {
                // Snapshot all transforms before entering play mode.
                std::vector<EditorState::TransformSnapshot> snapshot;
                impl_->registry.forEachEntity(
                    [&](EntityID e)
                    {
                        auto* tc = impl_->registry.get<TransformComponent>(e);
                        if (tc)
                        {
                            snapshot.push_back({e, *tc});
                        }
                    });
                impl_->editorState.saveSnapshot(snapshot);
                impl_->editorState.play();
                // Reset Jolt bodies so PhysicsSystem recreates them fresh
                // from the authored components with zeroed velocities.
                impl_->resetPhysicsBodies();
                snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Play mode");
                impl_->statusTimer = 2.0f;
                EditorLog::instance().info("Entered play mode");
            }
            else if (ps == EditorPlayState::Playing)
            {
                impl_->editorState.pause();
                snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Paused");
                impl_->statusTimer = 2.0f;
                EditorLog::instance().info("Paused");
            }
            else if (ps == EditorPlayState::Paused)
            {
                impl_->editorState.play();
                snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Resumed");
                impl_->statusTimer = 2.0f;
                EditorLog::instance().info("Resumed play mode");
            }
        }
        if (impl_->window->isKeyPressed(0x1B) && !impl_->addComponentMenuOpen)
        {
            auto ps = impl_->editorState.playState();
            if (ps == EditorPlayState::Playing || ps == EditorPlayState::Paused)
            {
                // Restore transforms from snapshot.
                for (const auto& snap : impl_->editorState.transformSnapshot())
                {
                    auto* tc = impl_->registry.get<TransformComponent>(snap.entity);
                    if (tc)
                    {
                        *tc = snap.transform;
                        tc->flags |= 0x01;  // mark dirty
                    }
                }
                impl_->editorState.stop();
                // Wipe physics bodies so editing the scene doesn't fight
                // leftover Jolt state; they'll be recreated fresh next Play.
                impl_->resetPhysicsBodies();
                snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Stopped - state restored");
                impl_->statusTimer = 2.0f;
                EditorLog::instance().info("Stopped play mode, transforms restored");
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
            }
        }

        // Decrement status timer.
        if (impl_->statusTimer > 0.0f)
        {
            impl_->statusTimer -= dt;
        }

        // -- Physics simulation (Play mode only) ------------------------------
        if (impl_->editorState.playState() == EditorPlayState::Playing)
        {
            impl_->physicsSys.update(impl_->registry, impl_->physics, dt);
        }

        // -- Transform system -------------------------------------------------
        impl_->transformSys.update(impl_->registry);

        // -- Render -----------------------------------------------------------
        const auto W = impl_->fbW;
        const auto H = impl_->fbH;

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
        bgfx::setViewTransform(kViewOpaque, glm::value_ptr(viewMtx), glm::value_ptr(projMtx));

        // Directional light — read from the first DirectionalLightComponent entity.
        // Falls back to a default sun direction if no light entity exists.
        glm::vec3 lightDir = glm::normalize(glm::vec3(0.4f, 0.7f, 0.5f));
        glm::vec3 lightColor = {1.0f, 0.95f, 0.85f};
        float lightIntensity = 6.0f;
        {
            auto dlView = impl_->registry.view<DirectionalLightComponent>();
            dlView.each(
                [&](EntityID /*e*/, const DirectionalLightComponent& dl)
                {
                    lightDir = glm::normalize(dl.direction);
                    lightColor = dl.color;
                    lightIntensity = dl.intensity;
                });
        }
        const float lightData[8] = {lightDir.x,
                                    lightDir.y,
                                    lightDir.z,
                                    0.0f,
                                    lightColor.x * lightIntensity,
                                    lightColor.y * lightIntensity,
                                    lightColor.z * lightIntensity,
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
                impl_->gizmoRenderer.render(*impl_->gizmo, viewMtx, projMtx, W, H);
            }
        }

        // -- Light gizmo rendering --------------------------------------------
        impl_->gizmoRenderer.renderLightGizmos(impl_->registry, viewMtx, projMtx, W, H);

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

        // Play state indicator.
        {
            auto ps = impl_->editorState.playState();
            if (ps == EditorPlayState::Playing)
            {
                bgfx::dbgTextPrintf(1, 3, 0x0a, "> PLAYING  (Space=pause, Esc=stop)");
            }
            else if (ps == EditorPlayState::Paused)
            {
                bgfx::dbgTextPrintf(1, 3, 0x0e, "|| PAUSED  (Space=resume, Esc=stop)");
            }
            else
            {
                bgfx::dbgTextPrintf(1, 3, 0x08, "Space=play");
            }
        }

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
            bgfx::dbgTextPrintf(2, 8, 0x07, "4) Rigid Body");
            bgfx::dbgTextPrintf(2, 9, 0x07, "5) Box Collider");
            bgfx::dbgTextPrintf(2, 10, 0x08, "Esc to cancel");
        }

        // -- Resource panel update --------------------------------------------
        impl_->resourcePanel.update(dt, impl_->registry, impl_->frameArena.get());
        {
            auto* rView = impl_->window->resourceView();
            if (rView)
            {
                rView->updateStats(impl_->resourcePanel.currentStats());
            }
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

    // Shut down physics (destroys all remaining bodies internally).
    impl_->physics.shutdown();

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
