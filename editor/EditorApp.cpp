#include "editor/EditorApp.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <crt_externs.h>  // _NSGetEnviron — for posix_spawn env passthrough
#include <mach-o/dyld.h>  // _NSGetExecutablePath, for asset path resolution
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <thread>

#include "editor/BuildPhaseParser.h"
#include "editor/EditorLog.h"
#include "editor/EditorState.h"
#include "editor/SelectionOutline.h"
#include "editor/gizmo/GizmoRenderer.h"
#include "editor/gizmo/TransformGizmo.h"
#include "editor/inspectors/ColliderInspector.h"
#include "editor/inspectors/LightInspector.h"
#include "editor/inspectors/MaterialInspector.h"
#include "editor/inspectors/NameInspector.h"
#include "editor/inspectors/RigidBodyInspector.h"
#include "editor/inspectors/TransformInspector.h"
#include "editor/panels/AnimationPanel.h"
#include "editor/panels/AssetBrowserPanel.h"
#include "editor/panels/ConsolePanel.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/PropertiesPanel.h"
#include "editor/panels/ResourcePanel.h"
#include "editor/platform/IEditorWindow.h"
#include "editor/platform/cocoa/CocoaAnimationView.h"
#include "editor/platform/cocoa/CocoaConsoleView.h"
#include "editor/platform/cocoa/CocoaEditorWindow.h"
#include "editor/platform/cocoa/CocoaHierarchyView.h"
#include "editor/platform/cocoa/CocoaPropertiesView.h"
#include "editor/platform/cocoa/CocoaResourceView.h"
#include "editor/undo/CommandStack.h"
#include "editor/undo/CreateEntityCommand.h"
#include "editor/undo/DeleteEntityCommand.h"
#include "editor/undo/SetTransformCommand.h"
#include "engine/animation/AnimStateMachine.h"
#include "engine/animation/AnimStateMachineSystem.h"
#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationEventQueue.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSerializer.h"
#include "engine/animation/AnimationSystem.h"
#include "engine/assets/CubemapLoader.h"
#include "engine/assets/EnvironmentAssetSerializer.h"
#include "engine/assets/GltfAsset.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/GltfSceneSpawner.h"
#include "engine/assets/HdrLoader.h"
#include "engine/assets/ObjLoader.h"
#include "engine/assets/StdFileSystem.h"
#include "engine/assets/Texture.h"
#include "engine/assets/TextureLoader.h"
#include "engine/core/OrbitCamera.h"
#include "engine/ecs/Registry.h"
#include "engine/memory/FrameArena.h"
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsSystem.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/IblResources.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/SkyboxRenderer.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/rendering/systems/PostProcessSystem.h"
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/NameComponent.h"
#include "engine/scene/SceneGraph.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/TransformSystem.h"
#include "engine/threading/ThreadPool.h"
#include "engine/ui/MsdfFont.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiRenderer.h"

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
    // through engine_core; instead we init bgfx directly, including selection
    // outline pass — see SelectionOutline.h for the stencil state helpers).
    ShaderUniforms uniforms;
    RenderResources resources;
    bgfx::ProgramHandle pbrProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinnedPbrProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle shadowProgram = BGFX_INVALID_HANDLE;

    // Selection-outline programs.  Both run on the HDR scene FB right after
    // the opaque/skybox passes so they can use the FB's D24S8 stencil
    // attachment and get tonemapped along with the rest of the scene.
    //   outlineFillProgram  — writes 1 to stencil at the visible mesh surface.
    //   outlineProgram      — re-renders the mesh inflated along its normal,
    //                         gated by stencil_test = NOT_EQUAL 1, painting
    //                         the silhouette band in u_outlineColor.
    bgfx::ProgramHandle outlineFillProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle outlineProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle outlineColorUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle outlineParamsUniform = BGFX_INVALID_HANDLE;

    // Post-process — owns the HDR scene framebuffer and the tonemap shader.
    // The editor binds kViewOpaque to postProcess.resources().sceneFb() so
    // PBR (which now outputs linear HDR) writes there; postProcess.submit()
    // tonemaps to the backbuffer every frame.  This also fixes the "viewport
    // goes black when idle" bug — the sceneFb persists between frames, so
    // the tonemap pass keeps presenting it even when no scene draws are
    // re-submitted.
    engine::rendering::PostProcessSystem postProcess;
    bool postProcessInitialized = false;

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

    // Skeletal animation runtime + editor panel.
    engine::animation::AnimationResources animationResources;
    engine::animation::AnimationSystem animationSystem;
    engine::animation::AnimStateMachineSystem stateMachineSystem;
    std::unique_ptr<AnimationPanel> animationPanel;

    // Imported state machines (kept alive so AnimStateMachineComponent can
    // hold a raw pointer into them).
    std::vector<std::shared_ptr<engine::animation::AnimStateMachine>> importedStateMachines;

    // Gizmo
    std::unique_ptr<TransformGizmo> gizmo;
    GizmoRenderer gizmoRenderer;

    // Skybox + procedural IBL. The cubemap that the skybox samples is the
    // mip 0 of IblResources::prefiltered() — the editor uses the engine's
    // built-in sunset-like procedural sky model from generateDefault().
    engine::rendering::IblResources iblResources;
    engine::rendering::SkyboxRenderer skybox;

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

    // HUD text rendering — replaces bgfx::dbgTextPrintf with a real
    // MSDF-rendered overlay using JetBrains Mono. Drawn on view kViewImGui.
    engine::ui::MsdfFont hudFont;
    bool hudFontLoaded = false;
    engine::ui::UiDrawList hudDrawList;
    engine::ui::UiRenderer uiRenderer;

    // Viewport click-to-select tracking.
    bool prevLeftDown = false;

    // Dirty flags for native panel updates.
    bool hierarchyDirty = true;
    bool propertiesDirty = true;
    size_t lastLogCount = 0;
    EntityID lastSelectedEntity = INVALID_ENTITY;

    // Viewport dirty-flag: when false, the run loop skips the heavy 3D scene
    // submission (DrawCallBuildSystem, selection outline, skybox, gizmos, HUD)
    // and just touches the relevant bgfx views so the swapchain re-presents
    // the previous frame. Set to true on any state change that affects the
    // viewport image (camera, selection, transform edits, undo/redo, resize,
    // scene load, component add/remove, asset hot-reload). Forced true every
    // frame while in Play mode (physics + animation are mutating state).
    // Default-true so the very first frame renders.
    bool viewportDirty = true;
    uint64_t viewportRedrawCount = 0;

    // Menu action pending from native menu click (set by static callback).
    std::string pendingMenuAction;

    // Android APK build state.  The build runs on a detached std::thread
    // spawned by the Build > Android menu actions; the editor must remain
    // interactive (rendering frames, processing input, polling for assets).
    //
    // Threading model (see docs/NOTES.md "Editor Build & Run threading"):
    //   * UI thread   : owns the Cocoa run loop, status bar, menu wiring.
    //                   Reads `androidBuildRunning` (atomic) to disable
    //                   the per-tier menu items while a build is in flight.
    //   * Build thread: spawns build_apk.sh via popen2, parses its stdout
    //                   line-by-line, and pushes status updates back to
    //                   the UI via window->setBuildStatus(...) (which
    //                   marshals onto the main queue itself).  Owns the
    //                   build process pid so the Cancel button can SIGTERM
    //                   it from the UI thread.
    //   * adb thread  : reused for queryAdbDevices() and `adb shell am
    //                   start` after a successful Build & Run.  Spawned
    //                   inline from the build thread on success, so we
    //                   never block the UI on adb network round-trips.
    std::atomic<bool> androidBuildRunning{false};
    std::atomic<bool> androidBuildCancelRequested{false};
    std::atomic<pid_t> androidBuildPid{-1};
    // Whether the active build should auto-install + launch when it
    // succeeds. Set by the Build & Run menu action; cleared when the
    // build thread exits.
    bool androidBuildAndRunActive = false;

    // Editor-loaded textures (separate from glTF-imported textures, which
    // are owned by the GltfSceneSpawner). When the user picks a new image
    // for a material slot via the Properties panel, we load it through the
    // asset manager, register the resulting bgfx handle in RenderResources,
    // and remember (resourceId, asset handle, source path) here so we can
    // (a) decrement the asset ref count on shutdown, (b) re-use the same
    // resource id when the same path is picked twice in one session,
    // (c) display the source filename in the Properties panel.
    struct EditorTexture
    {
        std::string path;
        engine::assets::AssetHandle<engine::assets::Texture> handle{};
        uint32_t resourceId = 0;        // index in RenderResources::textures_
        uint32_t materialRefCount = 0;  // number of material slots pointing at this texture
    };
    std::vector<EditorTexture> editorTextures;

    // Resolve `absolutePath` to a RenderResources texture id, loading it
    // synchronously if it isn't already cached. Returns 0 on failure.
    // The returned id has NO ref count added yet — callers must go through
    // rebindTexture() to install it into a material slot.
    uint32_t loadTextureForMaterial(const std::string& absolutePath);

    // Atomically swap the texture id held by a material slot, maintaining
    // per-texture reference counts. `materialSlotId` points at the Material
    // field (e.g. &mat->albedoMapId); `newId` may be 0 to clear the slot.
    // When a texture's ref count hits zero, its asset handle is released
    // and its RenderResources slot is freed.
    void rebindTexture(uint32_t* materialSlotId, uint32_t newId);

    // Release every editor-loaded texture (asset release + RenderResources
    // removeTexture) and clear the cache.  Called on scene New / Open.
    // Material slot ids in the registry are invalid after this — callers
    // must have already cleared/destroyed the referring entities.
    void clearEditorTextures();

    // Look up a previously-loaded texture by its RenderResources id and
    // return its source path (empty if the id is unknown). Used by the
    // Properties panel to display the filename next to a texture slot.
    const std::string& textureSourcePath(uint32_t resourceId) const;

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
        // Auto-fit half-extents to the entity's local scale: a unit cube
        // mesh covers the box [-0.5, 0.5]^3, so half-extents = 0.5 * scale
        // produces a collider that visually matches a non-uniformly scaled
        // mesh (e.g. the default Ground entity at scale {20, 0.01, 20}
        // becomes a 10 x 0.005 x 10 collider, exactly matching the slab).
        if (auto* tc = registry.get<TransformComponent>(selE))
        {
            cc.halfExtents = {std::max(std::abs(tc->scale.x) * 0.5f, 1e-4f),
                              std::max(std::abs(tc->scale.y) * 0.5f, 1e-4f),
                              std::max(std::abs(tc->scale.z) * 0.5f, 1e-4f)};
        }
        else
        {
            cc.halfExtents = {0.5f, 0.5f, 0.5f};
        }
        registry.emplace<engine::physics::ColliderComponent>(selE, cc);
        setStatus("Added Box Collider");
    }
    else if (type == "state_machine")
    {
        using namespace engine::animation;
        if (registry.has<AnimStateMachineComponent>(selE))
        {
            setStatus("State Machine already present");
            return false;
        }
        if (!registry.has<AnimatorComponent>(selE))
        {
            setStatus("Entity needs AnimatorComponent first (import an animated model)");
            return false;
        }

        // Build a default state machine with one state per clip.
        auto sm = std::make_shared<AnimStateMachine>();
        const uint32_t clipCount = animationResources.clipCount();
        for (uint32_t i = 0; i < clipCount; ++i)
        {
            const AnimationClip* clip = animationResources.getClip(i);
            std::string name =
                (clip && !clip->name.empty()) ? clip->name : "clip_" + std::to_string(i);
            sm->addState(name, i, true, 1.0f);
        }
        // Add a default "speed" parameter.
        if (clipCount > 0)
            sm->addTransition(0, 0, 0.3f, "speed", TransitionCondition::Compare::Greater, 999.0f);
        importedStateMachines.push_back(sm);

        AnimStateMachineComponent smComp;
        smComp.machine = sm.get();
        smComp.currentState = 0;
        smComp.setFloat("speed", 1.0f);
        registry.emplace<AnimStateMachineComponent>(selE, std::move(smComp));
        if (animationPanel)
            animationPanel->markDirty();
        setStatus("Added State Machine");
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
    viewportDirty = true;
    return true;
}

uint32_t EditorApp::Impl::loadTextureForMaterial(const std::string& absolutePath)
{
    if (absolutePath.empty())
        return 0;

    // De-dupe: if we already loaded this exact path in this session, return
    // the existing resource id so material slots can share textures.
    for (const auto& t : editorTextures)
    {
        if (t.path == absolutePath)
            return t.resourceId;
    }

    // Convert to a path the asset manager can resolve. We constructed the
    // StdFileSystem with root ".", so an absolute path needs to be either
    // made relative to cwd or passed as-is. The std file system accepts
    // absolute paths via fopen, so we just hand it over.
    auto handle = assetManager->load<engine::assets::Texture>(absolutePath);
    if (!handle.isValid())
    {
        snprintf(statusMsg, sizeof(statusMsg), "Texture load failed: bad handle");
        statusTimer = 3.0f;
        return 0;
    }

    // Synchronous wait. Texture loads are typically fast (decode + upload),
    // and the editor is interactive — blocking is acceptable for v1.
    while (assetManager->state(handle) == engine::assets::AssetState::Loading ||
           assetManager->state(handle) == engine::assets::AssetState::Pending)
    {
        assetManager->processUploads();
    }

    if (assetManager->state(handle) != engine::assets::AssetState::Ready)
    {
        const std::string& err = assetManager->error(handle);
        snprintf(statusMsg, sizeof(statusMsg), "Texture load failed: %.80s",
                 err.empty() ? "unknown error" : err.c_str());
        statusTimer = 3.0f;
        assetManager->release(handle);
        return 0;
    }

    const engine::assets::Texture* tex = assetManager->get<engine::assets::Texture>(handle);
    if (!tex || !tex->isValid())
    {
        snprintf(statusMsg, sizeof(statusMsg), "Texture load failed: invalid handle");
        statusTimer = 3.0f;
        assetManager->release(handle);
        return 0;
    }

    // RenderResources::addTexture takes the bgfx-free TextureHandle
    // wrapper; wrap the asset's bgfx handle at the boundary.
    const uint32_t resId = resources.addTexture(engine::rendering::TextureHandle{tex->handle.idx});
    editorTextures.push_back({absolutePath, handle, resId, 0u});

    snprintf(statusMsg, sizeof(statusMsg), "Loaded texture (id=%u)", resId);
    statusTimer = 2.0f;
    return resId;
}

void EditorApp::Impl::rebindTexture(uint32_t* materialSlotId, uint32_t newId)
{
    if (materialSlotId == nullptr)
        return;

    const uint32_t oldId = *materialSlotId;
    if (oldId == newId)
        return;

    // Increment the new texture's ref count BEFORE writing the slot so that
    // if newId happens to equal oldId's final reference, we don't momentarily
    // drop to zero and free a texture we're about to re-use.
    if (newId != 0)
    {
        for (auto& t : editorTextures)
        {
            if (t.resourceId == newId)
            {
                ++t.materialRefCount;
                break;
            }
        }
    }

    *materialSlotId = newId;

    if (oldId != 0)
    {
        for (auto it = editorTextures.begin(); it != editorTextures.end(); ++it)
        {
            if (it->resourceId != oldId)
                continue;
            if (it->materialRefCount > 0)
                --it->materialRefCount;
            if (it->materialRefCount == 0)
            {
                assetManager->release(it->handle);
                resources.removeTexture(it->resourceId);
                editorTextures.erase(it);
            }
            break;
        }
    }
}

void EditorApp::Impl::clearEditorTextures()
{
    for (auto& t : editorTextures)
    {
        assetManager->release(t.handle);
        resources.removeTexture(t.resourceId);
    }
    editorTextures.clear();
}

const std::string& EditorApp::Impl::textureSourcePath(uint32_t resourceId) const
{
    static const std::string kEmpty;
    if (resourceId == 0)
        return kEmpty;
    for (const auto& t : editorTextures)
    {
        if (t.resourceId == resourceId)
            return t.path;
    }
    return kEmpty;
}

void EditorApp::Impl::refreshHierarchyView()
{
    auto* hView = window->hierarchyView();
    if (!hView)
        return;

    // Pre-reserve so the typical 8-32 entity scenes don't reallocate.
    std::vector<CocoaHierarchyView::EntityInfo> entities;
    entities.reserve(64);

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

    // Reserve enough for the worst case: a fully-loaded entity with all
    // supported component blocks. Each block contributes a header + a
    // handful of fields. 48 covers Transform (10) + Material (5) + two
    // lights (8) + RigidBody (7) + Collider (8) + headers + a few labels.
    std::vector<CocoaPropertiesView::PropertyField> fields;
    fields.reserve(48);

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

    // Visibility checkbox.
    {
        CocoaPropertiesView::PropertyField f;
        f.type = CocoaPropertiesView::PropertyField::Type::CheckboxField;
        f.label = "Visible";
        f.fieldId = 50;
        f.checked = registry.has<VisibleTag>(entity);
        fields.push_back(f);
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

            // Texture slots — five rows, one per Material texture id field.
            // Field IDs 210..214 are reserved for these (see the texture-
            // changed callback wiring in init()).
            struct TexSlot
            {
                const char* label;
                int fieldId;
                uint32_t texId;
            };
            const TexSlot slots[] = {
                {"Albedo Map", 210, mat->albedoMapId}, {"Normal Map", 211, mat->normalMapId},
                {"ORM Map", 212, mat->ormMapId},       {"Emiss Map", 213, mat->emissiveMapId},
                {"AO Map", 214, mat->occlusionMapId},
            };
            for (const auto& s : slots)
            {
                CocoaPropertiesView::PropertyField f;
                f.type = CocoaPropertiesView::PropertyField::Type::TextureField;
                f.label = s.label;
                f.fieldId = s.fieldId;
                f.texturePath = textureSourcePath(s.texId);
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

// ---------------------------------------------------------------------------
// Android build helpers (file-local).
//
// `spawnBuildProcess` runs the supplied argv via posix_spawn (so we get a
// PID we can SIGTERM from the Cancel button) and returns the read end of
// a pipe wrapped in a FILE* via fdopen. The caller drains the pipe
// line-by-line and waitpid()s the child to harvest the exit status.
//
// Why posix_spawn instead of popen():
//   popen() returns a FILE* but hides the underlying PID — there is no
//   portable way to ask "what process did popen() fork?" so we can't
//   send a SIGTERM when the user clicks Cancel. `posix_spawn` gives us
//   the PID directly while keeping the same line-oriented stdout
//   reading pattern.
// ---------------------------------------------------------------------------
namespace
{

struct SpawnedProcess
{
    pid_t pid = -1;
    FILE* readFile = nullptr;
};

// On macOS, `environ` is not declared in <unistd.h> for executable
// targets — it's exposed via crt_externs.h's `_NSGetEnviron()`. Use that
// here so we don't depend on a non-portable extern declaration.
SpawnedProcess spawnBuildProcess(const std::vector<std::string>& argv,
                                 const std::vector<std::string>& extraEnv)
{
    SpawnedProcess out{};
    int pipeFds[2];
    if (pipe(pipeFds) != 0)
        return out;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    posix_spawn_file_actions_addclose(&actions, pipeFds[1]);

    std::vector<char*> argvC;
    argvC.reserve(argv.size() + 1);
    for (const auto& a : argv)
        argvC.push_back(const_cast<char*>(a.c_str()));
    argvC.push_back(nullptr);

    // Build environment: copy current env + extra vars.
    std::vector<std::string> envStorage;
    char*** envp = _NSGetEnviron();
    if (envp && *envp)
    {
        for (char** ep = *envp; *ep; ++ep)
            envStorage.emplace_back(*ep);
    }
    for (const auto& kv : extraEnv)
        envStorage.push_back(kv);
    std::vector<char*> envC;
    envC.reserve(envStorage.size() + 1);
    for (auto& e : envStorage)
        envC.push_back(const_cast<char*>(e.c_str()));
    envC.push_back(nullptr);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, argvC[0], &actions, nullptr, argvC.data(), envC.data());
    posix_spawn_file_actions_destroy(&actions);
    close(pipeFds[1]);

    if (rc != 0)
    {
        close(pipeFds[0]);
        return out;
    }

    out.pid = pid;
    out.readFile = fdopen(pipeFds[0], "r");
    if (!out.readFile)
    {
        close(pipeFds[0]);
        // Reap the child we just started so we don't leak a zombie.
        kill(pid, SIGTERM);
        int status = 0;
        waitpid(pid, &status, 0);
        out.pid = -1;
    }
    return out;
}

// Parse a build_apk.sh stdout line and return a short status-bar message.
// Returns empty string if the line is not a known phase marker. The
// structured grammar lives in editor/BuildPhaseParser.h so it can be unit
// tested without dragging the rest of EditorApp into the test binary; this
// thin wrapper preserves the original return contract (non-empty = show in
// status bar, empty = ignore) for the existing call site below.
std::string buildPhaseFromLine(const std::string& line)
{
    auto parsed = engine::editor::parseBuildPhase(line);
    if (!parsed)
        return {};
    return line;  // Whole line is short and informative.
}

}  // namespace

void EditorApp::Impl::syncConsoleView()
{
    auto* cView = window->consoleView();
    if (!cView)
        return;

    size_t currentCount = EditorLog::instance().entryCount();
    if (currentCount <= lastLogCount)
        return;

    // Stream new entries directly to the view. The ring buffer's `forEach`
    // walks in chronological order; we count how many entries we've already
    // sent and skip those. No intermediate vector, no per-frame heap
    // allocation.
    const size_t startIdx = lastLogCount;
    size_t i = 0;
    EditorLog::instance().forEach(
        [&](const EditorLog::Entry& entry)
        {
            if (i++ < startIdx)
                return;
            CocoaConsoleView::MessageLevel mlevel;
            switch (entry.level)
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
            cView->appendMessage(mlevel, entry.message);
        });

    lastLogCount = currentCount;
}

EditorApp::EditorApp() : impl_(std::make_unique<Impl>()) {}

EditorApp::~EditorApp()
{
    shutdown();
}

bool EditorApp::init(uint32_t width, uint32_t height)
{
    // -- Startup profile ------------------------------------------------------
    // Records wall-clock timestamps at each major init phase and dumps a
    // breakdown to stderr at the end of init(). Always-on; the cost is one
    // chrono::steady_clock read per checkpoint, ~10 ns each.
    using StartupClock = std::chrono::steady_clock;
    const auto startupT0 = StartupClock::now();
    auto startupNow = [&]() -> double
    {
        const auto dt = StartupClock::now() - startupT0;
        return std::chrono::duration<double, std::milli>(dt).count();
    };
    struct StartupSample
    {
        const char* name;
        double endMs;
    };
    std::vector<StartupSample> startupSamples;
    startupSamples.reserve(16);

    // -- Window ---------------------------------------------------------------
    impl_->window = std::make_unique<CocoaEditorWindow>();
    if (!impl_->window->init(width, height, "Sama Editor"))
    {
        fprintf(stderr, "EditorApp: failed to create window\n");
        return false;
    }
    startupSamples.push_back({"native window", startupNow()});

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
    startupSamples.push_back({"bgfx::init", startupNow()});

    bgfx::setDebug(BGFX_DEBUG_TEXT);

    impl_->fbW = static_cast<uint16_t>(impl_->window->viewportFramebufferWidth());
    impl_->fbH = static_cast<uint16_t>(impl_->window->viewportFramebufferHeight());

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030FF, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, impl_->fbW, impl_->fbH);

    // -- Uniforms -------------------------------------------------------------
    impl_->uniforms.init();
    startupSamples.push_back({"uniforms", startupNow()});

    // -- Post-process (HDR scene FB + ACES tonemap) --------------------------
    // Owns the offscreen scene framebuffer that kViewOpaque writes into.
    // submit() runs every frame so the tonemap pass converts linear HDR PBR
    // output to sRGB-gamma LDR for the backbuffer.  Because the scene FB is
    // persistent, idle frames (viewportDirty=false) can skip scene re-
    // submission and the tonemap pass still re-presents the last scene.
    impl_->postProcess.init(impl_->fbW, impl_->fbH);
    impl_->postProcessInitialized = true;

    // -- Shader programs ------------------------------------------------------
    // ShaderLoader returns engine::rendering::ProgramHandle (bgfx-free
    // wrapper); widen to bgfx for the editor-internal storage members.
    impl_->pbrProgram = bgfx::ProgramHandle{loadPbrProgram().idx};
    impl_->skinnedPbrProgram = bgfx::ProgramHandle{loadSkinnedPbrProgram().idx};
    impl_->shadowProgram = bgfx::ProgramHandle{loadShadowProgram().idx};
    impl_->outlineFillProgram = bgfx::ProgramHandle{loadOutlineFillProgram().idx};
    impl_->outlineProgram = bgfx::ProgramHandle{loadOutlineProgram().idx};
    // u_outlineColor: linear-HDR RGBA used by fs_outline.sc.  Bright yellow
    // value chosen so ACES tonemap output reads as a vivid, saturated band
    // in the editor viewport (see Phase 7 NOTES — fs_pbr.sc now writes
    // linear HDR, so anything subsequently submitted into sceneFb gets
    // tonemapped).
    // u_outlineParams.x: outline thickness in metres in object space.  The
    // vs_outline shader pushes each vertex along its normal by this much
    // before MVP — kept in object space so non-uniform world scales don't
    // distort the apparent silhouette width.
    impl_->outlineColorUniform = bgfx::createUniform("u_outlineColor", bgfx::UniformType::Vec4);
    impl_->outlineParamsUniform = bgfx::createUniform("u_outlineParams", bgfx::UniformType::Vec4);
    startupSamples.push_back({"shader programs (PBR + skinned + shadow + outline)", startupNow()});

    // -- Default textures -----------------------------------------------------
    {
        const uint8_t kWhite[4] = {255, 255, 255, 255};
        impl_->whiteTex =
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                  bgfx::copy(kWhite, sizeof(kWhite)));
        impl_->resources.setWhiteTexture(engine::rendering::TextureHandle{impl_->whiteTex.idx});

        const uint8_t kNeutralNormal[4] = {128, 128, 255, 255};
        impl_->neutralNormalTex =
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                  bgfx::copy(kNeutralNormal, sizeof(kNeutralNormal)));
        impl_->resources.setNeutralNormalTexture(
            engine::rendering::TextureHandle{impl_->neutralNormalTex.idx});

        uint8_t cubeFaces[6 * 4];
        for (int i = 0; i < 6 * 4; ++i)
            cubeFaces[i] = 255;
        impl_->whiteCubeTex =
            bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                    bgfx::copy(cubeFaces, sizeof(cubeFaces)));
        impl_->resources.setWhiteCubeTexture(
            engine::rendering::TextureHandle{impl_->whiteCubeTex.idx});

        // 1x1 dummy shadow map so the PBR shader has a valid sampler.
        const uint8_t kWhiteDepth[4] = {255, 255, 255, 255};
        impl_->dummyShadowTex =
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                  bgfx::copy(kWhiteDepth, sizeof(kWhiteDepth)));
    }
    startupSamples.push_back({"default textures", startupNow()});

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
    startupSamples.push_back({"default scene (cube + ground)", startupNow()});

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
            impl_->viewportDirty = true;
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
            // Name doesn't affect viewport image, but defensively keep this in
            // sync — rename doesn't change geometry, so skip viewport dirty.
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
            // Reparenting can change a child's world transform.
            impl_->viewportDirty = true;
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
            impl_->viewportDirty = true;
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
            impl_->viewportDirty = true;
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
            impl_->viewportDirty = true;
            EditorLog::instance().info("Deleted entity and its children");
        });

    // Wire selection change to mark properties dirty.
    impl_->editorState.setSelectionChangedCallback(
        [this]()
        {
            impl_->propertiesDirty = true;
            // Selection outline / gizmo target both depend on the selected
            // entity, so the viewport image changes whenever selection does.
            impl_->viewportDirty = true;
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
            // Viewport must redraw to reflect the new transform / material /
            // light value — safe because dirtying the viewport doesn't rebuild
            // any AppKit panels.
            impl_->viewportDirty = true;
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
            impl_->viewportDirty = true;
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
            impl_->viewportDirty = true;
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

    // Wire native properties view texture-picker callback.
    // fieldId encodes which slot of the material was clicked:
    //   210 = albedo, 211 = normal, 212 = ORM, 213 = emissive, 214 = occlusion
    impl_->window->propertiesView()->setTextureChangedCallback(
        [this](int fieldId, const std::string& path)
        {
            if (impl_->editorState.playState() != EditorPlayState::Editing)
                return;
            EntityID entity = impl_->editorState.primarySelection();
            if (entity == INVALID_ENTITY)
                return;
            auto* mc = impl_->registry.get<MaterialComponent>(entity);
            if (!mc)
                return;
            Material* mat = impl_->resources.getMaterialMut(mc->material);
            if (!mat)
                return;

            const uint32_t newId = impl_->loadTextureForMaterial(path);
            if (newId == 0)
                return;
            switch (fieldId)
            {
                case 210:
                    impl_->rebindTexture(&mat->albedoMapId, newId);
                    break;
                case 211:
                    impl_->rebindTexture(&mat->normalMapId, newId);
                    break;
                case 212:
                    impl_->rebindTexture(&mat->ormMapId, newId);
                    break;
                case 213:
                    impl_->rebindTexture(&mat->emissiveMapId, newId);
                    break;
                case 214:
                    impl_->rebindTexture(&mat->occlusionMapId, newId);
                    break;
                default:
                    break;
            }
            impl_->propertiesDirty = true;
            impl_->viewportDirty = true;
        });

    // Wire native properties view texture-clear callback.
    // Writes zero into the corresponding Material slot; the actual texture
    // lifecycle in RenderResources is managed elsewhere.
    impl_->window->propertiesView()->setTextureClearedCallback(
        [this](int fieldId)
        {
            if (impl_->editorState.playState() != EditorPlayState::Editing)
                return;
            EntityID entity = impl_->editorState.primarySelection();
            if (entity == INVALID_ENTITY)
                return;
            auto* mc = impl_->registry.get<MaterialComponent>(entity);
            if (!mc)
                return;
            Material* mat = impl_->resources.getMaterialMut(mc->material);
            if (!mat)
                return;

            // Route through rebindTexture so the cleared slot's previous
            // texture has its material ref count decremented; if no other
            // material references it, the asset handle is released and
            // the RenderResources slot is freed.
            switch (fieldId)
            {
                case 210:
                    impl_->rebindTexture(&mat->albedoMapId, 0);
                    break;
                case 211:
                    impl_->rebindTexture(&mat->normalMapId, 0);
                    break;
                case 212:
                    impl_->rebindTexture(&mat->ormMapId, 0);
                    break;
                case 213:
                    impl_->rebindTexture(&mat->emissiveMapId, 0);
                    break;
                case 214:
                    impl_->rebindTexture(&mat->occlusionMapId, 0);
                    break;
                default:
                    break;
            }
            impl_->propertiesDirty = true;
            impl_->viewportDirty = true;
        });

    impl_->window->propertiesView()->setBoolChangedCallback(
        [this](int fieldId, bool newValue)
        {
            EntityID entity = impl_->editorState.primarySelection();
            if (entity == INVALID_ENTITY)
                return;
            if (fieldId == 50)
            {
                if (newValue && !impl_->registry.has<VisibleTag>(entity))
                    impl_->registry.emplace<VisibleTag>(entity);
                else if (!newValue && impl_->registry.has<VisibleTag>(entity))
                    impl_->registry.remove<VisibleTag>(entity);
                impl_->propertiesDirty = true;
                impl_->viewportDirty = true;
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

    // -- Animation panel (bottom "Animation" tab) ----------------------------
    // Owns no state of its own; reads the selected entity's AnimatorComponent
    // each frame and drives the native CocoaAnimationView.
    {
        auto* animView = impl_->window->animationView();
        impl_->animationPanel = std::make_unique<AnimationPanel>(
            impl_->registry, impl_->editorState, impl_->animationResources, animView);
        impl_->animationPanel->init();

        if (animView)
        {
            using engine::animation::AnimatorComponent;

            // Helper: resolve the AnimatorComponent for the current primary
            // selection, or nullptr if none.
            auto getAnim = [this]() -> AnimatorComponent*
            {
                ecs::EntityID e = impl_->editorState.primarySelection();
                if (e == ecs::INVALID_ENTITY)
                    return nullptr;
                return impl_->registry.get<AnimatorComponent>(e);
            };

            animView->setPlayCallback(
                [this, getAnim]()
                {
                    if (auto* a = getAnim())
                    {
                        a->flags |= AnimatorComponent::kFlagPlaying;
                        if (impl_->animationPanel)
                            impl_->animationPanel->markDirty();
                    }
                });

            animView->setPauseCallback(
                [this, getAnim]()
                {
                    if (auto* a = getAnim())
                    {
                        a->flags &= ~AnimatorComponent::kFlagPlaying;
                        if (impl_->animationPanel)
                            impl_->animationPanel->markDirty();
                    }
                });

            animView->setStopCallback(
                [this, getAnim]()
                {
                    if (auto* a = getAnim())
                    {
                        a->flags &= ~AnimatorComponent::kFlagPlaying;
                        a->playbackTime = 0.0f;
                        a->flags |= AnimatorComponent::kFlagSampleOnce;
                        if (impl_->animationPanel)
                            impl_->animationPanel->markDirty();
                    }
                });

            animView->setClipSelectedCallback(
                [this, getAnim](int newClipIndex)
                {
                    if (auto* a = getAnim())
                    {
                        if (newClipIndex >= 0 && static_cast<uint32_t>(newClipIndex) <
                                                     impl_->animationResources.clipCount())
                        {
                            a->clipId = static_cast<uint32_t>(newClipIndex);
                            a->playbackTime = 0.0f;
                            a->flags |= AnimatorComponent::kFlagSampleOnce;
                            if (impl_->animationPanel)
                                impl_->animationPanel->markDirty();
                        }
                    }
                });

            animView->setTimeChangedCallback(
                [this, getAnim](float newTime)
                {
                    if (auto* a = getAnim())
                    {
                        a->playbackTime = newTime;
                        a->flags |= AnimatorComponent::kFlagSampleOnce;
                        if (impl_->animationPanel)
                            impl_->animationPanel->markDirty();
                    }
                });

            animView->setSpeedChangedCallback(
                [this, getAnim](float newSpeed)
                {
                    if (auto* a = getAnim())
                    {
                        a->speed = newSpeed;
                        if (impl_->animationPanel)
                            impl_->animationPanel->markDirty();
                    }
                });

            animView->setLoopChangedCallback(
                [this, getAnim](bool looping)
                {
                    if (auto* a = getAnim())
                    {
                        if (looping)
                            a->flags |= AnimatorComponent::kFlagLooping;
                        else
                            a->flags &= ~AnimatorComponent::kFlagLooping;
                        if (impl_->animationPanel)
                            impl_->animationPanel->markDirty();
                    }
                });

            animView->setEventAddedCallback(
                [this, getAnim](float time, const std::string& name)
                {
                    if (auto* a = getAnim())
                    {
                        auto* clip = impl_->animationResources.getClipMut(a->clipId);
                        if (clip)
                        {
                            clip->addEvent(time, name);
                            if (impl_->animationPanel)
                                impl_->animationPanel->markDirty();
                        }
                    }
                });

            animView->setEventRemovedCallback(
                [this, getAnim](int eventIndex)
                {
                    if (auto* a = getAnim())
                    {
                        auto* clip = impl_->animationResources.getClipMut(a->clipId);
                        if (clip && eventIndex >= 0 &&
                            static_cast<size_t>(eventIndex) < clip->events.size())
                        {
                            clip->events.erase(clip->events.begin() + eventIndex);
                            if (impl_->animationPanel)
                                impl_->animationPanel->markDirty();
                        }
                    }
                });

            animView->setEventEditedCallback(
                [this, getAnim](int eventIndex, float newTime, const std::string& newName)
                {
                    if (auto* a = getAnim())
                    {
                        auto* clip = impl_->animationResources.getClipMut(a->clipId);
                        if (clip && eventIndex >= 0 &&
                            static_cast<size_t>(eventIndex) < clip->events.size())
                        {
                            auto& evt = clip->events[eventIndex];
                            evt.name = newName;
                            evt.nameHash = animation::fnv1a(newName.c_str());
                            evt.time = newTime;
                            // Re-sort events by time.
                            std::sort(clip->events.begin(), clip->events.end(),
                                      [](const animation::AnimationEvent& a,
                                         const animation::AnimationEvent& b)
                                      { return a.time < b.time; });
                            if (impl_->animationPanel)
                                impl_->animationPanel->markDirty();
                        }
                    }
                });

            animView->setStateForceSetCallback(
                [this, getAnim](int stateIndex)
                {
                    using engine::animation::AnimStateMachineComponent;
                    ecs::EntityID e = impl_->editorState.primarySelection();
                    if (e == ecs::INVALID_ENTITY)
                        return;
                    auto* smComp = impl_->registry.get<AnimStateMachineComponent>(e);
                    if (!smComp || !smComp->machine)
                        return;
                    if (stateIndex < 0 ||
                        static_cast<size_t>(stateIndex) >= smComp->machine->states.size())
                        return;

                    smComp->currentState = static_cast<uint32_t>(stateIndex);

                    // Sync AnimatorComponent to match the new state.
                    if (auto* a = getAnim())
                    {
                        const auto& st = smComp->machine->states[stateIndex];
                        a->clipId = st.clipId;
                        a->speed = st.speed;
                        a->playbackTime = 0.0f;
                        if (st.loop)
                            a->flags |= AnimatorComponent::kFlagLooping;
                        else
                            a->flags &= ~AnimatorComponent::kFlagLooping;
                        a->flags |= AnimatorComponent::kFlagSampleOnce;
                    }
                    if (impl_->animationPanel)
                        impl_->animationPanel->markDirty();
                });

            animView->setParamChangedCallback(
                [this](const std::string& paramName, float value)
                {
                    using engine::animation::AnimStateMachineComponent;
                    ecs::EntityID e = impl_->editorState.primarySelection();
                    if (e == ecs::INVALID_ENTITY)
                        return;
                    auto* smComp = impl_->registry.get<AnimStateMachineComponent>(e);
                    if (!smComp)
                        return;

                    // Determine if this param is a bool by checking conditions.
                    bool isBool = false;
                    if (smComp->machine)
                    {
                        uint32_t hash = animation::fnv1aHash(paramName);
                        for (const auto& st : smComp->machine->states)
                        {
                            for (const auto& tr : st.transitions)
                            {
                                for (const auto& cond : tr.conditions)
                                {
                                    if (cond.paramHash == hash &&
                                        (cond.compare ==
                                             animation::TransitionCondition::Compare::BoolTrue ||
                                         cond.compare ==
                                             animation::TransitionCondition::Compare::BoolFalse))
                                    {
                                        isBool = true;
                                        break;
                                    }
                                }
                                if (isBool)
                                    break;
                            }
                            if (isBool)
                                break;
                        }
                    }

                    if (isBool)
                        smComp->setBool(paramName, value > 0.5f);
                    else
                        smComp->setFloat(paramName, value);

                    if (impl_->animationPanel)
                        impl_->animationPanel->markDirty();
                });

            // Sync AnimatorComponent to match the state machine's current state.
            auto syncAnimatorToCurrentState = [this]()
            {
                using engine::animation::AnimStateMachineComponent;
                using engine::animation::AnimatorComponent;
                ecs::EntityID e = impl_->editorState.primarySelection();
                if (e == ecs::INVALID_ENTITY)
                    return;
                auto* smComp = impl_->registry.get<AnimStateMachineComponent>(e);
                auto* ac = impl_->registry.get<AnimatorComponent>(e);
                if (!smComp || !smComp->machine || !ac)
                    return;
                if (smComp->currentState >= smComp->machine->states.size())
                    return;
                const auto& st = smComp->machine->states[smComp->currentState];
                ac->clipId = st.clipId;
                ac->speed = st.speed;
                if (st.loop)
                    ac->flags |= AnimatorComponent::kFlagLooping;
                else
                    ac->flags &= ~AnimatorComponent::kFlagLooping;
                ac->flags |= AnimatorComponent::kFlagSampleOnce;
            };

            // Helper to find (or create) a mutable AnimStateMachine for the
            // selected entity. Returns nullptr if entity has no state machine.
            auto getMutableMachine = [this]() -> engine::animation::AnimStateMachine*
            {
                using engine::animation::AnimStateMachineComponent;
                ecs::EntityID e = impl_->editorState.primarySelection();
                if (e == ecs::INVALID_ENTITY)
                    return nullptr;
                auto* smComp = impl_->registry.get<AnimStateMachineComponent>(e);
                if (!smComp || !smComp->machine)
                    return nullptr;
                return const_cast<engine::animation::AnimStateMachine*>(smComp->machine);
            };

            animView->setStateSelectedCallback(
                [this](int stateIndex)
                {
                    if (impl_->animationPanel)
                        impl_->animationPanel->setSelectedState(stateIndex);
                });

            animView->setTransitionSelectedCallback(
                [this](int stateIndex, int transIndex)
                {
                    if (impl_->animationPanel)
                        impl_->animationPanel->setSelectedTransition(stateIndex, transIndex);
                });

            animView->setStateSelectedCallback(
                [this](int stateIndex)
                {
                    if (impl_->animationPanel)
                    {
                        impl_->animationPanel->setSelectedState(stateIndex);
                        impl_->animationPanel->markDirty();
                    }
                });

            animView->setTransitionSelectedCallback(
                [this](int stateIndex, int transIndex)
                {
                    if (impl_->animationPanel)
                    {
                        impl_->animationPanel->setSelectedTransition(stateIndex, transIndex);
                        impl_->animationPanel->markDirty();
                    }
                });

            animView->setStateAddedCallback(
                [this, getMutableMachine, syncAnimatorToCurrentState]()
                {
                    auto* machine = getMutableMachine();
                    if (!machine)
                        return;
                    uint32_t clipId = 0;
                    std::string name = "State_" + std::to_string(machine->states.size());
                    machine->addState(name, clipId, true, 1.0f);
                    syncAnimatorToCurrentState();
                    if (impl_->animationPanel)
                        impl_->animationPanel->markDirty();
                });

            animView->setStateRemovedCallback(
                [this, getMutableMachine, syncAnimatorToCurrentState](int stateIndex)
                {
                    auto* machine = getMutableMachine();
                    if (!machine)
                        return;
                    if (stateIndex < 0 || static_cast<size_t>(stateIndex) >= machine->states.size())
                        return;
                    // Don't remove the last state.
                    if (machine->states.size() <= 1)
                        return;

                    machine->states.erase(machine->states.begin() + stateIndex);

                    // Fix up targetState indices in all transitions.
                    for (auto& st : machine->states)
                    {
                        for (auto it = st.transitions.begin(); it != st.transitions.end();)
                        {
                            if (static_cast<int>(it->targetState) == stateIndex)
                            {
                                it = st.transitions.erase(it);
                            }
                            else
                            {
                                if (static_cast<int>(it->targetState) > stateIndex)
                                    it->targetState--;
                                ++it;
                            }
                        }
                    }

                    // Fix default state.
                    if (machine->defaultState >= static_cast<uint32_t>(machine->states.size()))
                        machine->defaultState = 0;

                    // Fix runtime component currentState.
                    using engine::animation::AnimStateMachineComponent;
                    ecs::EntityID e = impl_->editorState.primarySelection();
                    if (e != ecs::INVALID_ENTITY)
                    {
                        auto* smComp = impl_->registry.get<AnimStateMachineComponent>(e);
                        if (smComp &&
                            smComp->currentState >= static_cast<uint32_t>(machine->states.size()))
                            smComp->currentState = 0;
                    }

                    syncAnimatorToCurrentState();
                    if (impl_->animationPanel)
                    {
                        impl_->animationPanel->setSelectedState(-1);
                        impl_->animationPanel->markDirty();
                    }
                });

            animView->setStateEditedCallback(
                [this, getMutableMachine, syncAnimatorToCurrentState](
                    int stateIndex, const std::string& name, int clipIndex, float speed, bool loop)
                {
                    auto* machine = getMutableMachine();
                    if (!machine)
                        return;
                    if (stateIndex < 0 || static_cast<size_t>(stateIndex) >= machine->states.size())
                        return;
                    auto& st = machine->states[stateIndex];

                    if (!name.empty())
                    {
                        st.name = name;
                        st.nameHash = animation::fnv1aHash(name);
                    }
                    if (clipIndex >= 0)
                        st.clipId = static_cast<uint32_t>(clipIndex);
                    if (speed >= 0.0f)
                        st.speed = speed;
                    st.loop = loop;

                    // If the edited state is the current state, sync the animator.
                    syncAnimatorToCurrentState();

                    if (impl_->animationPanel)
                        impl_->animationPanel->markDirty();
                });

            animView->setTransitionAddedCallback(
                [this, getMutableMachine](int fromState, int toState, float blendDuration)
                {
                    auto* machine = getMutableMachine();
                    if (!machine)
                        return;
                    if (fromState < 0 || static_cast<size_t>(fromState) >= machine->states.size())
                        return;
                    // Default target: next state (wrapping).
                    if (toState < 0)
                    {
                        toState = (fromState + 1) % static_cast<int>(machine->states.size());
                    }
                    machine->addTransition(static_cast<uint32_t>(fromState),
                                           static_cast<uint32_t>(toState), blendDuration);
                    if (impl_->animationPanel)
                        impl_->animationPanel->markDirty();
                });

            animView->setTransitionRemovedCallback(
                [this, getMutableMachine](int fromState, int transIndex)
                {
                    auto* machine = getMutableMachine();
                    if (!machine)
                        return;
                    if (fromState < 0 || static_cast<size_t>(fromState) >= machine->states.size())
                        return;
                    auto& trans = machine->states[fromState].transitions;
                    if (transIndex < 0 || static_cast<size_t>(transIndex) >= trans.size())
                        return;
                    trans.erase(trans.begin() + transIndex);
                    if (impl_->animationPanel)
                    {
                        impl_->animationPanel->setSelectedTransition(fromState, -1);
                        impl_->animationPanel->markDirty();
                    }
                });

            animView->setTransitionEditedCallback(
                [this, getMutableMachine](int fromState, int transIndex, int targetState,
                                          float blendDuration, float exitTime, bool hasExitTime)
                {
                    auto* machine = getMutableMachine();
                    if (!machine)
                        return;
                    if (fromState < 0 || static_cast<size_t>(fromState) >= machine->states.size())
                        return;
                    auto& trans = machine->states[fromState].transitions;
                    if (transIndex < 0 || static_cast<size_t>(transIndex) >= trans.size())
                        return;
                    auto& tr = trans[transIndex];

                    // Partial updates.
                    if (targetState >= 0)
                        tr.targetState = static_cast<uint32_t>(targetState);
                    if (blendDuration >= 0.0f)
                        tr.blendDuration = blendDuration;
                    if (exitTime >= 0.0f)
                        tr.exitTime = exitTime;
                    // hasExitTime is a checkbox; apply only when blend and
                    // exitTime are both sentinel (checkbox-only edit).
                    if (targetState < 0 && blendDuration < 0.0f && exitTime < 0.0f)
                        tr.hasExitTime = hasExitTime;

                    if (impl_->animationPanel)
                        impl_->animationPanel->markDirty();
                });

            animView->setConditionAddedCallback(
                [this, getMutableMachine](int fromState, int transIndex, const std::string& param,
                                          int compare, float threshold)
                {
                    auto* machine = getMutableMachine();
                    if (!machine)
                        return;
                    if (fromState < 0 || static_cast<size_t>(fromState) >= machine->states.size())
                        return;
                    auto& trans = machine->states[fromState].transitions;
                    if (transIndex < 0 || static_cast<size_t>(transIndex) >= trans.size())
                        return;
                    animation::TransitionCondition cond;
                    cond.paramName = param;
                    cond.paramHash = animation::fnv1aHash(param);
                    cond.compare = static_cast<animation::TransitionCondition::Compare>(compare);
                    cond.threshold = threshold;
                    trans[transIndex].conditions.push_back(std::move(cond));

                    // Register param name in machine.
                    machine->paramNames[animation::fnv1aHash(param)] = param;

                    if (impl_->animationPanel)
                        impl_->animationPanel->markDirty();
                });

            animView->setConditionRemovedCallback(
                [this, getMutableMachine](int fromState, int transIndex, int condIndex)
                {
                    auto* machine = getMutableMachine();
                    if (!machine)
                        return;
                    if (fromState < 0 || static_cast<size_t>(fromState) >= machine->states.size())
                        return;
                    auto& trans = machine->states[fromState].transitions;
                    if (transIndex < 0 || static_cast<size_t>(transIndex) >= trans.size())
                        return;
                    auto& conds = trans[transIndex].conditions;
                    // If condIndex is -1, remove the last condition.
                    int resolved = (condIndex < 0) ? static_cast<int>(conds.size()) - 1 : condIndex;
                    if (resolved >= 0 && static_cast<size_t>(resolved) < conds.size())
                        conds.erase(conds.begin() + resolved);
                    if (impl_->animationPanel)
                        impl_->animationPanel->markDirty();
                });

            animView->setParamAddedCallback(
                [this, getMutableMachine](const std::string& name, bool isBool)
                {
                    using engine::animation::AnimStateMachineComponent;
                    auto* machine = getMutableMachine();
                    if (!machine)
                        return;
                    // Generate a unique name.
                    std::string paramName = name;
                    uint32_t hash = animation::fnv1aHash(paramName);
                    int suffix = 0;
                    while (machine->paramNames.count(hash))
                    {
                        paramName = name + "_" + std::to_string(suffix++);
                        hash = animation::fnv1aHash(paramName);
                    }
                    machine->paramNames[hash] = paramName;

                    // Set initial value on the component.
                    ecs::EntityID e = impl_->editorState.primarySelection();
                    if (e != ecs::INVALID_ENTITY)
                    {
                        auto* smComp = impl_->registry.get<AnimStateMachineComponent>(e);
                        if (smComp)
                        {
                            if (isBool)
                                smComp->setBool(paramName, false);
                            else
                                smComp->setFloat(paramName, 0.0f);
                        }
                    }

                    if (impl_->animationPanel)
                        impl_->animationPanel->markDirty();
                });
        }
    }

    startupSamples.push_back({"editor panels", startupNow()});

    impl_->gizmo =
        std::make_unique<TransformGizmo>(impl_->registry, impl_->editorState, *impl_->window);
    impl_->gizmoRenderer.init();
    startupSamples.push_back({"gizmo", startupNow()});

    // -- Procedural IBL + skybox ----------------------------------------------
    // The IBL data (irradiance + prefiltered + BRDF LUT) is precomputed and
    // shipped as assets/env/default.env (~2.5 MB binary blob produced by
    // tools/bake_default_env). Loading the file is sub-50ms; regenerating
    // from scratch via IblResources::generateDefault() takes ~5 seconds.
    // Falls back to the slow path if the file is missing or has the wrong
    // version stamp (e.g. someone changed the procedural sky model and
    // forgot to re-bake).
    {
        // Same asset-path resolver used for the HUD font further below —
        // walks cwd-relative candidates first, then upward from
        // _NSGetExecutablePath. Inlined here because the helper is local
        // to the HUD font block.
        auto findAssetEarly = [](const char* relPath) -> std::string
        {
            const char* prefixes[] = {"", "../", "../../", "../../../"};
            for (const char* p : prefixes)
            {
                std::string c = std::string(p) + relPath;
                if (FILE* f = std::fopen(c.c_str(), "rb"))
                {
                    std::fclose(f);
                    return c;
                }
            }
            char execPath[4096] = {};
            uint32_t execPathSize = sizeof(execPath);
            if (_NSGetExecutablePath(execPath, &execPathSize) == 0)
            {
                std::string base(execPath);
                auto slash = base.find_last_of('/');
                if (slash != std::string::npos)
                    base.resize(slash);
                std::string prefix = base + "/";
                for (int depth = 0; depth < 6; ++depth)
                {
                    std::string c = prefix + relPath;
                    if (FILE* f = std::fopen(c.c_str(), "rb"))
                    {
                        std::fclose(f);
                        return c;
                    }
                    prefix += "../";
                }
            }
            return relPath;
        };

        const std::string envPath = findAssetEarly("assets/env/default.env");
        auto loaded = engine::assets::loadEnvironmentAsset(envPath);
        if (loaded.has_value())
        {
            impl_->iblResources.upload(*loaded);
        }
        else
        {
            fprintf(stderr,
                    "EditorApp: assets/env/default.env not found or invalid (looked at "
                    "%s) — regenerating procedural sky from scratch (~5 s)\n",
                    envPath.c_str());
            impl_->iblResources.generateDefault();
        }
    }
    startupSamples.push_back({"IBL load+upload", startupNow()});
    impl_->skybox.init();
    startupSamples.push_back({"skybox", startupNow()});

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
    impl_->assetManager->registerLoader(std::make_unique<engine::assets::TextureLoader>());
    startupSamples.push_back({"asset manager + loaders", startupNow()});

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
    startupSamples.push_back({"physics (Jolt)", startupNow()});

    // -- HUD font (JetBrains Mono via MSDF) -----------------------------------
    // Resolve the asset paths against several candidates so the editor works
    // whether the user runs ./build/sama_editor from the repo root or from
    // inside the build/ directory.
    impl_->uiRenderer.init();
    auto findAsset = [](const char* relPath) -> std::string
    {
        const char* prefixes[] = {"", "../", "../../", "../../../"};
        for (const char* p : prefixes)
        {
            std::string c = std::string(p) + relPath;
            if (FILE* f = std::fopen(c.c_str(), "rb"))
            {
                std::fclose(f);
                return c;
            }
        }
        char execPath[4096] = {};
        uint32_t execPathSize = sizeof(execPath);
        if (_NSGetExecutablePath(execPath, &execPathSize) == 0)
        {
            std::string base(execPath);
            auto slash = base.find_last_of('/');
            if (slash != std::string::npos)
                base.resize(slash);
            std::string prefix = base + "/";
            for (int depth = 0; depth < 6; ++depth)
            {
                std::string c = prefix + relPath;
                if (FILE* f = std::fopen(c.c_str(), "rb"))
                {
                    std::fclose(f);
                    return c;
                }
                prefix += "../";
            }
        }
        return relPath;
    };
    const std::string hudJson = findAsset("assets/fonts/JetBrainsMono-msdf.json");
    const std::string hudPng = findAsset("assets/fonts/JetBrainsMono-msdf.png");
    impl_->hudFontLoaded = impl_->hudFont.loadFromFile(hudJson.c_str(), hudPng.c_str());
    if (!impl_->hudFontLoaded)
    {
        fprintf(stderr,
                "EditorApp: failed to load HUD font (%s) — HUD will fall back to "
                "bgfx debug text\n",
                hudJson.c_str());
    }
    startupSamples.push_back({"HUD font (JBM MSDF)", startupNow()});

    // -- Timing ---------------------------------------------------------------
    impl_->prevTime =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    // Flush initial resource uploads.
    bgfx::frame();
    startupSamples.push_back({"first bgfx::frame", startupNow()});

    EditorLog::instance().info("Sama Editor initialized");

    // -- Startup profile dump -------------------------------------------------
    // Print a per-phase breakdown to stderr. Each row shows the elapsed time
    // for that phase (delta from the previous checkpoint) and the cumulative
    // total. The phase that costs the most is usually the right place to
    // start any startup-time optimization work.
    {
        fprintf(stderr, "\n[startup] sama_editor init breakdown (cwd: %s)\n",
                std::filesystem::current_path().string().c_str());
        fprintf(stderr, "  %-32s  %8s  %8s\n", "phase", "delta", "total");
        fprintf(stderr, "  %-32s  %8s  %8s\n", "--------------------------------", "--------",
                "--------");
        double prev = 0.0;
        for (const auto& s : startupSamples)
        {
            const double delta = s.endMs - prev;
            fprintf(stderr, "  %-32s  %7.1fms %7.1fms\n", s.name, delta, s.endMs);
            prev = s.endMs;
        }
        fprintf(stderr, "\n");
        fflush(stderr);
    }

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
            impl_->postProcess.resize(impl_->fbW, impl_->fbH);
            impl_->viewportDirty = true;
        }

        if (fbW == 0 || fbH == 0)
        {
            bgfx::frame();
            continue;
        }

        // -- Camera (only when mouse is over viewport) ------------------------
        if (impl_->window->isMouseOverViewport() && impl_->window->isRightMouseDown())
        {
            const double dx = impl_->window->mouseDeltaX();
            const double dy = impl_->window->mouseDeltaY();
            if (std::abs(dx) > 0.0 || std::abs(dy) > 0.0)
            {
                impl_->camera.orbit(static_cast<float>(dx), -static_cast<float>(dy), 0.25f);
                impl_->viewportDirty = true;
            }
        }

        if (impl_->window->isMouseOverViewport())
        {
            double scrollY = impl_->window->scrollDeltaY();
            if (std::abs(scrollY) > 0.01)
            {
                impl_->camera.zoom(static_cast<float>(scrollY * 0.1), 1.0f, 1.0f, 100.0f);
                impl_->viewportDirty = true;
            }
        }

        // -- Gizmo update (before transform system) ---------------------------
        float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
        glm::mat4 viewMtx = impl_->camera.view();
        glm::mat4 projMtx = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 200.0f);
        const GizmoAxis prevHover = impl_->gizmo->hoveredAxis();
        const bool wasDragging = impl_->gizmo->isDragging();
        const GizmoMode prevMode = impl_->gizmo->mode();
        impl_->gizmo->update(dt, viewMtx, projMtx);
        // Hover-color change, mode change (W/E/R), or any drag activity
        // (start / continue / end) means the gizmo overlay needs a fresh frame.
        if (impl_->gizmo->hoveredAxis() != prevHover || impl_->gizmo->mode() != prevMode ||
            impl_->gizmo->isDragging() || wasDragging)
        {
            impl_->viewportDirty = true;
        }

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
            impl_->viewportDirty = true;

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
                    // Hard reset editor-owned textures — any remaining refs
                    // belonged to the entities we just destroyed.
                    impl_->clearEditorTextures();
                    impl_->sceneSerializer.loadScene(path.c_str(), impl_->registry,
                                                     impl_->resources, *impl_->assetManager);
                    impl_->currentScenePath = path;
                    impl_->hierarchyDirty = true;
                    impl_->propertiesDirty = true;
                    impl_->viewportDirty = true;
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
                // Hard reset editor-owned textures alongside the entities.
                impl_->clearEditorTextures();
                impl_->currentScenePath.clear();
                impl_->commandStack.clear();
                impl_->editorState.clearSelection();
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
                impl_->viewportDirty = true;
                impl_->window->setWindowTitle("Sama Editor");
                EditorLog::instance().info("New scene");
            }
            else if (action == "undo")
            {
                impl_->commandStack.undo();
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
                impl_->viewportDirty = true;
            }
            else if (action == "redo")
            {
                impl_->commandStack.redo();
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
                impl_->viewportDirty = true;
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
                    impl_->viewportDirty = true;
                }
            }
            else if (action == "create_empty")
            {
                auto cmd =
                    std::make_unique<CreateEntityCommand>(impl_->registry, impl_->editorState);
                impl_->commandStack.execute(std::move(cmd));
                impl_->hierarchyDirty = true;
                impl_->propertiesDirty = true;
                impl_->viewportDirty = true;
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
                impl_->viewportDirty = true;
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
                impl_->viewportDirty = true;
            }
            else if (action.rfind("add_component:", 0) == 0)
            {
                impl_->addComponentToSelection(action.substr(14));
                impl_->viewportDirty = true;
            }
            else if (action == "load_environment")
            {
                std::string path = impl_->window->showOpenDialog("env");
                if (!path.empty())
                {
                    auto loaded = engine::assets::loadEnvironmentAsset(path);
                    if (loaded.has_value())
                    {
                        // upload() destroys the previous bgfx handles before
                        // creating new ones, so swapping is safe at any time.
                        if (impl_->iblResources.upload(*loaded))
                        {
                            const auto slash = path.find_last_of('/');
                            const std::string name =
                                slash == std::string::npos ? path : path.substr(slash + 1);
                            snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                     "Loaded environment: %.80s", name.c_str());
                            impl_->statusTimer = 3.0f;
                            impl_->viewportDirty = true;
                            EditorLog::instance().info("Loaded environment");
                        }
                        else
                        {
                            snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                     "Environment upload failed");
                            impl_->statusTimer = 3.0f;
                            EditorLog::instance().error("IblResources::upload returned false");
                        }
                    }
                    else
                    {
                        snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                 "Environment load failed (bad magic / version)");
                        impl_->statusTimer = 3.0f;
                        EditorLog::instance().error("loadEnvironmentAsset returned nullopt");
                    }
                }
            }
            else if (action == "load_environment_hdr")
            {
                std::string path = impl_->window->showOpenDialog("hdr");
                if (!path.empty())
                {
                    auto loaded = engine::assets::loadHdrEnvironment(path);
                    if (loaded.has_value())
                    {
                        if (impl_->iblResources.upload(*loaded))
                        {
                            const auto slash = path.find_last_of('/');
                            const std::string name =
                                slash == std::string::npos ? path : path.substr(slash + 1);
                            snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                     "Loaded HDR: %.80s", name.c_str());
                            impl_->statusTimer = 3.0f;
                            impl_->viewportDirty = true;
                            EditorLog::instance().info("Loaded HDR environment");
                        }
                        else
                        {
                            snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                     "HDR upload failed");
                            impl_->statusTimer = 3.0f;
                            EditorLog::instance().error(
                                "IblResources::upload returned false for HDR");
                        }
                    }
                    else
                    {
                        snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                 "HDR load failed (stb_image could not parse)");
                        impl_->statusTimer = 3.0f;
                        EditorLog::instance().error("loadHdrEnvironment returned nullopt");
                    }
                }
            }
            else if (action == "load_environment_cubemap")
            {
                // Accept any container bimg knows how to parse: KTX1/KTX2/DDS.
                std::string path = impl_->window->showOpenDialogMultiExt({"ktx", "ktx2", "dds"},
                                                                         "Load Cubemap (DDS/KTX)");
                if (!path.empty())
                {
                    auto loaded = engine::assets::loadCubemapEnvironment(path);
                    if (loaded.has_value())
                    {
                        if (impl_->iblResources.upload(*loaded))
                        {
                            const auto slash = path.find_last_of('/');
                            const std::string name =
                                slash == std::string::npos ? path : path.substr(slash + 1);
                            snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                     "Loaded cubemap: %.80s", name.c_str());
                            impl_->statusTimer = 3.0f;
                            impl_->viewportDirty = true;
                            EditorLog::instance().info("Loaded cubemap environment");
                        }
                        else
                        {
                            snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                     "Cubemap upload failed");
                            impl_->statusTimer = 3.0f;
                            EditorLog::instance().error(
                                "IblResources::upload returned false for cubemap");
                        }
                    }
                    else
                    {
                        snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                                 "Cubemap load failed (bimg could not parse)");
                        impl_->statusTimer = 3.0f;
                        EditorLog::instance().error("loadCubemapEnvironment returned nullopt");
                    }
                }
            }
            else if (action == "reset_environment")
            {
                // Re-run the same asset-path search used by init() to locate
                // the bundled default sky, then hot-swap it via IblResources.
                auto findAsset = [](const char* relPath) -> std::string
                {
                    const char* prefixes[] = {"", "../", "../../", "../../../"};
                    for (const char* p : prefixes)
                    {
                        std::string c = std::string(p) + relPath;
                        if (FILE* f = std::fopen(c.c_str(), "rb"))
                        {
                            std::fclose(f);
                            return c;
                        }
                    }
                    char execPath[4096] = {};
                    uint32_t execPathSize = sizeof(execPath);
                    if (_NSGetExecutablePath(execPath, &execPathSize) == 0)
                    {
                        std::string base(execPath);
                        auto slash = base.find_last_of('/');
                        if (slash != std::string::npos)
                            base.resize(slash);
                        std::string prefix = base + "/";
                        for (int depth = 0; depth < 6; ++depth)
                        {
                            std::string c = prefix + relPath;
                            if (FILE* f = std::fopen(c.c_str(), "rb"))
                            {
                                std::fclose(f);
                                return c;
                            }
                            prefix += "../";
                        }
                    }
                    return relPath;
                };

                const std::string envPath = findAsset("assets/env/default.env");
                auto loaded = engine::assets::loadEnvironmentAsset(envPath);
                if (loaded.has_value() && impl_->iblResources.upload(*loaded))
                {
                    snprintf(impl_->statusMsg, sizeof(impl_->statusMsg), "Reset to default sky");
                    impl_->statusTimer = 3.0f;
                    impl_->viewportDirty = true;
                    EditorLog::instance().info("Reset environment to default");
                }
                else
                {
                    snprintf(impl_->statusMsg, sizeof(impl_->statusMsg),
                             "Reset to default sky failed");
                    impl_->statusTimer = 3.0f;
                    EditorLog::instance().error(
                        "reset_environment: failed to load assets/env/default.env");
                }
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

                        engine::assets::GltfSceneSpawner::spawn(
                            *asset, impl_->registry, impl_->resources, impl_->animationResources);

                        // Auto-load sidecar files (events / state machine).
                        {
                            std::string basePath = path;
                            auto dotPos = basePath.rfind('.');
                            if (dotPos != std::string::npos)
                                basePath = basePath.substr(0, dotPos);

                            std::string eventsPath = basePath + ".events.json";
                            std::string smPath = basePath + ".statemachine.json";

                            if (std::ifstream(eventsPath).good())
                            {
                                if (engine::animation::loadEvents(impl_->animationResources,
                                                                  eventsPath))
                                {
                                    EditorLog::instance().info("Loaded animation events sidecar");
                                }
                            }

                            if (std::ifstream(smPath).good())
                            {
                                auto sm = std::make_shared<engine::animation::AnimStateMachine>();
                                if (engine::animation::loadStateMachine(
                                        *sm, impl_->animationResources, smPath))
                                {
                                    impl_->importedStateMachines.push_back(sm);
                                    // Attach to all newly spawned entities that have
                                    // AnimatorComponent (deferred until after entity discovery
                                    // below; store pointer for now).
                                    EditorLog::instance().info("Loaded state machine sidecar");
                                }
                            }
                        }

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

                        // Attach loaded state machine to animated entities.
                        if (!impl_->importedStateMachines.empty())
                        {
                            auto* sm = impl_->importedStateMachines.back().get();
                            for (EntityID e : newEntities)
                            {
                                if (impl_->registry.has<engine::animation::AnimatorComponent>(e) &&
                                    !impl_->registry
                                         .has<engine::animation::AnimStateMachineComponent>(e))
                                {
                                    engine::animation::AnimStateMachineComponent smComp;
                                    smComp.machine = sm;
                                    smComp.currentState = sm->defaultState;
                                    // Initialize all known parameters to defaults.
                                    for (const auto& [hash, name] : sm->paramNames)
                                        smComp.params[hash] = 0.0f;
                                    impl_->registry
                                        .emplace<engine::animation::AnimStateMachineComponent>(
                                            e, std::move(smComp));
                                }
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
                    impl_->viewportDirty = true;
                }
            }
            else if (action == "build_android_low" || action == "build_android_mid" ||
                     action == "build_android_high" || action == "build_android_run")
            {
                if (impl_->androidBuildRunning.load())
                {
                    EditorLog::instance().warning(
                        "Android build already in progress; ignoring request.");
                }
                else
                {
                    AndroidBuildSettings settings = impl_->window->loadAndroidBuildSettings();
                    std::string tier = settings.defaultTier;
                    bool andRun = false;

                    if (action == "build_android_low")
                        tier = "low";
                    else if (action == "build_android_mid")
                        tier = "mid";
                    else if (action == "build_android_high")
                        tier = "high";
                    else  // build_android_run
                        andRun = true;

                    if (tier != "low" && tier != "mid" && tier != "high")
                        tier = "mid";

                    // Honour the "Build & Run after build" persisted toggle
                    // so per-tier menu items also auto-install + launch when
                    // the user has enabled the preference in Settings. The
                    // explicit Build & Run menu item is unchanged (always
                    // forces andRun=true regardless of the toggle).
                    if (settings.buildAndRun)
                        andRun = true;

                    // For Build & Run, sanity-check adb up-front so we
                    // don't spend ~2 minutes building before noticing no
                    // device is connected. The actual launch still
                    // re-queries after install in case the user plugs in
                    // a phone mid-build.
                    bool aborted = false;
                    if (andRun)
                    {
                        auto devices = impl_->window->queryAdbDevices();
                        if (devices.empty())
                        {
                            impl_->window->showAlert(
                                "No Android device connected",
                                "Build & Run requires an Android device or running emulator.\n"
                                "\n"
                                "1. Connect a device via USB and enable USB debugging\n"
                                "   (Settings > System > Developer options > USB debugging),\n"
                                "   OR start an emulator (e.g. `emulator -avd sama_mid`).\n"
                                "2. Confirm with `adb devices` (the device should be listed).\n"
                                "3. Then re-run Build > Android > Build & Run.");
                            EditorLog::instance().warning(
                                "Build & Run aborted: no adb device available.");
                            aborted = true;
                        }
                    }

                    if (!aborted)
                    {
                        EditorLog::instance().info(("Building Android APK (" + tier + " tier" +
                                                    (andRun ? ", Build & Run" : "") + ")...")
                                                       .c_str());
                        impl_->window->setBuildStatus(
                            ("Starting Android APK build (" + tier + ")...").c_str(),
                            CocoaEditorWindow::BuildStatusKind::Running);

                        impl_->androidBuildRunning = true;
                        impl_->androidBuildCancelRequested = false;
                        impl_->androidBuildPid = -1;
                        impl_->androidBuildAndRunActive = andRun;

                        auto* impl = impl_.get();
                        auto* window = impl_->window.get();

                        // Cancel handler: SIGTERM the running build process.
                        // The build thread's drain loop will see EOF, waitpid
                        // returns the signal, and the post-build logic logs
                        // the cancellation. Marking `androidBuildCancelRequested`
                        // suppresses the post-build install/launch step.
                        window->setBuildCancelHandler(
                            [impl]()
                            {
                                pid_t pid = impl->androidBuildPid.load();
                                if (pid > 0)
                                {
                                    impl->androidBuildCancelRequested = true;
                                    EditorLog::instance().warning(
                                        ("Cancelling Android build (pid=" + std::to_string(pid) +
                                         ")")
                                            .c_str());
                                    kill(pid, SIGTERM);
                                }
                            });

                        std::thread buildThread(
                            [impl, window, tier, andRun, settings]()
                            {
                                // Build argv for build_apk.sh.
                                std::vector<std::string> argv;
                                argv.push_back("./android/build_apk.sh");
                                argv.push_back("--tier");
                                argv.push_back(tier);
                                if (!settings.packageId.empty() &&
                                    settings.packageId != "com.sama.game")
                                {
                                    argv.push_back("--package");
                                    argv.push_back(settings.packageId);
                                }
                                if (!settings.outputApkPath.empty())
                                {
                                    argv.push_back("--output");
                                    argv.push_back(settings.outputApkPath);
                                }
                                if (!settings.keystorePath.empty())
                                {
                                    argv.push_back("--keystore");
                                    argv.push_back(settings.keystorePath);
                                    if (!settings.keystorePasswordEnvVar.empty())
                                    {
                                        argv.push_back("--ks-pass-env");
                                        argv.push_back(settings.keystorePasswordEnvVar);
                                    }
                                }
                                if (andRun)
                                {
                                    // Use --debug + --install so the script signs
                                    // with the auto-generated debug keystore (no
                                    // password prompt) and pushes via adb.
                                    argv.push_back("--debug");
                                    argv.push_back("--install");
                                }

                                SpawnedProcess proc = spawnBuildProcess(argv, {});
                                if (proc.pid < 0 || !proc.readFile)
                                {
                                    EditorLog::instance().error("Failed to spawn build_apk.sh");
                                    window->setBuildStatus(
                                        "Build failed to start",
                                        CocoaEditorWindow::BuildStatusKind::Failure);
                                    impl->androidBuildRunning = false;
                                    impl->androidBuildPid = -1;
                                    window->setBuildCancelHandler(nullptr);
                                    return;
                                }
                                impl->androidBuildPid = proc.pid;

                                char buffer[1024];
                                std::string lastPhase;
                                while (fgets(buffer, sizeof(buffer), proc.readFile))
                                {
                                    std::string line(buffer);
                                    if (!line.empty() && line.back() == '\n')
                                        line.pop_back();
                                    EditorLog::instance().info(line.c_str());

                                    std::string phase = buildPhaseFromLine(line);
                                    if (!phase.empty() && phase != lastPhase)
                                    {
                                        lastPhase = phase;
                                        window->setBuildStatus(
                                            phase.c_str(),
                                            CocoaEditorWindow::BuildStatusKind::Running);
                                    }
                                }
                                fclose(proc.readFile);
                                int status = 0;
                                waitpid(proc.pid, &status, 0);
                                impl->androidBuildPid = -1;

                                int exitCode = -1;
                                bool signalled = false;
                                if (WIFEXITED(status))
                                    exitCode = WEXITSTATUS(status);
                                else if (WIFSIGNALED(status))
                                    signalled = true;

                                bool wasCancelled = impl->androidBuildCancelRequested.load();

                                if (wasCancelled || signalled)
                                {
                                    EditorLog::instance().warning("Android APK build cancelled.");
                                    window->setBuildStatus(
                                        "Build cancelled",
                                        CocoaEditorWindow::BuildStatusKind::Failure);
                                }
                                else if (exitCode == 0)
                                {
                                    // Try to extract APK size from the script's
                                    // "Size: ..." line for a friendlier final
                                    // status. Fall back to plain success.
                                    std::string outPath =
                                        settings.outputApkPath.empty()
                                            ? std::string("build/android/Game.apk")
                                            : settings.outputApkPath;
                                    std::string sizeStr;
                                    std::error_code ec;
                                    auto sz = std::filesystem::file_size(outPath, ec);
                                    if (!ec)
                                    {
                                        double mb = sz / (1024.0 * 1024.0);
                                        char buf[64];
                                        snprintf(buf, sizeof(buf), "%.2f MB", mb);
                                        sizeStr = buf;
                                    }
                                    std::string okMsg = sizeStr.empty()
                                                            ? std::string("Build succeeded")
                                                            : ("Build succeeded (" + sizeStr + ")");
                                    EditorLog::instance().info(
                                        ("Android APK build complete: " + okMsg).c_str());
                                    window->setBuildStatus(
                                        okMsg.c_str(), CocoaEditorWindow::BuildStatusKind::Success);

                                    if (andRun)
                                    {
                                        // Launch on device. The build script
                                        // already ran `adb install`; we only
                                        // need `am start` here.
                                        auto devices = window->queryAdbDevices();
                                        if (devices.empty())
                                        {
                                            EditorLog::instance().warning(
                                                "Build & Run: no device available for launch.");
                                        }
                                        else
                                        {
                                            std::string serial = settings.lastDeviceSerial;
                                            if (serial.empty() ||
                                                std::none_of(devices.begin(), devices.end(),
                                                             [&](const auto& d)
                                                             { return d.serial == serial; }))
                                            {
                                                serial = devices.front().serial;
                                            }
                                            std::string activity =
                                                settings.packageId + "/android.app.NativeActivity";
                                            std::string launchCmd = "adb -s " + serial +
                                                                    " shell am start -n " +
                                                                    activity + " 2>&1";
                                            EditorLog::instance().info(
                                                ("Launching: " + launchCmd).c_str());
                                            FILE* lp = popen(launchCmd.c_str(), "r");
                                            if (lp)
                                            {
                                                char lb[256];
                                                while (fgets(lb, sizeof(lb), lp))
                                                {
                                                    std::string ll(lb);
                                                    if (!ll.empty() && ll.back() == '\n')
                                                        ll.pop_back();
                                                    EditorLog::instance().info(ll.c_str());
                                                }
                                                int lrc = pclose(lp);
                                                if (lrc == 0)
                                                {
                                                    window->setBuildStatus(
                                                        ("Launched on " + serial).c_str(),
                                                        CocoaEditorWindow::BuildStatusKind::
                                                            Success);
                                                }
                                                else
                                                {
                                                    EditorLog::instance().warning(
                                                        "adb am start returned non-zero exit.");
                                                }
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    std::string failMsg =
                                        "Build failed (exit " + std::to_string(exitCode) + ")";
                                    EditorLog::instance().error(
                                        ("Android APK build failed: " + std::to_string(exitCode))
                                            .c_str());
                                    window->setBuildStatus(
                                        failMsg.c_str(),
                                        CocoaEditorWindow::BuildStatusKind::Failure);
                                }

                                impl->androidBuildRunning = false;
                                impl->androidBuildAndRunActive = false;
                                window->setBuildCancelHandler(nullptr);
                            });
                        buildThread.detach();
                    }  // !aborted
                }
            }
            else if (action == "build_android_settings")
            {
                AndroidBuildSettings s = impl_->window->loadAndroidBuildSettings();
                if (impl_->window->showAndroidBuildSettingsDialog(s))
                {
                    EditorLog::instance().info(
                        ("Android build settings saved (default tier: " + s.defaultTier + ")")
                            .c_str());
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
            impl_->viewportDirty = true;
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
                    impl_->viewportDirty = true;

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
            impl_->viewportDirty = true;
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
                impl_->viewportDirty = true;
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
                impl_->viewportDirty = true;
            }
        }

        // Decrement status timer.
        if (impl_->statusTimer > 0.0f)
        {
            impl_->statusTimer -= dt;
            // Status message is overlaid on the viewport; keep redrawing while
            // it's visible (and one extra frame after it expires so the message
            // gets cleared).
            impl_->viewportDirty = true;
        }

        // The "Add Component" overlay appears in the HUD; redraw while it's open.
        if (impl_->addComponentMenuOpen)
        {
            impl_->viewportDirty = true;
        }

        // -- Physics simulation (Play mode only) ------------------------------
        if (impl_->editorState.playState() == EditorPlayState::Playing)
        {
            impl_->physicsSys.update(impl_->registry, impl_->physics, dt);
            // While the simulation is running, the world geometry is
            // continuously updated; force a viewport redraw every frame.
            impl_->viewportDirty = true;
        }

        // -- Transform system -------------------------------------------------
        impl_->transformSys.update(impl_->registry);

        // -- Render -----------------------------------------------------------
        const auto W = impl_->fbW;
        const auto H = impl_->fbH;

        // Touch views 1..8 with minimal 1x1 clear to prevent pink artifacts.
        // Always done — bgfx needs every used view touched per frame so the
        // swapchain can present (idle frames keep the previously-rendered
        // viewport on screen rather than going blank).
        bgfx::setViewRect(0, 0, 0, W, H);
        bgfx::setViewClear(0, BGFX_CLEAR_NONE);
        bgfx::touch(0);
        for (bgfx::ViewId v = 1; v < kViewOpaque; ++v)
        {
            bgfx::setViewRect(v, 0, 0, 1, 1);
            bgfx::setViewClear(v, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000FF, 1.0f, 0);
            bgfx::touch(v);
        }

        // Main scene view setup is always applied (cheap; just state). When
        // viewportDirty is false we still touch the view but skip the heavy
        // DrawCallBuildSystem / FrustumCullSystem / selection / skybox / gizmo
        // submission below, so the GPU just re-presents the previous frame.
        const bool renderViewport = impl_->viewportDirty;

        // Main scene on kViewOpaque (view 9).  Routes into the post-process
        // HDR scene FB so PBR's linear HDR output gets tonemapped to the
        // backbuffer in postProcess.submit() below.  Clear is only set when
        // dirty so idle frames preserve the previously-rendered scene FB —
        // the tonemap pass keeps presenting it without a re-draw.
        bgfx::setViewRect(kViewOpaque, 0, 0, W, H);
        bgfx::setViewFrameBuffer(kViewOpaque, impl_->postProcess.resources().sceneFb());
        if (renderViewport)
        {
            // Clear stencil too — the selection-outline pass below relies on
            // a clean (=0) stencil buffer in the HDR scene FB's D24S8
            // depth-stencil attachment.
            bgfx::setViewClear(kViewOpaque,
                               BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, 0x303030FF,
                               1.0f, 0);
        }
        else
        {
            bgfx::setViewClear(kViewOpaque, BGFX_CLEAR_NONE);
        }
        bgfx::setViewTransform(kViewOpaque, glm::value_ptr(viewMtx), glm::value_ptr(projMtx));
        if (!renderViewport)
        {
            bgfx::touch(kViewOpaque);
        }

        if (renderViewport)
        {
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

            // -- Skinned PBR pass (skeletal animation)
            // ---------------------------------
            const auto* boneBuffer = impl_->animationSystem.boneBuffer();
            if (boneBuffer)
            {
                impl_->drawCallSys.updateSkinned(impl_->registry, impl_->resources,
                                                 impl_->skinnedPbrProgram, impl_->uniforms, frame,
                                                 boneBuffer);
            }

            // -- Selection outline (single-pass stencil) --------------------------
            // Replaces the pre-Phase-7 wireframe-with-gold-material hack.  Two
            // bgfx draws targeting the HDR scene FB:
            //   View kViewEditorSelectionStencil (11): position-only draw with
            //     stencil-write state, color writes off, depth test on — marks
            //     stencil = 1 wherever the selected mesh's *visible* surface
            //     hits the framebuffer.
            //   View kViewEditorSelectionOutline (12): inflated re-draw of the
            //     same mesh (vs_outline pushes vertices along their oct-decoded
            //     normal by u_outlineParams.x metres), gated by stencil_test =
            //     NOT_EQUAL 1 so only the silhouette band remains.  Depth test
            //     OFF — the outline pokes through any geometry that occludes
            //     the entity, which is the entire reason this feature exists
            //     ("where is my selection when the gizmo is hidden by a wall?").
            // Both views target the post-process sceneFb (set up below) so the
            // outline gets tonemapped along with the rest of the scene.  We use
            // an HDR-bright yellow so ACES tonemap output reads as a vivid,
            // saturated band.
            {
                EntityID selE = impl_->editorState.primarySelection();
                if (selE != INVALID_ENTITY && bgfx::isValid(impl_->outlineFillProgram) &&
                    bgfx::isValid(impl_->outlineProgram))
                {
                    auto* wt = impl_->registry.get<WorldTransformComponent>(selE);
                    auto* mc = impl_->registry.get<MeshComponent>(selE);
                    const Mesh* mesh = (wt && mc) ? impl_->resources.getMesh(mc->mesh) : nullptr;

                    if (mesh && mesh->isValid() && bgfx::isValid(mesh->surfaceVbh))
                    {
                        // Scale outline thickness by camera distance so the
                        // visible band stays roughly constant in screen space
                        // (a fixed object-space inflation looks huge up close
                        // and vanishes far away).  The 0.005 factor was tuned
                        // against the editor's default cube + camera distance.
                        const glm::vec3 entityPos = glm::vec3(wt->matrix[3]);
                        const float camDist = glm::length(impl_->camera.position() - entityPos);
                        const float outlineWidth = 0.005f * camDist + 0.01f;
                        const float outlineParams[4] = {outlineWidth, 0.0f, 0.0f, 0.0f};

                        // HDR-bright yellow — ACES tonemap output reads as a
                        // saturated yellow band against the scene image.
                        const float outlineColor[4] = {6.0f, 4.5f, 0.0f, 1.0f};

                        // Configure the two views.  Both target the same HDR
                        // scene FB as kViewOpaque so they share the D24S8
                        // attachment populated by the opaque pass.  Neither
                        // clears (BGFX_CLEAR_NONE preserves both color and
                        // stencil state from view 9 / view 11 respectively).
                        bgfx::setViewName(kViewEditorSelectionStencil, "SelectionStencil");
                        bgfx::setViewRect(kViewEditorSelectionStencil, 0, 0, W, H);
                        bgfx::setViewClear(kViewEditorSelectionStencil, BGFX_CLEAR_NONE);
                        bgfx::setViewFrameBuffer(kViewEditorSelectionStencil,
                                                 impl_->postProcess.resources().sceneFb());
                        bgfx::setViewTransform(kViewEditorSelectionStencil, glm::value_ptr(viewMtx),
                                               glm::value_ptr(projMtx));

                        bgfx::setViewName(kViewEditorSelectionOutline, "SelectionOutline");
                        bgfx::setViewRect(kViewEditorSelectionOutline, 0, 0, W, H);
                        bgfx::setViewClear(kViewEditorSelectionOutline, BGFX_CLEAR_NONE);
                        bgfx::setViewFrameBuffer(kViewEditorSelectionOutline,
                                                 impl_->postProcess.resources().sceneFb());
                        bgfx::setViewTransform(kViewEditorSelectionOutline, glm::value_ptr(viewMtx),
                                               glm::value_ptr(projMtx));

                        // Pass 1: stencil-fill.  Position stream only.
                        bgfx::setTransform(glm::value_ptr(wt->matrix));
                        bgfx::setVertexBuffer(0, mesh->positionVbh);
                        bgfx::setIndexBuffer(mesh->ibh);
                        bgfx::setState(outlineStencilFillState());
                        bgfx::setStencil(outlineStencilFillStencilFront(), BGFX_STENCIL_NONE);
                        bgfx::submit(kViewEditorSelectionStencil, impl_->outlineFillProgram);

                        // Pass 2: inflated outline draw.  Position + surface
                        // streams (vs_outline reads a_normal from stream 1).
                        bgfx::setTransform(glm::value_ptr(wt->matrix));
                        bgfx::setVertexBuffer(0, mesh->positionVbh);
                        bgfx::setVertexBuffer(1, mesh->surfaceVbh);
                        bgfx::setIndexBuffer(mesh->ibh);
                        bgfx::setUniform(impl_->outlineColorUniform, outlineColor);
                        bgfx::setUniform(impl_->outlineParamsUniform, outlineParams);
                        bgfx::setState(outlineDrawState());
                        bgfx::setStencil(outlineDrawStencilFront(), BGFX_STENCIL_NONE);
                        bgfx::submit(kViewEditorSelectionOutline, impl_->outlineProgram);
                    }
                }
            }

            // -- Skybox -----------------------------------------------------------
            // Submitted on the same view as the opaque pass AFTER all opaque
            // draws so it fills only those pixels where nothing else has been
            // drawn (depth test = LESS_EQUAL with the vertex shader forcing
            // depth = 1.0). The cubemap is mip 0 of the prefiltered IBL.
            // SkyboxRenderer::render takes the bgfx-free TextureHandle wrapper;
            // wrap the IBL prefiltered cubemap (still bgfx-typed) at the boundary.
            impl_->skybox.render(kViewOpaque, engine::rendering::TextureHandle{
                                                  impl_->iblResources.prefiltered().idx});

            ++impl_->viewportRedrawCount;
        }

        // -- Gizmo rendering --------------------------------------------------
        // Submitted EVERY frame (not gated on viewportDirty) because the
        // gizmo writes to its own view (kGizmoView=52) which targets the
        // backbuffer — and the post-process tonemap pass below overwrites
        // the backbuffer each frame.  If we only submitted on dirty frames,
        // the gizmo would vanish on the first idle frame after a redraw.
        // Hide all editor gizmos in Play/Paused mode so the viewport shows a
        // clean preview of the running game (Unity/Unreal/Godot convention).
        if (impl_->editorState.playState() == EditorPlayState::Editing)
        {
            EntityID selE = impl_->editorState.primarySelection();
            if (selE != INVALID_ENTITY)
            {
                impl_->gizmoRenderer.render(*impl_->gizmo, viewMtx, projMtx, W, H);
            }

            // Light gizmos (directional cylinder + arrows, point light icon).
            impl_->gizmoRenderer.renderLightGizmos(impl_->registry, viewMtx, projMtx, W, H);
        }

        // -- Post-process: tonemap HDR scene FB → backbuffer ------------------
        // Runs every frame (dirty or idle).  PBR (fs_pbr.sc) outputs linear
        // HDR; this pass applies ACES + sRGB gamma and writes the LDR result
        // to the backbuffer.  Bloom / SSAO / FXAA stay off — they would cost
        // extra view IDs and the editor doesn't need them yet.
        // On idle frames the kViewOpaque scene FB is unchanged, but the
        // tonemap pass still re-presents it, fixing the "viewport goes black
        // when idle" behaviour the dirty-flag path used to produce.
        {
            engine::rendering::PostProcessSettings ppSettings;
            ppSettings.bloom.enabled = false;
            ppSettings.ssao.enabled = false;
            ppSettings.fxaaEnabled = false;
            ppSettings.toneMapper = engine::rendering::ToneMapper::ACES;
            impl_->postProcess.submit(ppSettings, impl_->uniforms,
                                      engine::rendering::kViewPostProcessBase,
                                      bgfx::FrameBufferHandle{bgfx::kInvalidHandle});
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

        // -- HUD (viewport overlay: FPS, gizmo mode, shortcuts, status) -------
        // Built EVERY frame as a UiDrawList of text commands, submitted on
        // view kViewImGui through the engine's UiRenderer + JetBrains Mono
        // MSDF font (loaded once in init()).  Pre-Phase-7 the editor skipped
        // the rebuild on idle frames and just touched the view to keep it
        // alive (the swapchain re-presented the previous backbuffer).  That
        // optimisation no longer holds: the post-process tonemap pass at
        // view kViewPostProcessBase rewrites the entire backbuffer every
        // frame, so anything we don't re-submit on top — HUD, gizmo — is
        // wiped.  Falls back to bgfx debug text if the font failed to load.
        {
            bgfx::dbgTextClear();

            const char* modeStr = "Translate";
            if (impl_->gizmo->mode() == GizmoMode::Rotate)
                modeStr = "Rotate";
            else if (impl_->gizmo->mode() == GizmoMode::Scale)
                modeStr = "Scale";

            char fpsBuf[128];
            snprintf(fpsBuf, sizeof(fpsBuf),
                     "Sama Editor  |  %.1f fps  |  %.3f ms  |  redraws: %llu",
                     dt > 0.0f ? 1.0f / dt : 0.0f, dt * 1000.0f,
                     static_cast<unsigned long long>(impl_->viewportRedrawCount));
            char shortcutBuf[256];
            snprintf(shortcutBuf, sizeof(shortcutBuf),
                     "Right-drag=orbit  Scroll=zoom  W/E/R=gizmo [%s]  Cmd+Z=undo  "
                     "Cmd+Shift+Z=redo",
                     modeStr);

            const char* playStr = "Space=play";
            engine::math::Vec4 playColor{0.5f, 0.5f, 0.5f, 1.f};  // dark gray
            {
                auto ps = impl_->editorState.playState();
                if (ps == EditorPlayState::Playing)
                {
                    playStr = "> PLAYING  (Space=pause, Esc=stop)";
                    playColor = {0.4f, 1.0f, 0.4f, 1.f};  // green
                }
                else if (ps == EditorPlayState::Paused)
                {
                    playStr = "|| PAUSED  (Space=resume, Esc=stop)";
                    playColor = {1.0f, 1.0f, 0.4f, 1.f};  // yellow
                }
            }

            if (impl_->hudFontLoaded)
            {
                // MSDF path: clear list, push text commands, submit on view 15.
                impl_->hudDrawList.clear();
                const auto* font = static_cast<const engine::ui::IFont*>(&impl_->hudFont);

                const engine::math::Vec4 white{1.f, 1.f, 1.f, 1.f};
                const engine::math::Vec4 gray{0.75f, 0.75f, 0.75f, 1.f};
                const engine::math::Vec4 green{0.4f, 1.0f, 0.4f, 1.f};

                impl_->hudDrawList.drawText({12.f, 8.f}, fpsBuf, white, font, 16.f);
                impl_->hudDrawList.drawText({12.f, 28.f}, shortcutBuf, gray, font, 13.f);
                impl_->hudDrawList.drawText({12.f, 46.f}, playStr, playColor, font, 14.f);

                if (impl_->statusTimer > 0.0f)
                {
                    impl_->hudDrawList.drawText({400.f, 8.f}, impl_->statusMsg, green, font, 16.f);
                }

                if (impl_->addComponentMenuOpen)
                {
                    impl_->hudDrawList.drawText({24.f, 80.f}, "--- Add Component ---", white, font,
                                                16.f);
                    impl_->hudDrawList.drawText({24.f, 100.f}, "1) DirectionalLight", gray, font,
                                                14.f);
                    impl_->hudDrawList.drawText({24.f, 116.f}, "2) PointLight", gray, font, 14.f);
                    impl_->hudDrawList.drawText({24.f, 132.f}, "3) Mesh (cube)", gray, font, 14.f);
                    impl_->hudDrawList.drawText({24.f, 148.f}, "4) Rigid Body", gray, font, 14.f);
                    impl_->hudDrawList.drawText({24.f, 164.f}, "5) Box Collider", gray, font, 14.f);
                    impl_->hudDrawList.drawText({24.f, 184.f}, "Esc to cancel",
                                                engine::math::Vec4{0.5f, 0.5f, 0.5f, 1.f}, font,
                                                12.f);
                }

                // Set up view 15 to overlay text on top of the rendered scene
                // without clearing colour. The depth state of UiRenderer's text
                // pass already disables depth test/write, so the view doesn't
                // need a depth attach either.
                bgfx::setViewName(kViewImGui, "EditorHUD");
                bgfx::setViewRect(kViewImGui, 0, 0, impl_->fbW, impl_->fbH);
                bgfx::setViewClear(kViewImGui, BGFX_CLEAR_NONE);
                bgfx::touch(kViewImGui);
                impl_->uiRenderer.render(impl_->hudDrawList, kViewImGui, impl_->fbW, impl_->fbH);
            }
            else
            {
                // Fallback to bgfx debug text — same content, ugly font.
                bgfx::dbgTextPrintf(1, 1, 0x0f, "%s", fpsBuf);
                bgfx::dbgTextPrintf(1, 2, 0x07, "%s", shortcutBuf);
                bgfx::dbgTextPrintf(1, 3, playColor.y > 0.7f ? 0x0a : 0x08, "%s", playStr);
                if (impl_->statusTimer > 0.0f)
                {
                    bgfx::dbgTextPrintf(40, 1, 0x0a, "%s", impl_->statusMsg);
                }
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
            }
        }

        // -- Animation system + panel ----------------------------------------
        // The editor runs the animation system unconditionally (not gated on
        // play mode) so scrubbing / playing in edit mode still drives the
        // skinned mesh pose. Entities without kFlagPlaying simply don't
        // advance their time — the kFlagSampleOnce path lets the scrubber
        // force a fresh sample while paused.
        // State machine transitions (before animation so clip changes take effect).
        impl_->stateMachineSystem.update(impl_->registry, dt, impl_->animationResources);

        // Clear per-entity event queues from last frame so markers reset.
        impl_->registry.view<engine::animation::AnimationEventQueue>().each(
            [](ecs::EntityID, engine::animation::AnimationEventQueue& q) { q.clear(); });

        impl_->animationSystem.update(impl_->registry, dt, impl_->animationResources,
                                      impl_->frameArena->resource());
        if (impl_->animationPanel)
        {
            impl_->animationPanel->update(dt);
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

        // Clear the per-frame viewport dirty flag now that all rendering has
        // been submitted (or skipped). Anything that still needs a redraw on
        // the next frame must set the flag again from its event handler (or
        // the explicit per-frame checks below for animation playback).
        impl_->viewportDirty = false;

        // -- Animation playback dirties the viewport --------------------------
        // The animation system (run above) advances clip time and writes new
        // bone matrices when an animator has kFlagPlaying or kFlagSampleOnce.
        // Mark the viewport dirty so the *next* frame re-submits the skinned
        // pass with the freshly-sampled bones. (kFlagSampleOnce was just
        // consumed by AnimationSystem::update so this also covers panel
        // scrubbing while paused.)
        impl_->registry.view<engine::animation::AnimatorComponent>().each(
            [&](ecs::EntityID, const engine::animation::AnimatorComponent& a)
            {
                if (a.flags & engine::animation::AnimatorComponent::kFlagPlaying)
                {
                    impl_->viewportDirty = true;
                }
            });

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
    if (impl_->animationPanel)
    {
        impl_->animationPanel->shutdown();
    }

    impl_->gizmoRenderer.shutdown();
    impl_->skybox.shutdown();
    impl_->iblResources.shutdown();

    // Release editor-loaded textures (decrements asset ref counts and frees
    // RenderResources texture slots — destroyAll below also drops the
    // texture vector, but clearing here keeps the release path symmetric
    // with scene New/Open and avoids any double-release bugs).
    impl_->clearEditorTextures();

    // Tear down HUD font + UI renderer.
    impl_->hudFont.shutdown();
    impl_->uiRenderer.shutdown();

    // Tear down post-process (HDR scene FB + tonemap shader).
    if (impl_->postProcessInitialized)
    {
        impl_->postProcess.shutdown();
        impl_->postProcessInitialized = false;
    }

    // Shut down physics (destroys all remaining bodies internally).
    impl_->physics.shutdown();

    // Destroy shader programs.
    if (bgfx::isValid(impl_->pbrProgram))
        bgfx::destroy(impl_->pbrProgram);
    if (bgfx::isValid(impl_->skinnedPbrProgram))
        bgfx::destroy(impl_->skinnedPbrProgram);
    if (bgfx::isValid(impl_->shadowProgram))
        bgfx::destroy(impl_->shadowProgram);
    if (bgfx::isValid(impl_->outlineFillProgram))
        bgfx::destroy(impl_->outlineFillProgram);
    if (bgfx::isValid(impl_->outlineProgram))
        bgfx::destroy(impl_->outlineProgram);
    if (bgfx::isValid(impl_->outlineColorUniform))
        bgfx::destroy(impl_->outlineColorUniform);
    if (bgfx::isValid(impl_->outlineParamsUniform))
        bgfx::destroy(impl_->outlineParamsUniform);

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
