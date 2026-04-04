# Game Layer Architecture

## 1. Overview

The **game layer** is the bridge between the Sama engine (ECS, renderer, physics, audio) and game-specific logic. It defines where engine responsibility ends and where game code begins.

### Current Problem

Every demo currently hardcodes its entire lifecycle in `main()`:

- Engine and subsystem initialization (physics, IBL, asset loaders)
- ECS entity creation and component setup
- The frame loop (`while (eng.beginFrame(dt)) { ... eng.endFrame(); }`)
- Per-frame system dispatch ordering (physics, transform, draw calls)
- Input handling, camera control, ImGui panels
- Cleanup and shutdown

Compare `apps/helmet_demo/main.mm` and `apps/physics_demo/main.mm`: both repeat the same ~50 lines of boilerplate for engine init, beginFrame/endFrame, shadow pass setup, PBR frame params, and HUD rendering. The only differences are game-specific: which entities to spawn, what to do each frame, and what ImGui panels to show.

This means:

1. Adding a new demo requires copying 200+ lines of boilerplate.
2. System execution order is implicit and hand-rolled per demo.
3. There is no way to switch scenes at runtime.
4. Fixed-timestep physics is either missing or ad-hoc (`physicsSys.update(reg, physics, dt)` uses variable dt directly).

### Goal

A clean separation where the engine owns the frame loop and the game provides configuration and callbacks. A game is a small class that says "here are my entities, here is what I do each frame" without touching `beginFrame`/`endFrame`, shadow atlas setup, or ImGui frame management.

---

## 2. Game Loop Architecture

### 2.1 The `IGame` Interface

Game code implements a single interface. The engine calls its methods at well-defined points in the frame.

```cpp
// engine/game/IGame.h
#pragma once

namespace engine::core { class Engine; }
namespace engine::ecs  { class Registry; }

namespace engine::game
{

class IGame
{
public:
    virtual ~IGame() = default;

    // Called once after Engine::init() completes.
    // Create entities, load scenes, register custom systems, set up game state.
    virtual void onInit(core::Engine& engine, ecs::Registry& registry) = 0;

    // Called 0-N times per frame at a fixed timestep (default 1/60s).
    // Use for physics-rate gameplay: movement, AI ticks, physics forces.
    virtual void onFixedUpdate(core::Engine& engine, ecs::Registry& registry,
                               float fixedDt) {}

    // Called once per frame with the variable (rendering) delta time.
    // Use for input response, camera, animation blending, ImGui panels.
    virtual void onUpdate(core::Engine& engine, ecs::Registry& registry,
                          float dt) = 0;

    // Called after engine systems (transform, cull) but before endFrame.
    // Use for custom render passes, HUD overlays, debug visualization.
    virtual void onRender(core::Engine& engine) {}

    // Called once before Engine::shutdown().
    // Release assets, destroy physics bodies, clean up game state.
    virtual void onShutdown(core::Engine& engine, ecs::Registry& registry) {}
};

}  // namespace engine::game
```

Design choices:

- **`onFixedUpdate` and `onRender` are optional** (empty default implementations). Many games only need `onInit` + `onUpdate`. This avoids forcing trivial games to implement four empty methods.
- **`Registry&` is passed as a parameter**, not accessed through `Engine`. This makes the data flow explicit and keeps `Engine` from owning game-layer state. The `Engine` continues to own window, renderer, resources, and input -- exactly as it does today.
- **No `onEvent` callback.** Input is polled via `engine.inputState()` during `onUpdate`, consistent with the existing `InputState` model. An event queue can be added later without changing the interface.
- **Forward declarations only in the header** to minimize compile-time impact.

### 2.2 The `GameRunner`

`GameRunner` owns the frame loop and calls `IGame` at the right points. It replaces the hand-rolled `while (eng.beginFrame(dt))` pattern in each demo.

```cpp
// engine/game/GameRunner.h
#pragma once

#include <memory>

namespace engine::core  { class Engine; struct EngineDesc; }
namespace engine::ecs   { class Registry; }
namespace engine::game  { class IGame; }

namespace engine::game
{

class GameRunner
{
public:
    // Takes ownership of the game instance.
    explicit GameRunner(std::unique_ptr<IGame> game);
    ~GameRunner();

    // Run the full lifecycle: init -> loop -> shutdown.
    // Returns the process exit code (0 on clean exit).
    int run(const core::EngineDesc& desc);

private:
    std::unique_ptr<IGame> game_;
    float fixedTimestep_ = 1.0f / 60.0f;
    float maxAccumulator_ = 0.25f;  // cap spiral-of-death
};

}  // namespace engine::game
```

The implementation of `run()` contains the frame loop with fixed-timestep accumulator:

```cpp
int GameRunner::run(const EngineDesc& desc)
{
    Engine engine;
    if (!engine.init(desc))
        return 1;

    Registry registry;
    game_->onInit(engine, registry);

    float accumulator = 0.0f;
    float dt = 0.0f;

    while (engine.beginFrame(dt))
    {
        if (engine.fbWidth() == 0 || engine.fbHeight() == 0)
        {
            engine.endFrame();
            continue;
        }

        // --- Fixed update (physics rate) ---
        accumulator += dt;
        if (accumulator > maxAccumulator_)
            accumulator = maxAccumulator_;

        while (accumulator >= fixedTimestep_)
        {
            game_->onFixedUpdate(engine, registry, fixedTimestep_);
            accumulator -= fixedTimestep_;
        }

        // --- Variable update (render rate) ---
        game_->onUpdate(engine, registry, dt);

        // --- Engine systems ---
        // TransformSystem, cull, etc. run here between game update and render.
        // (See section 2.3 for system execution order.)

        // --- Render ---
        game_->onRender(engine);

        engine.endFrame();
    }

    game_->onShutdown(engine, registry);
    engine.shutdown();
    return 0;
}
```

### 2.3 System Execution Order

Each frame executes in this order:

```
1. Input             (engine.beginFrame -- polls GLFW, updates InputState)
2. FixedUpdate       (0-N times: game physics, AI ticks, gameplay at fixed rate)
3. Update            (1 time: game logic, camera, animation, ImGui at render rate)
4. AnimationSystem   (skeletal animation sampling)
5. IKSystem          (inverse kinematics corrections)
6. TransformSystem   (recompute world transforms from dirty locals)
7. FrustumCullSystem (tag visible entities)
8. Render            (shadow passes, opaque pass, post-process, game onRender)
9. EndFrame          (ImGui submit, frame arena reset, bgfx frame)
```

Steps 4-7 are engine systems dispatched by the `SystemExecutor` DAG scheduler. The game does not call these manually. Steps 2-3 are game callbacks. Step 8 is split between engine render orchestration and the game's optional `onRender` hook.

The existing `SystemExecutor<Systems...>` and compile-time `buildSchedule` remain unchanged. `GameRunner` instantiates a `SystemExecutor` with the engine's built-in systems and calls `runFrame` between `onUpdate` and `onRender`. Games that need custom ECS systems (e.g., a gameplay system with `Reads`/`Writes` declarations) can register them at init time and they will be included in the DAG.

### 2.4 Fixed Timestep Rationale

The current demos pass variable `dt` directly to the physics system (`physicsSys.update(reg, physics, dt)` in `physics_demo`). This causes non-deterministic physics behavior -- objects land in different positions depending on frame rate.

The accumulator pattern in `GameRunner::run()` fixes this:

- Physics always steps at `fixedTimestep_` (default 1/60s), regardless of render frame rate.
- Multiple physics steps per frame when rendering is slow; zero steps when rendering is fast.
- `maxAccumulator_` caps the spiral-of-death: if a frame takes 500ms, we do not run 30 physics steps to catch up. We cap at `0.25 / fixedTimestep_ = 15` steps and accept the simulation falling behind.
- The interpolation alpha (`accumulator / fixedTimestep_`) is available if visual interpolation between physics states is needed later, but is not required initially.

### 2.5 Extending Engine (Not Replacing It)

`Engine` is not modified. `GameRunner` composes `Engine` and `Registry` as local variables in `run()`. This means:

- Existing demos continue to work unchanged. Migration is opt-in.
- `Engine` remains a pure infrastructure layer (window, renderer, input, resources).
- `GameRunner` is the new default entry point for games, but advanced users can still write a raw `main()` with `beginFrame`/`endFrame` if they need full control.

A migrated demo looks like:

```cpp
// apps/physics_demo/PhysicsGame.h
class PhysicsGame : public engine::game::IGame
{
public:
    void onInit(Engine& engine, Registry& registry) override;
    void onFixedUpdate(Engine& engine, Registry& registry, float fixedDt) override;
    void onUpdate(Engine& engine, Registry& registry, float dt) override;
    void onShutdown(Engine& engine, Registry& registry) override;

private:
    JoltPhysicsEngine physics_;
    PhysicsSystem physicsSys_;
    OrbitCamera cam_;
    // ... game state
};

// apps/physics_demo/main.mm
int main()
{
    EngineDesc desc;
    desc.windowTitle = "Physics Demo";

    GameRunner runner(std::make_unique<PhysicsGame>());
    return runner.run(desc);
}
```

The 500-line `main.mm` becomes a ~5-line entry point plus a focused game class.

---

## 3. Scene Loading

### 3.1 SceneManager

The existing `SceneSerializer` handles JSON serialization of entities and components but has no concept of "the current scene" or scene transitions. `SceneManager` adds that layer.

```cpp
// engine/scene/SceneManager.h
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "engine/ecs/Entity.h"

namespace engine::core  { class Engine; }
namespace engine::ecs   { class Registry; }
namespace engine::scene { class SceneSerializer; }
namespace engine::assets { class AssetManager; }

namespace engine::scene
{

// Handle to a loaded scene. Opaque to game code.
struct SceneHandle
{
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

class SceneManager
{
public:
    SceneManager(ecs::Registry& registry, core::Engine& engine,
                 assets::AssetManager& assets);
    ~SceneManager();

    // Load a scene file. Entities are created in the registry.
    // Returns a handle to the loaded scene.
    SceneHandle loadScene(const char* filepath);

    // Unload the active scene: destroy all non-persistent entities,
    // release scene-owned assets.
    void unloadScene();

    // Convenience: unload current + load new. Useful for dev iteration.
    SceneHandle reloadScene();

    // The currently active scene.
    [[nodiscard]] SceneHandle activeScene() const { return activeScene_; }

    // Mark an entity as persistent -- it survives scene transitions.
    void markPersistent(ecs::EntityID entity);

    // Check if an entity is persistent.
    [[nodiscard]] bool isPersistent(ecs::EntityID entity) const;

    // Register a callback invoked after a scene finishes loading.
    using SceneLoadedCallback = std::function<void(SceneHandle)>;
    void setOnSceneLoaded(SceneLoadedCallback cb);

private:
    ecs::Registry& registry_;
    core::Engine& engine_;
    assets::AssetManager& assets_;
    SceneSerializer serializer_;

    SceneHandle activeScene_;
    uint32_t nextSceneId_ = 1;

    std::string activeScenePath_;
    std::vector<ecs::EntityID> sceneEntities_;  // entities owned by current scene
    std::vector<ecs::EntityID> persistentEntities_;

    SceneLoadedCallback onSceneLoaded_;
};

}  // namespace engine::scene
```

### 3.2 Scene Lifecycle

**Loading:**

```
loadScene("levels/beach.json")
  1. SceneSerializer::loadScene() creates entities in the registry
  2. SceneManager records all created entity IDs in sceneEntities_
  3. SceneHandle is assigned and stored as activeScene_
  4. onSceneLoaded_ callback fires (game can do post-load setup)
```

**Unloading:**

```
unloadScene()
  1. Iterate sceneEntities_, skip any in persistentEntities_
  2. Destroy each non-persistent entity via registry_.destroyEntity()
  3. Clear sceneEntities_, reset activeScene_
```

**Reloading (dev iteration):**

```
reloadScene()
  1. Store activeScenePath_
  2. unloadScene()
  3. loadScene(activeScenePath_)
```

This is the primary workflow during development -- edit a scene JSON, press a hotkey, see the result without restarting the application.

### 3.3 Persistent Entities

Some entities must survive scene transitions:

- **Player entity** (in games with a persistent player across levels)
- **Audio manager entity** (background music should not cut out during transitions)
- **Global game state entity** (score, inventory, settings)

Persistence is opt-in via `markPersistent()`. The `SceneManager` checks this list during `unloadScene()` and skips those entities. Persistent entities are the game's responsibility -- the engine does not assume anything about what should persist.

Implementation detail: persistent entity IDs are stored in a small `std::vector` (typically < 10 entries). A linear scan is fine; no hash map needed.

### 3.4 Scene File Format

**Development:** JSON, using the existing `SceneSerializer` and `io::JsonWriter`/`io::JsonDocument` infrastructure. Human-readable, diff-friendly, easy to hand-edit.

```json
{
    "version": 1,
    "entities": [
        {
            "id": 0,
            "name": "Sun",
            "components": {
                "Transform": {
                    "position": [0, 10, 0],
                    "rotation": [1, 0, 0, 0],
                    "scale": [1, 1, 1]
                },
                "DirectionalLight": {
                    "direction": [0.5, -1, 0.3],
                    "color": [1, 0.95, 0.85],
                    "intensity": 6.0,
                    "castShadows": true
                }
            }
        }
    ]
}
```

**Shipping:** Binary format (future). The `SceneSerializer` already abstracts read/write through `io::JsonWriter` and `io::JsonValue`. A binary serializer implementing the same component handler callbacks can be swapped in without changing any component registration code. The binary format would use a flat buffer layout: entity count, then per-entity component masks and packed component data. This is a Phase 2 optimization -- JSON is fine for now.

### 3.5 Startup Scene

The startup scene is configurable, resolved in this priority order:

1. **CLI argument:** `--scene levels/beach.json` (highest priority, for dev iteration)
2. **Project config:** `startupScene` field in `project.json` (see Section 4)
3. **None:** If no startup scene is specified, `onInit` creates entities programmatically (current demo behavior)

`GameRunner::run()` loads the startup scene after `onInit` returns, so the game can do pre-scene setup (register custom component handlers, create persistent entities) before the scene file is parsed.

### 3.6 Scene Transitions

Scene transitions are game-driven but engine-supported:

```cpp
// Simple (immediate) transition -- good for dev, loading screens
sceneManager.unloadScene();
sceneManager.loadScene("levels/forest.json");

// Transition with fade (game code orchestrates the timing)
void MyGame::transitionTo(const char* scenePath)
{
    fadeOverlay_.startFadeOut(0.3f);  // game-owned fade quad
    pendingScene_ = scenePath;
}

void MyGame::onUpdate(Engine& engine, Registry& registry, float dt)
{
    fadeOverlay_.update(dt);
    if (fadeOverlay_.isFadedOut() && !pendingScene_.empty())
    {
        sceneManager_.unloadScene();
        sceneManager_.loadScene(pendingScene_.c_str());
        pendingScene_.clear();
        fadeOverlay_.startFadeIn(0.3f);
    }
}
```

The engine provides the mechanism (load/unload). The game provides the policy (when to transition, what visual effect to use). This avoids baking a specific transition style into the engine.

### 3.7 Async Loading (Future)

For large scenes, loading on the main thread causes a visible hitch. The async path:

1. `loadSceneAsync("levels/city.json")` returns immediately with a `SceneHandle` in "loading" state.
2. A background thread parses the JSON and prepares component data in a staging buffer.
3. On the next frame, `SceneManager::processLoads()` (called by `GameRunner` before `onUpdate`) commits the staged entities to the registry on the main thread.
4. `onSceneLoaded_` fires once all entities are committed.

This mirrors the existing `AssetManager` pattern: `assets.load<GltfAsset>(path)` returns a handle immediately, and `assets.processUploads()` commits GPU resources on the main thread each frame. Scene loading would follow the same async-load, main-thread-commit model.

This is not needed for initial implementation. Scenes with < 10,000 entities load in < 5ms from JSON. Async loading becomes important when scene files reference large assets (meshes, textures) that need streaming.

---

## 4. Project Configuration

### 4.1 Format

A `project.json` file at the project root provides engine-wide defaults:

```json
{
    "name": "My Game",
    "startupScene": "scenes/main_menu.json",

    "window": {
        "title": "My Game",
        "width": 1920,
        "height": 1080,
        "fullscreen": false
    },

    "render": {
        "shadowResolution": 2048,
        "shadowCascades": 3
    },

    "physics": {
        "fixedTimestep": 0.01666,
        "gravity": [0, -9.81, 0],
        "maxSubSteps": 4
    },

    "audio": {
        "masterVolume": 1.0,
        "musicVolume": 0.7,
        "sfxVolume": 1.0
    },

    "frameArenaSize": 4194304
}
```

### 4.2 Loading

`ProjectConfig` is a plain struct parsed from JSON at startup, before `Engine::init()`:

```cpp
// engine/game/ProjectConfig.h
#pragma once

#include <string>

namespace engine::game
{

struct WindowConfig
{
    std::string title = "Sama";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool fullscreen = false;
};

struct RenderConfig
{
    uint32_t shadowResolution = 2048;
    uint32_t shadowCascades = 1;
};

struct PhysicsConfig
{
    float fixedTimestep = 1.0f / 60.0f;
    float gravity[3] = {0.0f, -9.81f, 0.0f};
    uint32_t maxSubSteps = 4;
};

struct AudioConfig
{
    float masterVolume = 1.0f;
    float musicVolume = 0.7f;
    float sfxVolume = 1.0f;
};

struct ProjectConfig
{
    std::string name = "Untitled";
    std::string startupScene;

    WindowConfig window;
    RenderConfig render;
    PhysicsConfig physics;
    AudioConfig audio;

    size_t frameArenaSize = 2 * 1024 * 1024;

    // Load from file. Returns false if file is missing or malformed.
    // Missing fields keep their defaults.
    bool loadFromFile(const char* filepath);
};

}  // namespace engine::game
```

### 4.3 Integration with GameRunner

`GameRunner::run()` uses `ProjectConfig` to populate `EngineDesc`:

```cpp
int GameRunner::run(const ProjectConfig& config)
{
    EngineDesc desc;
    desc.windowWidth = config.window.width;
    desc.windowHeight = config.window.height;
    desc.windowTitle = config.window.title.c_str();
    desc.shadowResolution = config.render.shadowResolution;
    desc.shadowCascades = config.render.shadowCascades;
    desc.frameArenaSize = config.frameArenaSize;

    fixedTimestep_ = config.physics.fixedTimestep;

    Engine engine;
    if (!engine.init(desc))
        return 1;

    // ... rest of lifecycle
}
```

This replaces the per-demo `EngineDesc` setup (currently 4-5 lines copy-pasted in every `main.mm`). CLI overrides (`--width 800 --scene foo.json`) take priority over `project.json` values, parsed before `loadFromFile` applies defaults.

### 4.4 Design Decisions

- **JSON, not TOML/YAML.** The engine already has `io::JsonDocument` and `io::JsonWriter`. Adding another format parser for a 20-line config file is not justified. JSON is verbose for config but the overhead is negligible here.
- **Plain struct, not a registry of key-value pairs.** Compile-time type safety. Typos in field names produce parse warnings, not silent runtime bugs.
- **No hot-reload of project config.** Project settings (window size, shadow resolution) require engine re-init to take effect. They are not frame-level tunables. ImGui panels handle runtime tweaking; project.json handles startup defaults.
- **`project.json` is optional.** If missing, all defaults apply. This keeps the "clone repo, build, run" workflow zero-config.

---

## 5. UI/Widget System

### Context

ImGui is used for the editor and debug overlays, but it is not suitable for shipped game UI. It lacks styling, has limited layout capabilities, renders with its own draw pipeline, and its immediate-mode model does not compose well with game state that persists across frames (inventory screens, dialog trees, animation-driven menus). The engine needs a retained-mode UI system that games can style, animate, and skin without touching C++ rendering code.

### Requirements

| Category | Examples |
|---|---|
| Menus | Main menu, pause menu, settings (keybinds, audio sliders, resolution picker) |
| HUD | Health bars, ammo counter, minimap, score display, objective markers |
| Panels | Inventory grid, dialog windows, quest log, crafting UI |
| Text | Localized strings, rich text (bold/color spans), chat, subtitles |

### Architecture: Retained-Mode UI Tree

The UI is a tree of `UiNode` objects that persists across frames. Each frame, the engine walks the tree to compute layout, generate render commands, and dispatch input events. This is **not** a scene graph -- UI nodes are plain C++ objects owned by the `UiCanvas`, not ECS entities. ECS is the wrong tool here: UI nodes form a deep, variable-depth tree with parent-relative coordinates, and the SparseSet iteration pattern (flat, unordered) does not map well to recursive tree traversal. A dedicated tree with pointer-based parent/child links is simpler and faster for layout.

#### Namespace

All UI types live in `engine::ui`.

#### UiNode Base

```cpp
namespace engine::ui
{

using UiCallback = std::function<void(UiNode& sender)>;

// Anchor point relative to parent's rect. (0,0) = top-left, (1,1) = bottom-right.
struct UiAnchor
{
    math::Vec2 min{0.f, 0.f};  // top-left anchor
    math::Vec2 max{0.f, 0.f};  // bottom-right anchor
};

class UiNode
{
public:
    virtual ~UiNode() = default;

    // Identity
    uint32_t id() const noexcept { return id_; }
    const char* name() const noexcept { return name_.c_str(); }

    // Tree structure
    UiNode* parent() const noexcept { return parent_; }
    std::span<std::unique_ptr<UiNode>> children() noexcept;

    UiNode& addChild(std::unique_ptr<UiNode> child);
    std::unique_ptr<UiNode> removeChild(uint32_t childId);

    // Layout -- anchor + offset model (similar to Unity RectTransform)
    UiAnchor anchor;
    math::Vec2 offsetMin{0.f, 0.f};  // pixel offset from anchored min corner
    math::Vec2 offsetMax{0.f, 0.f};  // pixel offset from anchored max corner
    math::Vec2 pivot{0.5f, 0.5f};    // rotation/scale pivot, normalized

    // Computed by layout pass -- read-only to game code
    struct ComputedRect
    {
        math::Vec2 position;  // top-left in screen pixels (logical)
        math::Vec2 size;      // width, height in logical pixels
    };
    const ComputedRect& rect() const noexcept { return computedRect_; }

    // Visibility and interaction
    bool visible = true;
    bool interactable = true;
    float opacity = 1.0f;

    // Style class name -- resolved against UiStyleSheet
    const char* styleClass = nullptr;

protected:
    virtual void onDraw(class UiDrawList& drawList) const = 0;
    virtual bool onEvent(const class UiEvent& event) { return false; }
    virtual void onLayoutComputed() {}  // hook after layout pass

private:
    friend class UiCanvas;

    uint32_t id_ = 0;
    std::string name_;
    UiNode* parent_ = nullptr;
    std::vector<std::unique_ptr<UiNode>> children_;
    ComputedRect computedRect_{};
};

}  // namespace engine::ui
```

**Design rationale -- `std::unique_ptr` children:** UI trees are modified infrequently (menu open/close, dialog advance) and traversed every frame. Owning pointers in a vector give cache-friendly iteration of children while still allowing O(1) reparenting via move. An intrusive linked list was considered but rejected: it saves the vector allocation but makes "iterate children in order" slower due to pointer chasing, and UI trees are rarely deeper than 10-15 levels.

**Why not ECS for UI:** SparseSet iteration is unordered and flat. UI layout requires parent-before-child ordering (top-down for anchor resolution) and child-before-parent ordering (bottom-up for size-to-content). A pointer-based tree handles both naturally. Putting UI nodes in the ECS would also pollute the entity ID space -- a typical settings menu has 50+ nodes (labels, sliders, checkboxes), none of which need frustum culling, physics, or any other engine system. Keeping UI nodes out of the registry avoids wasting SparseSet capacity on entities that only the UI system touches.

#### Concrete Widget Types

| Type | Purpose | Key properties |
|---|---|---|
| `UiPanel` | Background rect, container | `color`, `borderColor`, `borderWidth`, `cornerRadius` |
| `UiImage` | Textured quad | `textureId`, `uvRect`, `tint`, `preserveAspect` |
| `UiText` | Text rendering | `text`, `fontId`, `fontSize`, `color`, `align` |
| `UiButton` | Clickable panel with label | `onClick` callback, `normalStyle`, `hoverStyle`, `pressedStyle` |
| `UiSlider` | Horizontal/vertical slider | `value` (float 0-1), `onValueChanged` callback |
| `UiList` | Scrollable list of items | `itemHeight`, `dataSource` callback, virtualized rendering |
| `UiProgressBar` | Filled bar | `value` (float 0-1), `fillColor`, `bgColor` |

Each widget type inherits `UiNode` and overrides `onDraw()` and `onEvent()`.

```cpp
class UiButton : public UiNode
{
public:
    UiCallback onClick;
    UiCallback onHover;

    std::string label;
    uint32_t fontId = 0;
    float fontSize = 16.f;

protected:
    void onDraw(UiDrawList& drawList) const override;
    bool onEvent(const UiEvent& event) override;

private:
    enum class State : uint8_t { Normal, Hovered, Pressed };
    State state_ = State::Normal;
};
```

```cpp
class UiSlider : public UiNode
{
public:
    using ValueChangedCallback = std::function<void(UiSlider& sender, float newValue)>;
    ValueChangedCallback onValueChanged;

    float value = 0.f;       // clamped to [0, 1]
    float trackHeight = 4.f; // logical pixels
    float thumbSize = 16.f;  // logical pixels

protected:
    void onDraw(UiDrawList& drawList) const override;
    bool onEvent(const UiEvent& event) override;

private:
    bool dragging_ = false;
};
```

#### UiCanvas

`UiCanvas` is the root container and frame-level driver. There is one per game state (the main menu has its own canvas, gameplay HUD has another). The canvas owns the root `UiNode`, runs layout, dispatches events, and produces the draw list.

```cpp
class UiCanvas
{
public:
    explicit UiCanvas(float logicalWidth, float logicalHeight);

    // Root of the UI tree. All nodes are descendants of this.
    UiNode& root() noexcept { return root_; }

    // Resize (e.g. window resize). Marks layout dirty.
    void setSize(float logicalWidth, float logicalHeight);

    // Run layout if dirty. Call once per frame before drawing.
    void computeLayout();

    // Dispatch an input event. Returns true if consumed.
    bool dispatchEvent(const UiEvent& event);

    // Walk the tree and build draw commands. Does not submit to bgfx.
    void draw(UiDrawList& drawList);

    // True if the last dispatchEvent consumed input (game should skip it).
    [[nodiscard]] bool uiWantsInput() const noexcept { return uiWantsInput_; }

private:
    UiPanel root_;                // root is always a panel (full-screen container)
    bool dirtyLayout_ = true;
    bool uiWantsInput_ = false;
};
```

#### Layout System

Layout runs top-down in a single pass after the tree is modified:

1. **Anchor resolve** -- each node's rect is computed from its parent's rect using anchors + offsets. This is the same model as Unity's RectTransform: `anchorMin`/`anchorMax` define a relative rect within the parent, and `offsetMin`/`offsetMax` add pixel adjustments.

   Example -- center a 200x50 button:
   ```cpp
   button->anchor.min = {0.5f, 0.5f};
   button->anchor.max = {0.5f, 0.5f};
   button->offsetMin = {-100.f, -25.f};
   button->offsetMax = {100.f, 25.f};
   // Result: 200x50 rect centered in parent
   ```

   Example -- stretch a panel to fill parent width with 10px margin:
   ```cpp
   panel->anchor.min = {0.f, 0.f};
   panel->anchor.max = {1.f, 0.f};
   panel->offsetMin = {10.f, 10.f};
   panel->offsetMax = {-10.f, 60.f};
   // Result: full-width panel, 50px tall, with 10px margins
   ```

2. **Stack/flex containers** -- `UiPanel` can optionally act as a stack layout (`direction = Horizontal | Vertical`). Children are sized according to their `flex` weight (0 = fixed size, >0 = proportional). This handles common patterns like toolbar button rows and vertical settings lists without manual positioning.

   ```cpp
   struct StackLayout
   {
       enum class Direction : uint8_t { Horizontal, Vertical };
       Direction direction = Direction::Vertical;
       float spacing = 0.f;    // gap between children (logical pixels)
       float padding = 0.f;    // inset from container edges
   };
   ```

   When a `UiPanel` has a `StackLayout`, the layout pass overrides children's anchors along the stack axis. Children with `flex = 0` use their explicit size; children with `flex > 0` divide the remaining space proportionally. This is a simplified flexbox model -- enough for game UI without the full CSS spec.

3. **Post-layout hook** -- `onLayoutComputed()` fires after a node's rect is finalized, allowing widgets like `UiList` to compute which items are visible for virtualized rendering.

Layout only reruns when the tree is marked dirty (child added/removed, anchor changed, window resized). The `dirtyLayout_` flag on `UiCanvas` avoids per-frame recomputation when nothing changed.

#### Event System

Input flows through the UI before reaching game systems, using the same pattern as the existing `imguiWantsMouse()` guard in `Engine`.

```cpp
struct UiEvent
{
    enum class Type : uint8_t
    {
        MouseDown, MouseUp, MouseMove,
        KeyDown, KeyUp, TextInput,
        Scroll
    };

    Type type;
    math::Vec2 position;       // logical pixels
    int32_t button = 0;        // mouse button index
    int32_t keyCode = 0;       // platform key code
    float scrollDelta = 0.f;
    char textUtf8[8] = {};     // UTF-8 character for TextInput events
};
```

Event dispatch is a reverse-order hit test: `UiCanvas::dispatchEvent()` walks the tree in reverse child order (front-to-back, since later children render on top) and calls `onEvent()` on the first node whose `computedRect_` contains the cursor position. If a node returns `true`, the event is consumed and `uiWantsInput_` is set for the game loop to check.

```cpp
// In the game loop (inside a GameState):
UiEvent event = buildEventFromInput(engine.inputState());
bool consumed = canvas_.dispatchEvent(event);
if (!consumed)
{
    // Route to game input: camera, player movement, etc.
    handleGameInput(engine.inputState(), dt);
}
```

This mirrors the existing `engine.imguiWantsMouse()` check in the demos but applies to the game UI system instead of ImGui. Both guards can coexist -- during development, ImGui debug panels and game UI are both active, and input is routed to whichever system claims it first (ImGui checked first for debug priority, then game UI, then game input).

#### Styling: JSON Style Sheets

Styles are defined in JSON and loaded at startup via `io::JsonDocument`. Each widget type has a default style; widgets can override via `styleClass`.

```json
{
    "Button": {
        "normal": { "bgColor": [0.2, 0.2, 0.2, 1.0], "textColor": [1, 1, 1, 1] },
        "hovered": { "bgColor": [0.3, 0.3, 0.4, 1.0] },
        "pressed": { "bgColor": [0.1, 0.1, 0.15, 1.0] },
        "cornerRadius": 4.0,
        "padding": 8.0
    },
    "Button.primary": {
        "normal": { "bgColor": [0.1, 0.4, 0.8, 1.0] }
    },
    "Panel": {
        "bgColor": [0.05, 0.05, 0.05, 0.9],
        "cornerRadius": 4.0,
        "padding": 8.0
    },
    "Text": {
        "fontId": "default",
        "fontSize": 14.0,
        "color": [0.9, 0.9, 0.9, 1.0]
    },
    "Slider": {
        "trackColor": [0.3, 0.3, 0.3, 1.0],
        "fillColor": [0.2, 0.5, 0.9, 1.0],
        "thumbColor": [1, 1, 1, 1]
    }
}
```

```cpp
class UiStyleSheet
{
public:
    // Load styles from a JSON file. Merges with existing styles (later loads win).
    bool loadFromFile(const char* filepath);

    // Resolve style for a widget type + optional class (e.g., "Button.primary").
    // Falls back to base type style if class not found.
    struct ResolvedStyle
    {
        math::Vec4 bgColor{0.f, 0.f, 0.f, 0.f};
        math::Vec4 textColor{1.f, 1.f, 1.f, 1.f};
        float cornerRadius = 0.f;
        float padding = 0.f;
        float fontSize = 14.f;
        uint32_t fontId = 0;
    };

    ResolvedStyle resolve(const char* typeName, const char* styleClass = nullptr) const;

private:
    // Parsed and cached at load time -- no per-frame JSON access.
    ankerl::unordered_dense::map<std::string, ResolvedStyle> cache_;
};
```

Styles are resolved once when a node is added to the tree (or when `styleClass` changes) and cached on the node. There are no per-frame JSON lookups or string comparisons during rendering.

#### Rendering

UI rendering uses a dedicated `UiDrawList` that collects draw commands during the tree walk, then submits them as batched sprite geometry reusing the existing `SpriteBatcher` vertex format (`{float2 pos, float2 uv, uint8x4 color}` = 20 bytes/vertex, as defined in `SpriteBatcher.h`).

**View allocation:** UI renders after post-processing to avoid bloom/FXAA/tonemapping affecting text and icons. The current `ViewIds.h` reserves views 14-15 for UI, but these are numerically before the post-process range (16-47), and bgfx processes views in ID order. Two options:

1. **Move game UI to view 48** (after all post-process views). This is the cleanest solution. `kViewImGui` moves to 49.
2. **Render UI to a separate framebuffer** and composite it after post-process. More complex, but allows per-UI-element post-effects (blur behind panels).

Option 1 is the initial approach. The view ID constants would be:

```cpp
// Proposed addition to ViewIds.h:
inline constexpr bgfx::ViewId kViewGameUi = 48;  // retained-mode game UI
inline constexpr bgfx::ViewId kViewImGui  = 49;  // editor/debug overlay (moved from 15)
// kViewUi (14) remains available for world-space 3D sprites
```

**Draw list structure:**

```cpp
class UiDrawList
{
public:
    void addRect(math::Vec2 pos, math::Vec2 size, math::Vec4 color,
                 float cornerRadius = 0.f);
    void addImage(math::Vec2 pos, math::Vec2 size, uint32_t textureId,
                  math::Vec4 uvRect, math::Vec4 tint);
    void addText(math::Vec2 pos, const char* text, uint32_t fontId,
                 float fontSize, math::Vec4 color);

    // Sort by texture, batch consecutive same-texture commands, submit to bgfx.
    // Vertex/index data allocated from FrameArena (reset each frame by Engine).
    void flush(bgfx::Encoder* enc, bgfx::ProgramHandle program,
               bgfx::UniformHandle s_texture, const rendering::RenderResources& res,
               memory::FrameArena& arena);

    void clear();

private:
    struct DrawCmd
    {
        enum class Type : uint8_t { Rect, Image, Text };
        Type type;
        math::Vec2 pos;
        math::Vec2 size;
        math::Vec4 colorOrTint;
        math::Vec4 uvRect;       // for Image; {0,0,1,1} for Rect
        uint32_t textureId;      // 0 = white texture (solid color rect)
        float cornerRadius;
        const char* text;        // for Text; nullptr otherwise
        uint32_t fontId;
        float fontSize;
    };

    memory::InlinedVector<DrawCmd, 256> commands_;
};
```

**Batching strategy:** Commands are sorted by texture ID, matching `SpriteBatcher`'s existing approach. Consecutive commands with the same texture are merged into a single transient vertex/index buffer and submitted as one draw call. Solid-color rectangles use `textureId = 0` (the engine's white 1x1 texture from `RenderResources::whiteTexture()`), so all solid UI elements batch together regardless of color. Rounded rectangles use a small SDF circle texture for corner rendering -- they batch with each other since they share the same texture.

A typical HUD (health bar, ammo count, minimap frame, 4 status icons) produces ~15 draw commands that batch down to 3-4 bgfx submits (one per unique texture).

**Memory model:** The `UiNode` tree is persistent heap memory -- it lives across frames and is only rebuilt when menus open/close. The `UiDrawList` commands and all transient vertex/index buffers are allocated from the `FrameArena` via `arena.resource()` and reset automatically by `Engine::endFrame()`. This means the UI rendering path makes zero heap allocations per frame.

#### Text Rendering

Text uses pre-generated bitmap font atlases. Two phases:

**Phase 1 -- Bitmap fonts (initial implementation):**

Each font is a single texture atlas + a JSON metrics file describing glyph rects, horizontal advances, and kerning pairs (BMFont format, generated by tools like Hiero or bmfont). `UiText::onDraw()` emits one quad per visible glyph into the `UiDrawList`, all referencing the same atlas texture, so an entire text string batches into a single draw call.

```cpp
struct GlyphMetrics
{
    math::Vec4 uvRect;   // position in atlas {u0, v0, u1, v1}
    math::Vec2 size;     // glyph size in pixels
    math::Vec2 offset;   // bearing offset from cursor
    float advance;       // horizontal advance to next glyph
};

class BitmapFont
{
public:
    bool loadFromFile(const char* metricsPath, rendering::RenderResources& res);

    const GlyphMetrics* getGlyph(uint32_t codepoint) const;
    float getKerning(uint32_t left, uint32_t right) const;
    float lineHeight() const noexcept { return lineHeight_; }
    uint32_t atlasTextureId() const noexcept { return atlasTextureId_; }

private:
    ankerl::unordered_dense::map<uint32_t, GlyphMetrics> glyphs_;
    ankerl::unordered_dense::map<uint64_t, float> kerning_;  // key = (left << 32) | right
    float lineHeight_ = 0.f;
    uint32_t atlasTextureId_ = 0;
};
```

**Phase 2 -- MSDF fonts (deferred):**

For resolution-independent text (critical for HiDPI and VR), switch to multi-channel signed distance field fonts generated by `msdf-atlas-gen`. The vertex pipeline is identical to Phase 1 -- one quad per glyph, same atlas texture. Only the fragment shader changes: it samples the MSDF atlas and reconstructs sharp edges at any scale using `fwidth()` for anti-aliasing. This is a shader swap, not an architecture change.

MSDF is deferred because bitmap fonts are simpler to implement and sufficient for fixed-resolution desktop games. The trigger to implement MSDF is the first game targeting HiDPI displays (Retina, 4K) or VR headsets where text is viewed at varying distances.

#### HiDPI

All UI coordinates are in **logical pixels**. The content scale factors from `Engine::contentScaleX()` / `contentScaleY()` are applied once when building the orthographic projection matrix for the UI view:

```cpp
float logicalW = static_cast<float>(engine.fbWidth()) / engine.contentScaleX();
float logicalH = static_cast<float>(engine.fbHeight()) / engine.contentScaleY();
auto uiProj = glm::ortho(0.f, logicalW, logicalH, 0.f, -1.f, 1.f);
```

Game code and the layout system work exclusively in logical pixels. The projection maps logical coordinates to physical framebuffer pixels. Font atlases should be generated at 2x for Retina displays (the atlas texture is sampled at physical resolution, so glyphs stay sharp). The MSDF path eliminates this concern entirely since distance fields scale to any resolution.

Mouse input coordinates from `InputState` are already in logical pixels (GLFW provides content-scaled cursor positions on macOS), so hit testing works without any conversion.

---

## 6. Game State Management

### Context

Games are not a single loop -- they are a sequence of distinct states (loading, menu, gameplay, pause, cutscene) with different update logic, input handling, and rendering. The `IGame` interface from Section 2 handles the simple case (one game class, one loop), but real games need to switch between fundamentally different modes. A pause menu should freeze physics but still render the 3D scene underneath. A loading screen should show a progress bar while assets stream in on a background thread. These patterns require a state machine.

The state machine builds on top of `IGame` and `GameRunner` -- it is not a replacement. Simple demos continue to use `IGame` directly. Games that need state management create a `GameStateManager` inside their `IGame::onUpdate()` and delegate to it.

### State Machine

#### IGameState Interface

```cpp
// engine/game/IGameState.h
#pragma once

namespace engine::core { class Engine; }
namespace engine::ecs  { class Registry; }

namespace engine::game
{

class GameStateManager;

class IGameState
{
public:
    virtual ~IGameState() = default;

    // Called once when the state becomes active (pushed onto stack).
    virtual void onEnter(GameStateManager& mgr, core::Engine& engine,
                         ecs::Registry& registry) = 0;

    // Called once when the state is removed (popped from stack).
    virtual void onExit(GameStateManager& mgr, core::Engine& engine,
                        ecs::Registry& registry) = 0;

    // Called when another state is pushed on top of this one.
    // The state remains on the stack but is no longer the top.
    // Use to pause physics, mute audio, disable input.
    virtual void onSuspend(GameStateManager& mgr, core::Engine& engine) {}

    // Called when the state above is popped and this becomes active again.
    // Use to resume physics, unpause audio.
    virtual void onResume(GameStateManager& mgr, core::Engine& engine) {}

    // Per-frame update. Called ONLY on the top-of-stack state.
    virtual void onUpdate(GameStateManager& mgr, core::Engine& engine,
                          ecs::Registry& registry, float dt) = 0;

    // Per-frame render. Called on ALL visible states, bottom to top.
    // This allows gameplay to render the 3D scene while the pause menu
    // renders its overlay on top -- both in the same frame.
    virtual void onRender(GameStateManager& mgr, core::Engine& engine,
                          ecs::Registry& registry) = 0;

    // Whether states below should also render.
    // true  = overlay (pause menu, dialog box -- scene visible behind it)
    // false = opaque  (main menu, loading screen -- nothing behind it)
    virtual bool isTransparent() const { return false; }
};

}  // namespace engine::game
```

**Design rationale -- `onSuspend`/`onResume`:** When the pause menu is popped, the gameplay state needs to know it is active again (to unpause physics, restart audio). A simple `onEnter`/`onExit` pair does not distinguish "first entry" from "returned from overlay." The suspend/resume pair solves this cleanly without requiring the gameplay state to check stack position.

**Design rationale -- `onRender` called on all visible states:** The alternative is having only the top state render, which forces the pause state to re-render the 3D scene (duplicating gameplay rendering code) or copy the previous frame to a texture (extra framebuffer copy). The multi-render approach is simpler: gameplay state renders the 3D scene as usual, pause state renders a semi-transparent overlay + menu on top. The existing bgfx view system handles this naturally since each state submits to different views.

#### GameStateManager

```cpp
// engine/game/GameStateManager.h
#pragma once

#include <memory>
#include <vector>

#include "engine/memory/InlinedVector.h"

namespace engine::core { class Engine; }
namespace engine::ecs  { class Registry; }

namespace engine::game
{

class IGameState;

class GameStateManager
{
public:
    // Push a new state. Current top receives onSuspend(), new state
    // receives onEnter().
    void push(std::unique_ptr<IGameState> state);

    // Pop the top state. It receives onExit(), new top receives onResume().
    // Asserts if the stack is empty.
    void pop();

    // Replace the top state. Equivalent to pop + push but avoids the
    // intermediate onResume/onSuspend on the state below.
    void replace(std::unique_ptr<IGameState> state);

    // Clear the entire stack. All states receive onExit() from top to bottom.
    void clearAll();

    // Called by the game each frame. Dispatches onUpdate to the top state,
    // then applies any queued transitions.
    void update(core::Engine& engine, ecs::Registry& registry, float dt);

    // Called by the game each frame. Renders visible states bottom-to-top.
    void render(core::Engine& engine, ecs::Registry& registry);

    // True when the stack is empty (game should exit).
    [[nodiscard]] bool empty() const noexcept { return stack_.empty(); }

    // Access the top state (for type checks, debugging).
    [[nodiscard]] IGameState* top() const noexcept;

private:
    // State transitions are deferred to avoid mutating the stack while
    // iterating. push/pop/replace/clearAll queue an operation; applyPendingOps
    // executes them after update() returns.
    enum class Op : uint8_t { Push, Pop, Replace, ClearAll };
    struct PendingOp
    {
        Op type;
        std::unique_ptr<IGameState> state;  // null for Pop/ClearAll
    };

    std::vector<std::unique_ptr<IGameState>> stack_;
    memory::InlinedVector<PendingOp, 4> pendingOps_;

    void applyPendingOps(core::Engine& engine, ecs::Registry& registry);
};
```

**Deferred transitions:** State changes are not applied immediately. If `GameplayState::onUpdate()` calls `mgr.push(pauseState)`, the push is queued and applied after `update()` returns. This prevents iterator invalidation and ensures `onRender()` sees a consistent stack. The `InlinedVector<PendingOp, 4>` avoids heap allocation for the common case -- more than 4 state transitions per frame is pathological.

#### Render Order

`GameStateManager::render()` finds the lowest visible state by walking down from the top until it hits a non-transparent state, then renders upward:

```cpp
void GameStateManager::render(core::Engine& engine, ecs::Registry& registry)
{
    if (stack_.empty())
        return;

    // Find the lowest visible state.
    int32_t bottom = static_cast<int32_t>(stack_.size()) - 1;
    while (bottom > 0 && stack_[static_cast<size_t>(bottom)]->isTransparent())
    {
        --bottom;
    }

    // Render from bottom-visible to top.
    for (int32_t i = bottom; i < static_cast<int32_t>(stack_.size()); ++i)
    {
        stack_[static_cast<size_t>(i)]->onRender(*this, engine, registry);
    }
}
```

This means the gameplay state renders the 3D scene (shadow pass, opaque pass, post-process), then the pause state renders a darkened full-screen quad + pause menu UI on top -- all in a single frame with no extra framebuffer copies.

### Common States

```
                    ┌───────────┐
                    │  Loading  │
                    └─────┬─────┘
                          │
                    ┌─────▼─────┐     push         ┌──────────┐
                    │ MainMenu  │──────────────────>│ Gameplay │
                    └───────────┘                   └────┬─────┘
                          ▲                              │
                          │ clearAll + push          push │ pop
                          │                              ▼
                    ┌─────┴─────┐                   ┌──────────┐
                    │ GameOver  │                   │  Paused  │
                    └───────────┘                   └──────────┘
```

| State | `onUpdate` behavior | `onRender` behavior | `isTransparent` |
|---|---|---|---|
| `LoadingState` | Polls async scene load, updates progress | Loading screen UI (progress bar) | `false` |
| `MainMenuState` | Menu input, settings UI | Menu background + UI canvas | `false` |
| `GameplayState` | Physics, AI, animation, player input | Full 3D scene + HUD overlay | `false` |
| `PausedState` | UI-only input (menu navigation) | Dim overlay + pause menu UI | `true` |
| `GameOverState` | Score display, retry/quit input | Results screen UI | `false` |
| `CutsceneState` | Camera path playback, dialog sequence | 3D scene + letterbox + subtitles | `false` |

#### Integration with IGame / GameRunner

The state machine is owned by the game, not the engine. This keeps `Engine` and `GameRunner` unchanged:

```cpp
class MyGame : public engine::game::IGame
{
public:
    void onInit(core::Engine& engine, ecs::Registry& registry) override
    {
        states_.push(std::make_unique<LoadingState>("scenes/main_menu.json"));
    }

    void onUpdate(core::Engine& engine, ecs::Registry& registry, float dt) override
    {
        if (states_.empty())
        {
            // All states popped -- signal exit (e.g., set a flag checked
            // by the window close condition)
            return;
        }
        states_.update(engine, registry, dt);
    }

    void onRender(core::Engine& engine) override
    {
        // Registry is not passed to onRender in IGame, so either:
        // (a) Store a Registry* in MyGame from onInit, or
        // (b) Have states that need the registry capture it.
        // Option (a) is simpler:
        states_.render(engine, *registry_);
    }

private:
    game::GameStateManager states_;
    ecs::Registry* registry_ = nullptr;  // set in onInit
};
```

### Scene Transitions

Loading a new scene is integrated with the state machine via `LoadingState`:

```cpp
class LoadingState : public IGameState
{
public:
    // nextStateFactory creates the state to transition to after loading.
    LoadingState(std::string scenePath,
                 std::function<std::unique_ptr<IGameState>()> nextStateFactory)
        : scenePath_(std::move(scenePath))
        , nextStateFactory_(std::move(nextStateFactory))
    {}

    void onEnter(GameStateManager& mgr, core::Engine& engine,
                 ecs::Registry& registry) override
    {
        // Kick off async scene load on a background thread.
        // Uses std::async for simplicity; production code would use the
        // engine's thread pool.
        loadFuture_ = std::async(std::launch::async, [this, &engine]()
        {
            return scene::SceneLoader::loadFromFile(scenePath_.c_str(),
                                                     engine.resources());
        });
    }

    void onUpdate(GameStateManager& mgr, core::Engine& engine,
                  ecs::Registry& registry, float dt) override
    {
        if (loadFuture_.wait_for(std::chrono::seconds(0))
            == std::future_status::ready)
        {
            auto sceneData = loadFuture_.get();
            sceneData.populateRegistry(registry, engine.resources());
            mgr.replace(nextStateFactory_());
        }
    }

    void onRender(GameStateManager& mgr, core::Engine& engine,
                  ecs::Registry& registry) override
    {
        // Render loading screen: background + progress bar via UiCanvas.
        loadingCanvas_.computeLayout();
        UiDrawList drawList;
        loadingCanvas_.draw(drawList);
        drawList.flush(/*...*/);
    }

private:
    std::string scenePath_;
    std::function<std::unique_ptr<IGameState>()> nextStateFactory_;
    std::future<scene::SceneData> loadFuture_;
    ui::UiCanvas loadingCanvas_{1280.f, 720.f};
};
```

**Pause state integration:**

```cpp
class PausedState : public IGameState
{
public:
    void onEnter(GameStateManager& mgr, core::Engine& engine,
                 ecs::Registry& registry) override
    {
        // Build pause menu UI tree
        auto& root = pauseCanvas_.root();
        auto resumeBtn = std::make_unique<ui::UiButton>();
        resumeBtn->label = "Resume";
        resumeBtn->onClick = [&mgr](ui::UiNode&) { mgr.pop(); };
        root.addChild(std::move(resumeBtn));
    }

    void onUpdate(GameStateManager& mgr, core::Engine& engine,
                  ecs::Registry& registry, float dt) override
    {
        ui::UiEvent event = buildUiEvent(engine.inputState());
        pauseCanvas_.dispatchEvent(event);

        // ESC also unpauses
        if (engine.inputState().keyPressed(input::Key::Escape))
            mgr.pop();
    }

    void onRender(GameStateManager& mgr, core::Engine& engine,
                  ecs::Registry& registry) override
    {
        // Semi-transparent dark overlay
        // Then pause menu UI
        pauseCanvas_.computeLayout();
        ui::UiDrawList drawList;
        pauseCanvas_.draw(drawList);
        drawList.flush(/*...*/);
    }

    bool isTransparent() const override { return true; }

private:
    ui::UiCanvas pauseCanvas_{1280.f, 720.f};
};
```

### Save/Load System

#### ISaveable Interface

Game components that need persistence implement `ISaveable`. This is a mixin applied to game-specific ECS components -- not all components are saveable (render state, physics cache, audio handles are transient and reconstructed on load).

```cpp
// engine/game/ISaveable.h
#pragma once

namespace engine::io { class JsonWriter; class JsonValue; }

namespace engine::game
{

class ISaveable
{
public:
    virtual ~ISaveable() = default;

    // Write persistent state to the save file.
    virtual void serialize(io::JsonWriter& writer) const = 0;

    // Restore state from a save file. Called after entity and
    // component are created.
    virtual void deserialize(const io::JsonValue& value) = 0;
};

}  // namespace engine::game
```

`ISaveable` uses `io::JsonWriter` and `io::JsonValue` directly -- the same serialization infrastructure used by `SceneSerializer`. This is intentional: game state and scene state share the same format, so tools and debugging workflows apply to both.

#### What to Save vs. Not Save

| Save | Do NOT save |
|---|---|
| Player position, rotation, velocity | `WorldTransformComponent` (recomputed from scene graph) |
| Inventory contents and quantities | `VisibleTag`, `ShadowVisibleTag` (recomputed by cull systems) |
| Quest/mission progress flags | Transient particle system state |
| World state: opened doors, defeated enemies, collected items | Audio handles, channel state (SoLoud) |
| Camera mode and settings | bgfx resource handles (meshes, textures) |
| Game clock / accumulated play time | `FrameArena` contents |
| Skill trees, player stats, XP | ImGui internal state |
| NPC relationship/dialog state | Cached light cluster data |

The guiding principle: save **game-meaningful state** that the player would expect to persist. Everything else is engine-internal state that is reconstructed from the save data during load.

#### Save File Format

```cpp
struct SaveSlotMetadata
{
    uint32_t slotIndex;
    uint64_t timestampUtc;       // seconds since epoch
    float playTimeSeconds;       // total accumulated play time
    uint32_t thumbnailTextureId; // screenshot taken at save time (stored as PNG)
    char sceneName[64];          // human-readable scene/level name
};
```

**Dev builds:** JSON via `io::JsonWriter` for human readability, diffability, and easy debugging. A save file is just a JSON document containing entity IDs, component type names, and serialized component data.

**Shipping builds:** Binary format for size and load speed. The save system abstracts the format behind `SaveWriter` / `SaveReader` interfaces so game component code does not branch on format:

```cpp
class SaveWriter
{
public:
    virtual ~SaveWriter() = default;
    virtual void writeFloat(const char* key, float value) = 0;
    virtual void writeInt(const char* key, int32_t value) = 0;
    virtual void writeString(const char* key, const char* value) = 0;
    virtual void writeVec3(const char* key, const math::Vec3& value) = 0;
    virtual void beginObject(const char* key) = 0;
    virtual void endObject() = 0;
    virtual void beginArray(const char* key) = 0;
    virtual void endArray() = 0;
};

class SaveReader
{
public:
    virtual ~SaveReader() = default;
    virtual float readFloat(const char* key, float defaultVal) const = 0;
    virtual int32_t readInt(const char* key, int32_t defaultVal) const = 0;
    virtual const char* readString(const char* key,
                                    const char* defaultVal) const = 0;
    virtual math::Vec3 readVec3(const char* key) const = 0;
    virtual bool beginObject(const char* key) = 0;
    virtual void endObject() = 0;
    virtual size_t beginArray(const char* key) = 0;
    virtual void endArray() = 0;
};

// Concrete implementations:
class JsonSaveWriter final : public SaveWriter { /* wraps io::JsonWriter */ };
class JsonSaveReader final : public SaveReader { /* wraps io::JsonValue */ };
// Future:
class BinarySaveWriter final : public SaveWriter { /* packed binary */ };
class BinarySaveReader final : public SaveReader { /* packed binary */ };
```

The `SaveWriter`/`SaveReader` interface mirrors `io::JsonWriter`/`io::JsonValue` intentionally. The JSON implementation is a thin wrapper. The binary implementation writes the same logical data in a compact format (4-byte type tags + packed values, no key strings in binary mode -- keys are hashed to uint32 for lookup).

#### Save Slot Manager

```cpp
class SaveSlotManager
{
public:
    static constexpr uint32_t MAX_SLOTS = 8;

    // Scan save directory and populate slot metadata.
    void scanSlots(const char* saveDirectory);

    // Save the current game state to a slot.
    // Takes a screenshot thumbnail, iterates saveable components,
    // writes to disk.
    bool save(uint32_t slotIndex, ecs::Registry& reg, core::Engine& engine);

    // Load a save slot into the registry.
    // Clears non-persistent entities, recreates saved entities,
    // calls deserialize() on each component.
    bool load(uint32_t slotIndex, ecs::Registry& reg, core::Engine& engine);

    // Delete a save slot from disk.
    bool deleteSlot(uint32_t slotIndex);

    // Access metadata for save/load screen UI.
    const SaveSlotMetadata* getSlotMetadata(uint32_t slotIndex) const;
    bool isSlotOccupied(uint32_t slotIndex) const;

private:
    SaveSlotMetadata slots_[MAX_SLOTS]{};
    bool occupied_[MAX_SLOTS]{};
    std::string saveDirectory_;
};
```

**Save process:**
1. Capture a screenshot thumbnail (downsample the current framebuffer to 256x144 via a blit pass).
2. Write metadata (timestamp, play time, scene name, thumbnail path) to `saves/slot_N.meta`.
3. Iterate all entities in the registry. For each entity with at least one `ISaveable` component, write the entity ID and all saveable component data.
4. Write to `saves/slot_N.json` (dev) or `saves/slot_N.sav` (shipping).

**Load process:**
1. Read metadata to populate the save/load screen UI (showing timestamp, play time, thumbnail).
2. On user confirmation: clear all non-persistent entities from the registry.
3. Parse the save file, recreate entities, call `deserialize()` on each saveable component.
4. Reconstruct transient state: recompute world transforms (run `TransformSystem`), rebuild the physics world from loaded positions/shapes, reload audio sources.

### Entity Persistence Across Scenes

When the player moves between scenes (entering a building, changing levels), most entities are destroyed and replaced. Some entities must survive. This uses the `PersistentTag` approach from Section 3.3, integrated with the state machine:

```cpp
// Tag component -- zero-size, presence in SparseSet is the signal.
// Entities with this tag are not destroyed during scene transitions.
struct PersistentTag {};
static_assert(sizeof(PersistentTag) == 1);  // empty struct = 1 byte in C++
```

The scene transition flow inside a game state:

```cpp
void GameplayState::transitionToScene(ecs::Registry& reg, core::Engine& engine,
                                       const char* newScene)
{
    // 1. Destroy all non-persistent entities.
    std::vector<ecs::EntityID> toDestroy;
    reg.forEachEntity([&](ecs::EntityID entity)
    {
        if (!reg.has<PersistentTag>(entity))
            toDestroy.push_back(entity);
    });
    for (auto id : toDestroy)
        reg.destroyEntity(id);

    // 2. Load new scene entities into the same registry.
    sceneManager_.loadScene(newScene);

    // 3. Persistent entities (player, audio manager, game state singleton)
    //    retain all their components untouched.
}
```

**Entities that should be persistent:**
- Player entity (position, inventory, stats, active quests)
- Global audio manager entity (music crossfade state, ambient loop)
- Game state singleton entity (quest flags, world state map, play timer)
- Camera rig entity (avoids jarring camera reset on scene load)

**Entities that should NOT be persistent:** Scene-specific NPCs, props, lights, terrain, triggers. If the player returns to a previously visited scene, world state changes (opened doors, defeated enemies) are stored in the save system's world state map as flags, not by keeping old entities alive. This keeps the entity count bounded and avoids stale physics/render state from old scenes.

### Design Decisions

- **State machine is game-owned, not engine-owned.** The engine provides `IGameState` and `GameStateManager` as library code, but does not force their use. Simple demos use `IGame` directly. This avoids over-engineering the simple case.
- **`std::unique_ptr` ownership for states.** States are created on the heap because they carry variable amounts of data (UI canvases, physics worlds, loaded scene references). Stack allocation is not feasible. The `unique_ptr` makes ownership explicit and prevents leaks.
- **No global state singleton.** Game state (quest progress, world flags) is stored as components on a persistent entity, not in a global variable. This keeps it queryable through the same ECS patterns as everything else and makes it serializable through `ISaveable`.
- **JSON for dev saves, binary for shipping.** JSON is debuggable -- when a save file is corrupt, you can open it in a text editor. Binary is ~10x smaller and ~5x faster to parse, but not worth the debugging cost during development. The `SaveWriter`/`SaveReader` abstraction means game code does not change when switching formats.

---

## Open Questions

| Question | When to resolve |
|---|---|
| UI animation (tweens, easing on node properties)? | When the first game needs animated menus. Start with a `UiAnimator` that interpolates position/opacity/color over time with configurable easing curves. |
| Gamepad navigation for UI (focus system, D-pad traversal)? | Before the first shipped title on console. Requires a focus graph overlay on the UI tree -- non-trivial with arbitrary layouts. |
| UI data binding (reactive updates from game state)? | Evaluate when inventory/crafting UI is built. A lightweight observable property system may be simpler than manual `setText()` calls each frame. |
| Binary save format specification? | When the first game ships. JSON is sufficient during development. |
| Cloud save integration? | After the networking layer supports authenticated REST calls. The `SaveSlotManager` interface would gain `uploadSlot()` / `downloadSlot()` methods. |
| UI localization pipeline (string tables, RTL support)? | When the first game targets non-English audiences. String table lookups would replace hardcoded text in `UiText` nodes. |
