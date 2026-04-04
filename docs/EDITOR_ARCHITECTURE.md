# Sama Editor Architecture -- Native Platform GUI

## Document Purpose

This document specifies the architecture for a native-GUI editor for the Sama game engine. The editor replaces the existing ImGui-based plan described in NOTES.md with platform-native controls (AppKit on macOS, Win32/WinUI on Windows) while still rendering the 3D viewport through bgfx. The rationale: native UI provides superior text rendering, accessibility, system integration (drag-and-drop, file dialogs, dark mode), and "feels right" on each platform without the visual compromises of an immediate-mode overlay.

---

## 0. Existing Engine Surface Area

Key types the editor must interact with:

- `engine::ecs::Registry` -- entity lifecycle, component CRUD, `forEachEntity`, `view<Components...>()`
- `engine::ecs::EntityID` (uint64_t, generation|index packed)
- `engine::scene::setParent`, `detach`, `destroyHierarchy`, `getParent`, `getChildren`, `isAncestor`
- `engine::scene::NameComponent` (std::string name)
- `engine::scene::HierarchyComponent` (parent EntityID), `ChildrenComponent` (InlinedVector children)
- `engine::rendering::TransformComponent` (position Vec3, rotation Quat, scale Vec3, flags)
- `engine::rendering::CameraComponent`, `MeshComponent`, `MaterialComponent`
- `engine::rendering::DirectionalLightComponent`, `PointLightComponent`, `SpotLightComponent`
- `engine::rendering::Material` (albedo Vec4, roughness, metallic, emissiveScale, texture IDs)
- `engine::rendering::RenderResources` -- mesh/material/texture registries
- `engine::rendering::Renderer` -- `init(RendererDesc)`, `beginFrame()`, `endFrame()`, `resize()`
- `engine::rendering::RenderPass` -- fluent view configuration, can bind framebuffers
- `engine::scene::SceneSerializer` -- extensible save/load with `registerComponent` callbacks
- `engine::physics::RigidBodyComponent`, `ColliderComponent`
- `engine::animation::AnimatorComponent`, `SkeletonComponent`, `SkinComponent`
- `engine::audio::AudioSourceComponent`, `AudioListenerComponent`
- `engine::platform::IWindow` -- `nativeWindowHandle()` returns CAMetalLayer* (Mac) / HWND (Windows)
- `engine::core::Engine` -- owns window, renderer, resources, input, shaders, frame arena
- `engine::core::OrbitCamera` -- orbit/zoom/WASD camera
- `engine::input::InputState` -- key/mouse/touch queries

bgfx view ID layout (from `ViewIds.h`): shadow 0-7, depth 8, opaque 9, transparent 10, UI 14-15, post-process 16-47. Views 48+ are available for editor use.

---

## 1. Platform Abstraction Layer

### 1.1 Design Decision: Per-Platform Native (Not Qt/wxWidgets)

The engine already abstracts windowing through `engine::platform::IWindow`. The editor extends this pattern: thin C++ interfaces in headers, with per-platform implementations in `.mm` (macOS) and `.cpp` (Windows). This avoids pulling in Qt (200+ MB dependency, LGPL/commercial licensing) or wxWidgets (inconsistent native fidelity). The cost is two implementations per panel; the benefit is pixel-perfect native behavior and zero additional dependencies.

Linux support is deferred. When needed, GTK4 is the recommended choice (C API, integrates well with bgfx via GtkGLArea's native window handle).

### 1.2 Core Interfaces

All interfaces live in `engine/editor/platform/`:

```cpp
// engine/editor/platform/IEditorWindow.h
namespace engine::editor
{

struct EditorWindowDesc
{
    uint32_t width = 1600;
    uint32_t height = 1000;
    const char* title = "Sama Editor";
};

class IEditorWindow
{
public:
    virtual ~IEditorWindow() = default;

    virtual bool init(const EditorWindowDesc& desc) = 0;
    virtual void shutdown() = 0;

    // Returns false when the window should close.
    virtual bool pollEvents() = 0;

    // Native handle for the main window (NSWindow* on Mac, HWND on Windows).
    virtual void* nativeWindowHandle() const = 0;

    // Split layout management.
    virtual void addPanel(class IEditorPanel* panel, PanelPosition pos) = 0;
    virtual void removePanel(class IEditorPanel* panel) = 0;

    // Menu bar.
    virtual void setMenuBar(class IEditorMenuBar* menu) = 0;

    // Toolbar.
    virtual void setToolbar(class IEditorToolbar* toolbar) = 0;

    // Window dimensions in logical points.
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;

    // Content scale for DPI.
    virtual float contentScale() const = 0;
};

enum class PanelPosition : uint8_t
{
    Left,
    Right,
    Center,
    Bottom
};

} // namespace engine::editor
```

```cpp
// engine/editor/platform/IEditorPanel.h
namespace engine::editor
{

class IEditorPanel
{
public:
    virtual ~IEditorPanel() = default;

    virtual const char* panelName() const = 0;

    // Called once when the panel is added to the window.
    // The implementation creates native views here.
    // parentView: NSView* on Mac, HWND on Windows.
    virtual void createNativeView(void* parentView) = 0;

    // Called each frame to refresh the panel contents.
    virtual void update() = 0;

    // Called when the panel is removed from the window.
    virtual void destroyNativeView() = 0;

    // Returns the native view handle (NSView* or HWND).
    virtual void* nativeViewHandle() const = 0;

    // Minimum size in logical points.
    virtual uint32_t minWidth() const { return 200; }
    virtual uint32_t minHeight() const { return 100; }
};

} // namespace engine::editor
```

```cpp
// engine/editor/platform/IEditorMenuBar.h
namespace engine::editor
{

using MenuCallback = std::function<void()>;

struct MenuItem
{
    const char* label;
    const char* shortcut;     // e.g. "Cmd+S" or "Ctrl+S"
    MenuCallback callback;
    bool enabled = true;
    bool separator = false;   // if true, renders as a separator line
};

struct Menu
{
    const char* title;
    std::vector<MenuItem> items;
    std::vector<Menu> submenus;
};

class IEditorMenuBar
{
public:
    virtual ~IEditorMenuBar() = default;

    virtual void build(const std::vector<Menu>& menus) = 0;
    virtual void setItemEnabled(const char* menuTitle, const char* itemLabel, bool enabled) = 0;
};

} // namespace engine::editor
```

### 1.3 bgfx Rendering into a Native View

bgfx supports creating a framebuffer from a native window handle via `bgfx::createFrameBuffer(nwh, width, height)`. This is the mechanism for rendering into a panel that is not the main backbuffer.

For the editor's 3D viewport:

1. The `IEditorWindow` implementation creates the main OS window. bgfx is initialized with the main window's native handle (as it is today via `RendererDesc::nativeWindowHandle`).

2. The 3D viewport panel (`ViewportPanel`) creates a child native view (NSView subclass on Mac, child HWND on Windows).

3. On macOS, the NSView subclass is backed by a `CAMetalLayer` (same pattern as `GlfwWindow::nativeWindowHandle()`). The layer is passed to `bgfx::createFrameBuffer(metalLayer, width, height)` to get a `bgfx::FrameBufferHandle`.

4. On Windows, the child HWND is passed directly to `bgfx::createFrameBuffer(hwnd, width, height)`.

5. Each frame, the renderer binds this framebuffer to the scene views (opaque, transparent, shadow, post-process) instead of the default backbuffer.

6. When the viewport resizes, the framebuffer is destroyed and recreated at the new size. bgfx handles the GPU resource lifecycle.

For multiple viewports (scene view vs. game view): each viewport panel creates its own native view and framebuffer. The scene is rendered once per visible viewport with different camera parameters. bgfx views 48-63 are reserved for the second viewport, 64-79 for a third, etc.

### 1.4 macOS Implementation

```
engine/editor/platform/cocoa/
    CocoaEditorWindow.h / .mm     -- NSWindow + NSSplitView layout
    CocoaEditorPanel.h / .mm      -- Base NSView wrapper
    CocoaMenuBar.h / .mm          -- NSMenu / NSMenuItem
    CocoaToolbar.h / .mm          -- NSToolbar
    MetalViewportView.h / .mm     -- NSView subclass with CAMetalLayer
```

The main window uses `NSSplitView` (nested) for the three-column layout:
- Outer NSSplitView: horizontal split into [left sidebar | center+bottom | right sidebar]
- Inner NSSplitView (center+bottom): vertical split into [3D viewport | bottom tab area]

`NSOutlineView` is used for the hierarchy tree. `NSTableView` with custom cells for the properties inspector. `NSCollectionView` for the asset browser grid.

### 1.5 Windows Implementation

```
engine/editor/platform/win32/
    Win32EditorWindow.h / .cpp    -- HWND + splitter layout
    Win32EditorPanel.h / .cpp     -- Child HWND wrapper
    Win32MenuBar.h / .cpp         -- HMENU
    Win32Toolbar.h / .cpp         -- Toolbar control
    D3DViewportView.h / .cpp      -- Child HWND for bgfx D3D rendering
```

Uses standard Win32 controls: `TreeView` (hierarchy), `ListView` (asset browser), custom child HWNDs with `WM_PAINT` for the inspector. Splitter bars are implemented as thin HWNDs that handle `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` for resize.

---

## 2. Editor Layout

```
+-----------------------------------------------------------------------------------+
|  File   Edit   View   Entity   Component   Window                       [Sama]  |
+-----------------------------------------------------------------------------------+
| [Move] [Rotate] [Scale] [Local|World] [Snap: 0.5] | [Play] [Pause] [Stop]        |
+-----------------------------------------------------------------------------------+
|              |                                          |                         |
|   SCENE      |           3D VIEWPORT                    |    PROPERTIES           |
|  HIERARCHY   |                                          |    INSPECTOR            |
|              |    +--+                                  |                         |
|  v Scene     |    |  | <-- gizmo                        |  Transform              |
|    v Player  |    +--+                                  |   Pos  [0] [0] [0]      |
|      Mesh    |         ___                              |   Rot  [0] [0] [0]      |
|      Camera  |        /   \  <-- selected entity        |   Scl  [1] [1] [1]      |
|    v Enemies |       /     \                            |                         |
|      Enemy1  |       \_____/                            |  Mesh                   |
|      Enemy2  |                                          |   Model: cube.glb       |
|    Light     |              .  .  .  .  .  .            |                         |
|    Ground    |    . . . . grid . . . . . . .            |  Material               |
|              |              .  .  .  .  .  .            |   Albedo [====] #fff    |
|  [Search...] |                                          |   Rough  [==--] 0.5     |
|              |                                          |   Metal  [----] 0.0     |
|              |                                          |                         |
|              |                                          |  [+ Add Component]      |
+--------------+------------------------------------------+-------------------------+
|  Scene View  |  Game View  |  Console  |  Assets  |  Animation                    |
+--------------+-------------+-----------+----------+-------------------------------+
|                                                                                   |
|  ASSET BROWSER                                                                    |
|                                                                                   |
|  assets/                                                                          |
|    meshes/         textures/         audio/          scenes/                       |
|    [cube.glb]      [brick.png]       [bgm.ogg]       [level1.json]                |
|    [sphere.glb]    [grass.png]       [hit.wav]        [level2.json]                |
|                                                                                   |
+-----------------------------------------------------------------------------------+
|  [Console] Entity "Player" created  |  FPS: 60  |  Entities: 24  |  Draw: 156    |
+-----------------------------------------------------------------------------------+
```

### Layout Structure (Detailed)

```
+-----------------------------------------------------------------------------------+
|                              MENU BAR (native)                                    |
+-----------------------------------------------------------------------------------+
|                              TOOLBAR (native)                                     |
+--------+-------------------------------------------------+------------------------+
|        |                                                 |                        |
| LEFT   |              CENTER                             |   RIGHT                |
| PANEL  |              (NSSplitView / child HWND)         |   PANEL                |
| 250px  |                                                 |   300px                |
|        |  +-------------------------------------------+  |                        |
|        |  |                                           |  |                        |
|        |  |         3D VIEWPORT                       |  |                        |
|        |  |         (bgfx framebuffer)                |  |                        |
|        |  |                                           |  |                        |
|        |  |                                           |  |                        |
|        |  +-------------------------------------------+  |                        |
|        |  |  Tab Bar: [Scene|Game|Console|Assets|Anim]|  |                        |
|        |  +-------------------------------------------+  |                        |
|        |  |                                           |  |                        |
|        |  |         BOTTOM TAB PANEL (200px)          |  |                        |
|        |  |                                           |  |                        |
|        |  +-------------------------------------------+  |                        |
+--------+-------------------------------------------------+------------------------+
|                              STATUS BAR                                           |
+-----------------------------------------------------------------------------------+
```

---

## 3. Scene Hierarchy Panel

### 3.1 Data Model

The panel reads from the ECS `Registry` every frame (or on change notification). It walks root entities (those without a `HierarchyComponent` or whose `HierarchyComponent::parent == INVALID_ENTITY`) and recursively enumerates children via `scene::getChildren()`.

Each row displays:
- Disclosure triangle (expand/collapse) if the entity has `ChildrenComponent`
- Icon based on component presence: mesh icon if `MeshComponent`, light icon if any light component, camera icon if `CameraComponent`, speaker icon if `AudioSourceComponent`, default cube icon otherwise
- Entity name from `NameComponent::name` (falls back to "Entity #<index>" if no NameComponent)

### 3.2 Platform Implementation

**macOS:** `NSOutlineView` with a custom `NSOutlineViewDataSource` and `NSOutlineViewDelegate`. The data source wraps the ECS hierarchy. `outlineView:child:ofItem:` walks `getChildren()`. `outlineView:isItemExpandable:` checks for `ChildrenComponent` presence.

**Windows:** `TreeView` control (`WC_TREEVIEWW`). Items are `HTREEITEM` handles. On refresh, the tree is incrementally updated (insert/remove changed items only, not rebuilt from scratch).

### 3.3 Drag-to-Reparent

On macOS: implement `outlineView:validateDrop:proposedItem:proposedChildIndex:` and `outlineView:acceptDrop:item:childIndex:`. On drop, call `engine::scene::setParent(reg, draggedEntity, newParentEntity)`. The `setParent` function already handles cycle detection (returns false if the new parent is a descendant of the child).

The reparent operation is wrapped in a command object for undo/redo (see Section 7).

### 3.4 Context Menu

Right-click on an entity shows:
- Create Empty Child
- Duplicate (deep clone with all components and children)
- Delete (calls `scene::destroyHierarchy`)
- Rename (inline edit in the tree view)
- Copy / Paste

Right-click on empty space shows:
- Create Empty Entity
- Create Cube / Sphere / Plane (entity with TransformComponent + MeshComponent + MaterialComponent)
- Create Point Light / Spot Light / Directional Light
- Create Camera

### 3.5 Search/Filter

A text field above the tree view. Typing filters the tree to show only entities whose `NameComponent::name` contains the search string (case-insensitive). Parent entities of matching entities are shown (collapsed) to preserve hierarchy context. Clearing the search restores the full tree.

### 3.6 Multi-Select

Shift-click for range select, Cmd/Ctrl-click for toggle. The editor maintains a `std::vector<EntityID> selection_` in the `EditorState` (see Section 7). The properties inspector shows shared components when multiple entities are selected, with "mixed" indicators for differing values.

---

## 4. Properties/Inspector Panel

### 4.1 Architecture

The inspector uses a **component registry** pattern. Each component type registers an `IComponentInspector` that knows how to draw its editing UI and apply changes:

```cpp
// engine/editor/inspector/IComponentInspector.h
namespace engine::editor
{

class IComponentInspector
{
public:
    virtual ~IComponentInspector() = default;

    // Human-readable component name (e.g. "Transform", "Material").
    virtual const char* componentName() const = 0;

    // Returns true if the entity has this component.
    virtual bool hasComponent(ecs::EntityID entity, const ecs::Registry& reg) const = 0;

    // Create native UI for editing this component.
    // parentView: NSView* or HWND where the inspector should add its controls.
    virtual void createUI(void* parentView) = 0;

    // Refresh UI from the current component values.
    virtual void refresh(ecs::EntityID entity, const ecs::Registry& reg,
                         const rendering::RenderResources& res) = 0;

    // Add this component to an entity (for "Add Component" menu).
    virtual void addToEntity(ecs::EntityID entity, ecs::Registry& reg) = 0;

    // Remove this component from an entity.
    virtual void removeFromEntity(ecs::EntityID entity, ecs::Registry& reg) = 0;
};

} // namespace engine::editor
```

### 4.2 Built-in Component Inspectors

**TransformComponent Inspector:**
- Three rows: Position, Rotation (displayed as Euler angles), Scale
- Each row: label + three text fields (X, Y, Z) with drag-to-adjust
- On macOS: `NSTextField` with a custom `NSFormatter` for numeric input, `NSSlider` for drag behavior
- On Windows: `EDIT` controls with `ES_NUMBER` style, custom `WM_MOUSEMOVE` handling for drag
- Changes are batched: dragging creates a single undo command on mouse-up (not one per pixel)

**MaterialComponent Inspector (via RenderResources lookup):**
- Albedo: color well (`NSColorWell` / Win32 `ChooseColor`) + opacity slider
- Roughness: horizontal slider [0..1] with text field
- Metallic: horizontal slider [0..1] with text field
- Emissive Scale: slider [0..10]
- Texture slots (albedo map, normal map, ORM map, emissive map): thumbnail preview + "Browse" button that opens the asset browser with a filter
- Drag-drop from asset browser to texture slot

**LightComponent Inspectors (Directional/Point/Spot):**
- Color: color well
- Intensity: slider [0..100] with text field
- Direction (directional/spot): normalized Vec3 editor or two-angle (yaw/pitch) control
- Radius (point/spot): slider
- Inner/Outer angle (spot): dual slider with visual cone preview
- Cast Shadows toggle (directional): checkbox

**RigidBodyComponent Inspector:**
- Body Type: dropdown (Static / Dynamic / Kinematic)
- Mass: text field (hidden when Static)
- Linear/Angular Damping: sliders [0..1]
- Friction: slider [0..1]
- Restitution: slider [0..1]

**ColliderComponent Inspector:**
- Shape: dropdown (Box / Sphere / Capsule / Mesh)
- Offset: Vec3 editor
- Half Extents: Vec3 editor (shown for Box)
- Radius: slider (shown for Sphere/Capsule)

**AnimatorComponent Inspector:**
- Clip selector: dropdown listing available animation clips
- Speed: slider [0..4] with text field
- Looping: checkbox
- Play / Pause buttons
- Playback time: scrubber bar

**AudioSourceComponent Inspector:**
- Clip selector: dropdown listing loaded audio clips
- Volume: slider [0..1]
- Pitch: slider [0.1..3]
- Category: dropdown (SFX / Music / UI / Ambient)
- Spatial: checkbox
- Loop: checkbox
- Auto Play: checkbox
- Min/Max Distance: sliders (shown when Spatial is checked)

**CameraComponent Inspector:**
- Projection Type: dropdown (Perspective / Orthographic)
- FOV Y: slider [10..120] degrees (Perspective only)
- Near/Far Plane: text fields
- Aspect Ratio: text field or "Auto" checkbox

### 4.3 "Add Component" Button

At the bottom of the inspector, a button opens a searchable dropdown listing all registered component types that the selected entity does not already have. Selecting one calls `addToEntity` on the corresponding `IComponentInspector`, wrapped in an undo command.

### 4.4 Custom Component Support

Third-party or game-specific components register their inspectors via:

```cpp
// Called at editor startup or from a plugin.
editorState.inspectorRegistry().registerInspector(
    std::make_unique<MyCustomComponentInspector>());
```

This mirrors the `SceneSerializer::registerComponent` pattern already established in the engine.

---

## 5. Transform Gizmos

### 5.1 Rendering

Gizmos are rendered in the 3D viewport as an overlay pass after the main scene. They use a dedicated bgfx view ID (e.g., view 50 for the first viewport) with depth testing disabled against the scene (so gizmos are always visible) but with their own depth buffer (so gizmo parts occlude each other correctly).

Gizmo geometry is procedural:
- **Translate:** Three arrows (cones + cylinders) along X (red), Y (green), Z (blue) axes, plus three small squares for XY/XZ/YZ plane movement
- **Rotate:** Three circles (torus) around each axis, plus a white screen-space circle for free rotation
- **Scale:** Three lines with cubes at the ends, plus a center cube for uniform scale

Gizmos are rendered at a constant screen size regardless of distance (the gizmo model matrix is scaled by distance-to-camera).

### 5.2 Interaction

1. On mouse hover over the viewport, raycast against gizmo geometry (analytical ray-cylinder/ray-cone/ray-torus intersection, not GPU picking).
2. Highlight the hovered axis (brighten color).
3. On mouse down, begin drag. Project mouse movement onto the gizmo's constraint line/plane:
   - Translate: project onto the axis line (1D) or the plane (2D for XY/XZ/YZ handles)
   - Rotate: compute angle delta from the rotation circle's center
   - Scale: project onto the axis line, compute scale factor from distance ratio
4. Apply delta to `TransformComponent` each frame during drag. Mark the component dirty (`flags |= 0x01`).
5. On mouse up, create a single undo command capturing the before/after transform.

### 5.3 Controls

- **W** key: switch to Translate mode
- **E** key: switch to Rotate mode
- **R** key: switch to Scale mode
- **X** key: toggle Local / World space
- Hold **Ctrl/Cmd** during drag: snap to grid (configurable snap increment, default 0.5 units for translate, 15 degrees for rotate, 0.1 for scale)

### 5.4 Implementation Location

```
engine/editor/gizmo/
    TransformGizmo.h / .cpp      -- gizmo state machine, raycast, delta computation
    GizmoRenderer.h / .cpp       -- procedural mesh generation, bgfx draw calls
    GizmoShader.h                -- embedded unlit shader for gizmo rendering
```

The gizmo uses the existing unlit shader (`vs_unlit` / `fs_unlit`) with per-draw color uniforms. No new shaders are required.

---

## 6. 3D Viewport

### 6.1 Rendering Pipeline

The viewport panel owns:
- A native view (NSView subclass with CAMetalLayer on Mac, child HWND on Windows)
- A `bgfx::FrameBufferHandle` created from the native view's handle
- An editor camera (`OrbitCamera` or a new `FlyCamera`)

Each frame:
1. Bind the viewport's framebuffer to views kViewOpaque, kViewTransparent, kViewShadowBase..N, and the post-process chain
2. Set the editor camera's view/projection matrices on those views
3. Run the standard render systems (FrustumCullSystem, ShadowCullSystem, DrawCallBuildSystem)
4. Render gizmos on the overlay view
5. Render grid on a dedicated view (after opaque, before gizmos)

### 6.2 Mouse Picking

Two approaches available, in order of preference:

**Approach A -- CPU Raycast (recommended for initial implementation):**
1. Unproject the mouse click position into a world-space ray using the editor camera's inverse VP matrix
2. Iterate all entities with `MeshComponent` + `WorldTransformComponent`
3. Test ray against each entity's AABB (from the `Mesh` bounds in `RenderResources`)
4. Select the nearest hit entity

**Approach B -- GPU ID Buffer (higher accuracy, deferred):**
1. Render the scene to a 1-pixel or small-rect offscreen framebuffer with entity IDs encoded in the color output
2. Read back the pixel at the click position
3. Decode the EntityID
4. Requires a dedicated shader and readback (latency concern on some platforms)

### 6.3 Camera Controls

The editor camera supports two modes, toggled via a toolbar button or shortcut:

**Orbit mode (default):** Uses the existing `OrbitCamera` pattern. RMB-drag to orbit, scroll to zoom, middle-click drag to pan. F key frames the selection (moves the orbit target to the selected entity's position).

**Fly mode:** WASD + mouse look (FPS-style). Hold RMB to activate. Shift for sprint. Useful for navigating large scenes.

### 6.4 Grid

An infinite ground-plane grid rendered as a single full-screen quad with a fragment shader that computes grid lines from world-space coordinates. The grid fades with distance to avoid aliasing. Uses view ID kViewOpaque - 1 (or a dedicated editor view) with depth write disabled.

### 6.5 Selection Highlight

Selected entities are highlighted with an outline effect:
1. Render selected entities to a stencil buffer
2. Dilate the stencil (3x3 or 5x5 kernel) in a post-process pass
3. Draw the dilated area minus the original stencil as a colored outline

Alternative: render selected entities in wireframe mode with a bright color on an overlay view. Simpler but less visually polished.

### 6.6 Multiple Viewports

The bottom tab area can host a "Game View" tab that is a second viewport rendering through a different camera (the in-scene `CameraComponent` entity). This viewport has its own native view, its own bgfx framebuffer, and its own set of bgfx view IDs (48-63 for scene view, 64-79 for game view).

Additional orthographic viewports (top/front/side) can be added as split panes within the center area. Each gets its own framebuffer and camera.

---

## 7. Undo/Redo System

### 7.1 Command Pattern

```cpp
// engine/editor/undo/ICommand.h
namespace engine::editor
{

class ICommand
{
public:
    virtual ~ICommand() = default;

    // Human-readable description for the Edit menu (e.g. "Move Entity").
    virtual const char* description() const = 0;

    // Execute the command (first time or redo).
    virtual void execute() = 0;

    // Reverse the command.
    virtual void undo() = 0;

    // Optional: merge with a subsequent command of the same type.
    // Returns true if merged (the other command is then discarded).
    // Used for continuous edits like slider drags.
    virtual bool mergeWith(const ICommand& other) { return false; }
};

} // namespace engine::editor
```

### 7.2 Command Stack

```cpp
// engine/editor/undo/CommandStack.h
namespace engine::editor
{

class CommandStack
{
public:
    // Execute a command and push it onto the undo stack.
    // Clears the redo stack.
    void execute(std::unique_ptr<ICommand> cmd);

    // Undo the most recent command. Returns false if nothing to undo.
    bool undo();

    // Redo the most recently undone command. Returns false if nothing to redo.
    bool redo();

    // Returns the description of the next undoable command, or nullptr.
    const char* undoDescription() const;
    const char* redoDescription() const;

    // Set the maximum undo depth (default: 100).
    void setMaxDepth(size_t depth);

    // Returns true if the stack has been modified since the last save point.
    bool isDirty() const;

    // Mark the current state as the save point (called after File > Save).
    void markSaved();

private:
    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
    size_t maxDepth_ = 100;
    size_t savedIndex_ = 0;
};

} // namespace engine::editor
```

### 7.3 Built-in Command Types

Located in `engine/editor/undo/commands/`:

| Command | Captures | Execute | Undo |
|---------|----------|---------|------|
| `SetTransformCommand` | entity, old transform, new transform | write new transform | write old transform |
| `SetMaterialCommand` | material ID, old Material, new Material | write new material | write old material |
| `SetComponentFieldCommand<T>` | entity, field offset, old value, new value | write new | write old |
| `ReparentEntityCommand` | entity, old parent, new parent, old child index | setParent(new) | setParent(old) + reinsert at old index |
| `CreateEntityCommand` | captures all components after creation | re-create entity | destroy entity |
| `DeleteEntityCommand` | captures all components + children before deletion | destroy entity | re-create entity + components + children |
| `DuplicateEntityCommand` | source entity, created entity | (same as create) | (same as delete) |
| `AddComponentCommand<T>` | entity, component data | emplace component | remove component |
| `RemoveComponentCommand<T>` | entity, captured component data | remove component | emplace component |

### 7.4 Keyboard Shortcuts

Handled at the `IEditorWindow` level:
- macOS: Cmd+Z (undo), Cmd+Shift+Z (redo)
- Windows: Ctrl+Z (undo), Ctrl+Y (redo)

The Edit menu items ("Undo <description>", "Redo <description>") update dynamically from `CommandStack::undoDescription()` / `redoDescription()`.

---

## 8. Asset Browser

### 8.1 Architecture

```cpp
// engine/editor/assets/AssetBrowser.h
namespace engine::editor
{

class AssetBrowser : public IEditorPanel
{
public:
    // Set the root directory to display.
    void setRootPath(const std::string& path);

    // Navigate into a subdirectory.
    void navigateTo(const std::string& path);

    // Current path.
    const std::string& currentPath() const;

    // Get selected asset path.
    const std::string& selectedAsset() const;

    // ...IEditorPanel overrides...
};

} // namespace engine::editor
```

### 8.2 File System Watching

Uses platform-specific APIs:
- macOS: `FSEvents` (via `FSEventStreamCreate`)
- Windows: `ReadDirectoryChangesW`

When a file changes on disk, the browser refreshes the affected directory. If the changed file is a currently-loaded asset (texture, mesh), the editor triggers a hot-reload through the existing `AssetManager`.

### 8.3 Display

**Grid view (default):** Icons/thumbnails in a grid. On macOS: `NSCollectionView`. On Windows: `ListView` with `LVS_ICON` style.

**List view:** Rows with name, type, size, modified date. On macOS: `NSTableView`. On Windows: `ListView` with `LVS_REPORT` style.

Thumbnail generation:
- Textures: decode the image at a small resolution (64x64), display as the icon
- Meshes (.glb/.gltf): render a preview to an offscreen framebuffer using the engine's renderer, capture as an image
- Audio: generic waveform icon
- Scenes (.json): generic scene icon

### 8.4 Drag-Drop to Scene

Dragging an asset file from the browser to the 3D viewport:
1. Begin drag with the file path as the drag payload
2. On drop in the viewport, unproject the drop position to a world-space point on the ground plane (Y=0)
3. Create a new entity at that position:
   - .glb/.gltf: load via `AssetManager` + `GltfSceneSpawner`, position at drop point
   - .png/.jpg: create a sprite entity or apply as texture to selected entity's material
   - .wav/.ogg: create an entity with `AudioSourceComponent`
4. Wrap in a `CreateEntityCommand` for undo

### 8.5 Supported File Types

| Extension | Type | Action on drag-to-scene |
|-----------|------|-------------------------|
| .glb, .gltf | 3D model | Spawn full glTF scene hierarchy |
| .png, .jpg, .tga | Texture | Apply to selected entity's material, or create sprite |
| .wav, .ogg | Audio | Create entity with AudioSourceComponent |
| .json | Scene | Open in editor (replace current scene with confirmation) |

---

## 9. Menu Bar

### 9.1 Structure

**File:**
- New Scene (Cmd/Ctrl+N) -- clears registry, resets to empty scene
- Open Scene... (Cmd/Ctrl+O) -- file dialog, calls `SceneSerializer::loadScene`
- Save (Cmd/Ctrl+S) -- calls `SceneSerializer::saveScene` to current path
- Save As... (Cmd/Ctrl+Shift+S) -- file dialog, then save
- ---
- Recent Files > (submenu with last 10 opened files)
- ---
- Export > (submenu: macOS App Bundle, Windows Executable -- future)
- ---
- Quit (Cmd/Ctrl+Q)

**Edit:**
- Undo <description> (Cmd/Ctrl+Z)
- Redo <description> (Cmd/Ctrl+Shift+Z or Ctrl+Y)
- ---
- Cut (Cmd/Ctrl+X) -- copy + delete selected entities
- Copy (Cmd/Ctrl+C) -- serialize selected entities to clipboard
- Paste (Cmd/Ctrl+V) -- deserialize entities from clipboard
- Delete (Delete/Backspace) -- delete selected entities
- ---
- Select All (Cmd/Ctrl+A)
- Deselect All (Cmd/Ctrl+D)
- ---
- Preferences... (Cmd/Ctrl+,) -- opens settings window

**View:**
- Toggle Hierarchy Panel
- Toggle Properties Panel
- Toggle Asset Browser
- Toggle Console
- Toggle Animation Timeline
- ---
- Reset Layout

**Entity:**
- Create Empty (Cmd/Ctrl+Shift+N)
- ---
- Create Cube
- Create Sphere
- Create Plane
- Create Cylinder
- ---
- Create Point Light
- Create Spot Light
- Create Directional Light
- ---
- Create Camera

**Component:** (enabled when an entity is selected)
- Add Transform
- Add Mesh
- Add Material
- Add Rigid Body
- Add Collider
- Add Animator
- Add Audio Source
- Add Camera
- (dynamically extended by registered component inspectors)

**Window:**
- Preferences
- About Sama

### 9.2 Implementation

On macOS, the menu bar is the system menu bar (`[NSApp setMainMenu:]`), which is native and expected. On Windows, it is an `HMENU` attached to the window via `SetMenu()`.

---

## 10. Serialization Integration

### 10.1 Scene Save/Load

The editor uses the existing `SceneSerializer` directly:

```cpp
// Save
SceneSerializer serializer;
serializer.registerEngineComponents();
// + any custom plugin component registrations
serializer.saveScene(registry, renderResources, filepath);

// Load
registry = ecs::Registry();  // fresh registry
serializer.loadScene(filepath, registry, renderResources, assetManager);
```

### 10.2 Project File

A separate JSON file (`.sama-project`) at the project root:

```json
{
    "name": "My Game",
    "version": "0.1.0",
    "startScene": "assets/scenes/main.json",
    "buildSettings": {
        "windowWidth": 1280,
        "windowHeight": 720,
        "windowTitle": "My Game",
        "targetPlatforms": ["macos", "windows"]
    },
    "editorSettings": {
        "lastOpenScene": "assets/scenes/level1.json",
        "recentFiles": [],
        "panelLayout": { ... }
    }
}
```

### 10.3 Auto-Save / Crash Recovery

- Every 60 seconds (configurable), the editor saves the current scene to a temporary file: `<project>/.sama/autosave.json`
- On startup, if an autosave exists that is newer than the last saved scene, prompt: "Recover unsaved changes?"
- The autosave file is deleted on clean exit or after a successful manual save

---

## 11. Build System Integration

### 11.1 CMake Structure

```cmake
# engine/editor/CMakeLists.txt

# Cross-platform editor core (undo, gizmo math, EditorState)
set(EDITOR_CORE_SOURCES
    editor/EditorApp.cpp
    editor/EditorState.cpp
    editor/undo/CommandStack.cpp
    editor/undo/commands/SetTransformCommand.cpp
    editor/undo/commands/ReparentEntityCommand.cpp
    editor/undo/commands/CreateEntityCommand.cpp
    editor/undo/commands/DeleteEntityCommand.cpp
    editor/gizmo/TransformGizmo.cpp
    editor/gizmo/GizmoRenderer.cpp
    editor/inspector/TransformInspector.cpp
    editor/inspector/MaterialInspector.cpp
    editor/inspector/LightInspector.cpp
    editor/inspector/PhysicsInspector.cpp
    editor/inspector/AnimatorInspector.cpp
    editor/inspector/AudioInspector.cpp
    editor/inspector/CameraInspector.cpp
    editor/assets/AssetBrowser.cpp
)

if(APPLE)
    set(EDITOR_PLATFORM_SOURCES
        editor/platform/cocoa/CocoaEditorWindow.mm
        editor/platform/cocoa/CocoaMenuBar.mm
        editor/platform/cocoa/CocoaToolbar.mm
        editor/platform/cocoa/MetalViewportView.mm
        editor/platform/cocoa/CocoaHierarchyPanel.mm
        editor/platform/cocoa/CocoaPropertiesPanel.mm
        editor/platform/cocoa/CocoaAssetBrowserPanel.mm
        editor/platform/cocoa/CocoaConsolePanel.mm
    )
elseif(WIN32)
    set(EDITOR_PLATFORM_SOURCES
        editor/platform/win32/Win32EditorWindow.cpp
        editor/platform/win32/Win32MenuBar.cpp
        editor/platform/win32/Win32Toolbar.cpp
        editor/platform/win32/D3DViewportView.cpp
        editor/platform/win32/Win32HierarchyPanel.cpp
        editor/platform/win32/Win32PropertiesPanel.cpp
        editor/platform/win32/Win32AssetBrowserPanel.cpp
        editor/platform/win32/Win32ConsolePanel.cpp
    )
endif()

add_library(engine_editor STATIC ${EDITOR_CORE_SOURCES} ${EDITOR_PLATFORM_SOURCES})

target_link_libraries(engine_editor PUBLIC
    engine
    engine_core
    engine_rendering
    engine_scene
    engine_assets
)

if(APPLE)
    target_link_libraries(engine_editor PRIVATE
        "-framework AppKit"
        "-framework QuartzCore"  # CAMetalLayer
    )
endif()

# Editor executable
add_executable(sama_editor apps/editor/main.cpp)  # or main.mm on macOS
target_link_libraries(sama_editor PRIVATE engine_editor)
```

### 11.2 Conditional Compilation

The editor target (`sama_editor`) is separate from the game runtime. Game builds (`sama_game`) link only the engine libraries, not `engine_editor`. This matches the NOTES.md requirement that "release builds have editor code stripped."

Preprocessor guard `SAMA_EDITOR` is defined only for the editor target, allowing engine code to include optional editor hooks:

```cpp
#ifdef SAMA_EDITOR
    // Register editor-only debug visualizations
#endif
```

### 11.3 Relationship to Existing Engine

The editor does NOT modify `Engine::init()` or `Engine::beginFrame()` / `Engine::endFrame()`. Instead, the editor has its own main loop:

```cpp
// Pseudocode for the editor main loop
EditorApp app;
app.init();

while (app.window().pollEvents())
{
    app.beginFrame();           // polls native events, updates editor UI
    app.updateEngine(dt);       // runs ECS systems, physics, etc. if in play mode
    app.renderViewports();      // renders scene into viewport framebuffers
    app.endFrame();             // submits bgfx frame
}

app.shutdown();
```

The editor creates its own `Engine` instance internally but replaces the GLFW window with the native editor window's main view for bgfx initialization. The `RendererDesc::nativeWindowHandle` points to the main viewport's native view (CAMetalLayer on Mac, HWND on Windows), not the editor window itself.

---

## 12. Implementation Phases

### Phase 1: Basic Window with 3D Viewport (Estimated: 2-3 weeks)
- Create `IEditorWindow` interface and macOS `CocoaEditorWindow` implementation
- NSWindow with a single NSView that hosts a CAMetalLayer
- Initialize bgfx with the CAMetalLayer as the native window handle
- Render a test scene (colored cube + grid) through the engine's renderer
- Basic orbit camera in the viewport
- **Deliverable:** A native macOS window showing a 3D scene rendered via bgfx

### Phase 2: Scene Hierarchy Panel + Entity Selection (2 weeks)
- Implement `IEditorPanel` and macOS `CocoaHierarchyPanel` with NSOutlineView
- NSSplitView for left sidebar layout
- Click entity in hierarchy to select; selection state in `EditorState`
- Selection highlight in viewport (wireframe overlay)
- **Deliverable:** Tree view of entities, click to select, selection visible in 3D

### Phase 3: Properties Inspector with Transform Editing (2 weeks)
- Implement `IComponentInspector` interface
- `TransformInspector` with NSTextField fields for position/rotation/scale
- Right sidebar with NSScrollView containing inspector views
- Editing a field immediately updates the ECS component
- **Deliverable:** Select entity, edit transform in inspector, see change in viewport

### Phase 4: Transform Gizmos (2-3 weeks)
- Implement `TransformGizmo` state machine (translate/rotate/scale modes)
- Implement `GizmoRenderer` (procedural mesh, unlit shader, overlay view)
- Mouse raycast against gizmo geometry
- Drag interaction with constraint projection
- W/E/R key switching, local/world toggle, snap-to-grid
- **Deliverable:** Full 3-mode transform gizmo with keyboard shortcuts

### Phase 5: Undo/Redo System (1 week)
- Implement `CommandStack`
- Implement `SetTransformCommand` (wraps gizmo and inspector edits)
- Implement `ReparentEntityCommand`, `CreateEntityCommand`, `DeleteEntityCommand`
- Wire Cmd+Z / Cmd+Shift+Z
- **Deliverable:** All transform edits, entity creation/deletion, and reparenting are undoable

### Phase 6: Menu Bar + Context Menus (1 week)
- Implement `IEditorMenuBar` and macOS `CocoaMenuBar`
- File > New/Open/Save/Save As using NSOpenPanel / NSSavePanel
- Edit > Undo/Redo with dynamic descriptions
- Entity > Create menu items
- Right-click context menus on hierarchy panel
- **Deliverable:** Full menu bar, file save/load working

### Phase 7: Asset Browser (2 weeks)
- Implement `AssetBrowser` panel with NSCollectionView
- Directory navigation with breadcrumb bar
- File type icons and texture thumbnails
- Drag-drop from browser to viewport (spawn entity)
- FSEvents file watcher for live directory updates
- **Deliverable:** Browse assets, drag mesh files into the scene

### Phase 8: Additional Component Inspectors (2 weeks)
- Material inspector with color picker and texture slots
- Light inspectors (directional, point, spot)
- Physics inspectors (rigid body, collider)
- Audio inspector
- Camera inspector
- "Add Component" dropdown
- **Deliverable:** All built-in components editable in the inspector

### Phase 9: Console + Animation Timeline (2 weeks)
- Console panel: NSTextView (read-only) connected to engine log output
- Log levels (info/warning/error) with color coding and filtering
- Animation timeline panel: horizontal track view with keyframe markers
- Scrubber bar, play/pause/stop controls
- **Deliverable:** Log output visible, basic animation scrubbing

### Phase 10: Resource Usage Inspector (2 weeks)
- **CPU panel:** Per-system frame time breakdown (animation, physics, transform, cull, draw call submission) with rolling history graph. Data sourced from `std::chrono::high_resolution_clock` timing wrappers around each system's `update()`.
- **GPU panel:** bgfx stats integration (`bgfx::getStats()`) — draw calls, triangles, texture memory, buffer memory, GPU time per view, encoder stats. Displayed as a live dashboard with sparkline graphs.
- **Memory panel:** FrameArena usage (bytes used / capacity), total heap allocations (tracked via custom allocator or platform API), per-system arena breakdown, texture/mesh/material counts from RenderResources.
- **Entity panel:** Total entity count, component type distribution (how many entities have each component type), sparse set fill ratios.
- **Network/Asset panel (optional):** Pending asset loads, asset cache hit/miss rates, total loaded asset memory.
- **Implementation:** `IResourceInspector` interface with `CocoaResourcePanel` (NSTableView + custom NSView for graphs). Lightweight ring-buffer (`InlinedVector<float, 120>`) stores 2 seconds of history at 60fps per metric. All data collection behind `#if SAMA_EDITOR` to avoid overhead in game builds.
- **Deliverable:** Tabbed resource panel showing CPU, GPU, memory, and entity stats with live graphs

### Phase 11: Multi-Viewport + Game View (1-2 weeks)
- Tab bar in bottom area with "Game View" tab
- Second viewport rendering through the in-scene camera
- Play/Pause/Stop toolbar buttons
- In play mode: game systems run, editor camera frozen; game view is "live"
- **Deliverable:** Side-by-side scene editor and game preview

### Phase 12: Windows Platform (3-4 weeks)
- Implement all `IEditorWindow` / `IEditorPanel` interfaces for Win32
- Port all Cocoa panels to Win32 equivalents
- Test bgfx rendering into child HWND (D3D11/D3D12 backend)
- **Deliverable:** Editor running natively on Windows

### Phase 13: Plugin System (2 weeks)
- Plugin API: shared library (.dylib / .dll) that exports a registration function
- Plugins can register `IComponentInspector`, `IEditorPanel`, menu items
- Hot-reload support: watch plugin .dylib for changes, unload/reload
- **Deliverable:** Custom panels and inspectors loadable from plugins

### Phase 14: Build & Publish Pipeline (4-6 weeks)

#### 14.1 Asset Baker (`engine/build/AssetBaker`)

A standalone library (usable from both the editor and a CLI tool) that transforms development-time assets into optimized, platform-specific binary formats:

| Asset type | Dev format | Baked format | Tool |
|-----------|-----------|-------------|------|
| Scenes | `.json` | `.scene.bin` (flat binary, memory-mappable) | Custom serializer (already have `SceneSerializer`) |
| Textures | `.png`, `.jpg`, `.hdr` | `.ktx2` with BC7 (desktop), ASTC 4x4 (mobile), ETC2 (fallback) | basisu / KTX-Software (`toktx`) |
| Meshes | `.glb`, `.obj` | `.mesh.bin` (pre-indexed, quantized, meshoptimizer-optimized) | meshoptimizer for vertex cache + overdraw + fetch optimization |
| Shaders | `.sc` (bgfx) | `.bin` per profile (Metal, SPIR-V, HLSL, GLSL) | bgfx `shaderc` (already integrated) |
| Audio | `.wav`, `.ogg`, `.mp3` | `.ogg` (normalized loudness, resampled to target rate) | SoLoud handles decoding at runtime; baking just normalizes |
| Animation | embedded in `.glb` | `.anim.bin` (flat keyframe arrays, no JSON overhead) | Custom exporter from `AnimationResources` |

**Key design decisions:**
- **Dependency graph:** The baker tracks which source files produce which baked assets. Only re-bakes when sources change (content hash, not timestamp, to avoid false rebuilds).
- **Parallel baking:** Texture compression is the bottleneck (~2-10s per texture for ASTC). Baker dispatches to a thread pool. Each asset is independently bakeable.
- **Platform profiles:** A profile defines the target platform's capabilities:
  ```cpp
  struct PlatformProfile
  {
      std::string name;               // "macOS", "iOS", "Windows", "Android"
      TextureFormat textureFormat;     // BC7, ASTC_4x4, ETC2
      bgfx::RendererType::Enum renderer; // Metal, Vulkan, D3D12
      uint32_t maxTextureSize;         // 4096 for mobile, 8192 for desktop
      bool compressAudio;              // true for mobile (smaller OGG quality)
  };
  ```
- **Asset manifest:** The baked output includes a `manifest.json` listing every asset with its path, size, content hash, and dependencies. The runtime `AssetManager` uses this for fast lookup instead of scanning the filesystem.

#### 14.2 Build Settings Panel

A new editor panel (`CocoaBuildSettingsPanel` / Win32 equivalent) accessible from **File > Build Settings**:

```
+----------------------------------------------------------+
|  Build Settings                                           |
+----------------------------------------------------------+
|  Target Platform:  [macOS ▼]                             |
|  Build Config:     [Release ▼]  (Debug / Release / Profile)|
|  Renderer Backend: [Metal ▼]   (auto-selected per platform)|
|                                                           |
|  --- Application ---                                      |
|  Bundle ID:        [com.studio.mygame        ]           |
|  Version:          [1.0.0                    ]           |
|  App Icon:         [icon_512.png        ] [Browse]       |
|  Window Title:     [My Game              ]               |
|                                                           |
|  --- Quality ---                                          |
|  Shadow Resolution:  [2048 ▼]                            |
|  SSAO:               [✓ On ]                             |
|  Bloom:              [✓ On ]                             |
|  Max Texture Size:   [4096 ▼]                            |
|                                                           |
|  --- Assets ---                                           |
|  Texture Compression: [BC7 (Desktop) ▼]                  |
|  Strip Debug Data:    [✓ Yes]                            |
|  Scenes to Include:   [All ▼] / [Selected...]           |
|                                                           |
|  [Build]  [Build & Run]  [Clean]                         |
+----------------------------------------------------------+
```

Settings are stored per-project in `.sama-project/build_settings.json`. Each platform target can override any setting.

#### 14.3 Platform Builders

Each platform has a builder module that takes baked assets + compiled executable and produces the final distributable:

**macOS (.app bundle):**
```
MyGame.app/
  Contents/
    MacOS/
      MyGame              (universal binary: arm64 + x86_64)
    Resources/
      assets/             (baked assets)
      AppIcon.icns
    Info.plist            (generated from build settings)
    _CodeSignature/       (codesign -s "Developer ID")
```
- Notarization: `xcrun notarytool submit MyGame.zip` → staple ticket
- DMG creation: `hdiutil create` with background image

**Windows (.exe + installer):**
```
MyGame/
  MyGame.exe
  assets/               (baked assets)
  d3d12.dll / vulkan-1.dll  (if not statically linked)
```
- Optional NSIS/WiX installer generation
- Code signing via `signtool.exe`

**iOS (.ipa):**
```
MyGame.app/
  MyGame                (arm64 only)
  assets/               (baked assets, ASTC textures)
  Info.plist
  embedded.mobileprovision
```
- Requires Xcode project generation (or `xcodebuild` with a template `.xcodeproj`)
- Build via `xcodebuild -scheme MyGame -configuration Release -archivePath ...`
- Export: `xcodebuild -exportArchive` with export options plist
- Upload: `xcrun altool --upload-app` or Transporter

**Android (.apk / .aab):**
```
app/
  src/main/
    jniLibs/arm64-v8a/
      libMyGame.so      (native activity)
    assets/             (baked assets, ASTC/ETC2 textures)
    AndroidManifest.xml
  build.gradle
```
- Gradle wrapper generated from a template
- NDK builds the native library
- `./gradlew assembleRelease` produces the APK/AAB
- Keystore signing configured in build settings

#### 14.4 Build Menu Integration

Added to the editor menu bar (Phase 6):

```
File
  ├── New Scene
  ├── Open Scene...
  ├── Save Scene         ⌘S
  ├── Save Scene As...   ⇧⌘S
  ├── ─────────────────
  ├── Build Settings...  ⌘B
  ├── Build              ⇧⌘B
  ├── Build & Run        ⌘R
  ├── Clean Build        ⇧⌘K
  └── ─────────────────
```

Build output streams to the Console panel (Phase 9) with progress bar in the status bar.

#### 14.5 CLI Build Mode (Headless)

The editor binary supports headless builds for CI/CD pipelines:

```bash
# Build for macOS release
sama_editor --build --platform=macOS --config=Release --project=./MyGame.sama-project

# Build for iOS, skip notarization
sama_editor --build --platform=iOS --config=Release --no-sign

# Bake assets only (no compile)
sama_editor --bake --platform=Android --output=./baked/

# List available platforms
sama_editor --list-platforms
```

CI integration example (GitHub Actions):
```yaml
jobs:
  build-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake --build build --target sama_editor -j$(sysctl -n hw.ncpu)
      - run: ./build/sama_editor --build --platform=macOS --config=Release
      - uses: actions/upload-artifact@v4
        with:
          name: MyGame-macOS
          path: build/output/MyGame.app
```

#### 14.6 File Layout

```
engine/build/
    AssetBaker.h             -- IAssetBaker interface
    AssetBaker.cpp           -- dependency graph, parallel dispatch
    TextureBaker.h/.cpp      -- PNG/JPG → KTX2 (BC7/ASTC/ETC2)
    MeshBaker.h/.cpp         -- glTF/OBJ → optimized binary mesh
    SceneBaker.h/.cpp        -- JSON scene → binary scene
    ShaderBaker.h/.cpp       -- bgfx shaderc wrapper
    AudioBaker.h/.cpp        -- normalization + resampling
    AnimationBaker.h/.cpp    -- clip data → flat binary
    PlatformProfile.h        -- per-platform settings struct
    PlatformBuilder.h        -- IPlatformBuilder interface

engine/build/platforms/
    MacOSBuilder.h/.cpp      -- .app bundle, codesign, notarize, DMG
    WindowsBuilder.h/.cpp    -- .exe packaging, signtool, NSIS
    IOSBuilder.h/.cpp        -- Xcode project gen, archive, export
    AndroidBuilder.h/.cpp    -- Gradle wrapper, NDK, APK/AAB

editor/panels/
    BuildSettingsPanel.h     -- platform-independent build settings UI logic
    CocoaBuildSettingsPanel.mm  -- macOS AppKit implementation
```

- **Deliverable:** Full build pipeline from editor or CLI. Bake assets, compile, package, sign, and optionally upload for macOS, Windows, iOS, and Android.

---

## 13. Technology Choices

### 13.1 Native Per-Platform vs. Cross-Platform Toolkit

| Option | Pros | Cons |
|--------|------|------|
| **Native per-platform (AppKit + Win32)** | Pixel-perfect native feel; zero dependency; system dark mode, accessibility, font rendering for free; smallest binary | Two implementations per panel; higher total code volume |
| **Qt** | Single codebase; mature widget set; QOpenGLWidget for bgfx embedding | 200+ MB dependency; LGPL or $5k/yr commercial license; "almost native" but visually off on macOS; MOC build step |
| **wxWidgets** | Single codebase; actually uses native widgets; permissive license | Less polished than Qt; smaller community; some widgets are not truly native on all platforms |
| **Dear ImGui** (current NOTES.md plan) | Single codebase; trivial integration; zero widget library | Not native; poor text editing; no accessibility; no system integration (file dialogs, drag-drop from Finder) |

**Decision: Native per-platform (AppKit + Win32).** The engine is a solo-developer project with macOS as the primary development platform. Phase 1 through Phase 10 target macOS only. Windows is Phase 11. The overhead of two implementations is manageable because the core editor logic (undo, gizmo math, component inspectors' data logic) is platform-independent C++; only the view layer differs.

Linux is deferred. When needed, GTK4 is recommended over Qt for licensing simplicity and smaller footprint.

### 13.2 Specific Technology Selections

| Component | macOS | Windows | Shared |
|-----------|-------|---------|--------|
| Main window | NSWindow | HWND + WS_OVERLAPPEDWINDOW | IEditorWindow |
| Split layout | NSSplitView (nested) | Custom splitter HWNDs | PanelPosition enum |
| Hierarchy tree | NSOutlineView | TreeView (WC_TREEVIEW) | IEditorPanel |
| Properties | NSScrollView + NSStackView | Scrollable child HWND + custom controls | IComponentInspector |
| Asset browser | NSCollectionView | ListView (LVS_ICON) | AssetBrowser |
| Console | NSTextView (read-only) | RichEdit (read-only) | IEditorPanel |
| Color picker | NSColorWell + NSColorPanel | ChooseColor() | Color well widget |
| File dialogs | NSOpenPanel / NSSavePanel | IFileOpenDialog / IFileSaveDialog | Menu callbacks |
| Menu bar | NSMenu (system menu bar) | HMENU | IEditorMenuBar |
| Toolbar | NSToolbar | Rebar / custom toolbar HWND | IEditorToolbar |
| 3D viewport | NSView + CAMetalLayer | Child HWND | bgfx::createFrameBuffer(nwh) |
| File watching | FSEvents | ReadDirectoryChangesW | IFileWatcher |
| Keyboard shortcuts | NSEvent key equivalents | WM_KEYDOWN + accelerator table | Shortcut registry |

### 13.3 Rendering Backend

bgfx selects Metal on macOS and D3D11/D3D12 on Windows automatically. The editor does not constrain this; `bgfx::createFrameBuffer(nwh, w, h)` works with any backend. On macOS, the native view must provide a `CAMetalLayer` (already done in `GlfwWindow.cpp`). On Windows, the native view is a plain HWND (bgfx creates its own swap chain).

---

## 14. Editor State

A central `EditorState` object holds all shared editor state:

```cpp
// engine/editor/EditorState.h
namespace engine::editor
{

class EditorState
{
public:
    // Selection.
    std::vector<ecs::EntityID> selection;

    // Undo/redo.
    CommandStack commandStack;

    // Component inspector registry.
    InspectorRegistry& inspectorRegistry();

    // Current scene file path (empty = unsaved).
    std::string scenePath;

    // Project root directory.
    std::string projectRoot;

    // Gizmo mode.
    enum class GizmoMode { Translate, Rotate, Scale };
    GizmoMode gizmoMode = GizmoMode::Translate;

    // Coordinate space.
    enum class CoordSpace { Local, World };
    CoordSpace coordSpace = CoordSpace::World;

    // Snap settings.
    bool snapEnabled = false;
    float translateSnap = 0.5f;
    float rotateSnapDeg = 15.0f;
    float scaleSnap = 0.1f;

    // Play mode.
    enum class PlayState { Editing, Playing, Paused };
    PlayState playState = PlayState::Editing;

    // When entering play mode, the scene is serialized to memory.
    // On stop, the scene is restored from this snapshot.
    std::string playModeSnapshot;
};

} // namespace engine::editor
```

---

## 15. Console/Log Panel

### 15.1 Log Sink

The engine currently does not have a formal logging system. The editor introduces a simple log sink:

```cpp
// engine/editor/log/EditorLog.h
namespace engine::editor
{

enum class LogLevel : uint8_t { Info, Warning, Error };

struct LogEntry
{
    LogLevel level;
    std::string message;
    double timestamp;
};

class EditorLog
{
public:
    void log(LogLevel level, const char* fmt, ...);
    const std::vector<LogEntry>& entries() const;
    void clear();

    // Filter.
    void setMinLevel(LogLevel level);
};

} // namespace engine::editor
```

The console panel (`CocoaConsolePanel` / `Win32ConsolePanel`) reads from `EditorLog::entries()` and appends new entries to the text view each frame. Entries are color-coded: white for info, yellow for warning, red for error.

---

## 16. Animation Timeline Panel

### 16.1 Scope

The animation timeline is a post-MVP feature (Phase 9). It provides a visual keyframe editor for skeletal animation clips referenced by `AnimatorComponent`.

### 16.2 Layout

```
+--------+------------------------------------------------------------------+
| Track  |  0.0s    0.5s    1.0s    1.5s    2.0s    2.5s    3.0s           |
| Name   |  |        |        |        |        |        |        |        |
+--------+------------------------------------------------------------------+
| Root   |  [*]------|--------[*]------|--------[*]                        |
| Spine  |  [*]------[*]------|--------[*]------|--------[*]               |
| Head   |  [*]------|--------|--------[*]                                 |
| L.Arm  |  [*]------[*]------[*]------[*]------[*]                        |
| R.Arm  |  [*]------[*]------[*]------[*]------[*]                        |
+--------+------------------------------------------------------------------+
| [<<] [|>] [>>]  Time: [1.234]  Speed: [1.0x]  | Loop  | Clip: [walk_v] |
+--------+------------------------------------------------------------------+
```

On macOS: custom NSView with Core Graphics drawing for tracks and keyframes. On Windows: custom-painted child HWND with GDI+.

---

## 17. Plugin System

### 17.1 Plugin API

```cpp
// engine/editor/plugin/EditorPlugin.h
namespace engine::editor
{

struct EditorPluginAPI
{
    EditorState* state;
    ecs::Registry* registry;
    rendering::RenderResources* resources;
    InspectorRegistry* inspectors;

    // Register a custom panel.
    void (*addPanel)(IEditorPanel* panel, PanelPosition pos);

    // Register a custom menu item.
    void (*addMenuItem)(const char* menu, const MenuItem& item);
};

// Plugins export this function.
using PluginInitFn = void (*)(const EditorPluginAPI& api);

} // namespace engine::editor
```

Plugins are shared libraries loaded via `dlopen` (macOS/Linux) or `LoadLibrary` (Windows). The editor scans a `plugins/` directory at startup and calls each plugin's `sama_editor_plugin_init` exported symbol.

### 17.2 Hot Reload (Future)

Watch the plugin `.dylib` / `.dll` for changes. On change: call a `sama_editor_plugin_shutdown` export (if present), `dlclose`, `dlopen` the new version, call `sama_editor_plugin_init` again. Requires plugins to not hold persistent pointers into their own memory.

---

### Critical Files for Implementation

- `/Users/shayanj/claude/engine/engine/platform/Window.h` -- the `IWindow` interface that the editor window abstraction parallels and extends
- `/Users/shayanj/claude/engine/engine/platform/desktop/GlfwWindow.cpp` -- shows the existing pattern for creating a CAMetalLayer from a native window handle (lines 63-93), which the editor viewport must replicate
- `/Users/shayanj/claude/engine/engine/scene/SceneGraph.h` -- the hierarchy API (`setParent`, `getChildren`, `destroyHierarchy`) that the hierarchy panel and reparent commands call directly
- `/Users/shayanj/claude/engine/engine/scene/SceneSerializer.h` -- the extensible serialization system that the editor's save/load and undo/redo snapshot features build on
- `/Users/shayanj/claude/engine/engine/rendering/ViewIds.h` -- the bgfx view ID layout; the editor must allocate IDs above 47 for viewport framebuffers and gizmo overlay passes

---

## 18. Performance Requirements Review

This section establishes binding performance requirements for all editor implementation work. Every subsection includes concrete targets, identified risks, and required mitigations. Code that violates these requirements must not be merged.

---

### 18.1 Compile Time

**Risks identified in the architecture:**

1. **Objective-C++ compilation.** Every `.mm` file in `engine/editor/platform/cocoa/` must parse Objective-C runtime headers (`<AppKit/AppKit.h>`, `<QuartzCore/QuartzCore.h>`). These are massive headers (AppKit alone pulls in >100K lines). If any `.h` file in the editor includes them, the cost propagates to every translation unit that includes that header.

2. **Template-heavy engine headers.** The editor depends on `Registry` (heavily templated `view<Components...>()`, `emplace<T>()`, etc.), `InlinedVector<T, N>`, and `PoolAllocator<T, MaxCount>`. Each editor `.cpp` that touches the ECS will instantiate these templates.

3. **`IComponentInspector` implementations.** There are 7+ built-in inspectors (Section 4.2). If each inspector header pulls in its component type's full definition plus the ECS registry, the include graph fans out quickly.

4. **`std::function` in `MenuItem`.** The `MenuCallback = std::function<void()>` in `IEditorMenuBar.h` pulls in `<functional>`, which is a heavy standard library header.

**Required mitigations:**

- **No platform headers in any `.h` file.** The following headers must never appear in any file under `engine/editor/` with a `.h` extension:
  - `<Cocoa/Cocoa.h>`, `<AppKit/AppKit.h>`, `<QuartzCore/QuartzCore.h>`
  - `<objc/runtime.h>`, `<objc/message.h>`
  - `<windows.h>`, `<commctrl.h>`, `<d3d11.h>`, `<d3d12.h>`
  - Any Objective-C `@class` or `@protocol` forward declaration (use `void*` opaque handles in the C++ interface instead)
  - Platform types are confined to `.mm` and `.cpp` implementation files only.

- **Pimpl pattern for all platform panel implementations.** Each Cocoa/Win32 panel class must use a forward-declared `struct Impl` in its header and `std::unique_ptr<Impl>` as its only data member. The `Impl` definition (containing NSView*, HWND, etc.) lives in the `.mm` / `.cpp` file. Example:
  ```cpp
  // CocoaHierarchyPanel.h
  class CocoaHierarchyPanel : public IEditorPanel
  {
  public:
      CocoaHierarchyPanel();
      ~CocoaHierarchyPanel();
      // ...IEditorPanel overrides...
  private:
      struct Impl;
      std::unique_ptr<Impl> impl_;
  };
  ```

- **Forward-declare all engine types in editor headers.** Editor headers must forward-declare `ecs::Registry`, `ecs::EntityID`, `rendering::RenderResources`, etc. rather than including their full definitions. Only the `.cpp` files include the full headers.

- **Precompiled header (PCH) for the editor target.** Create `engine/editor/EditorPCH.h` containing only:
  - `<cstdint>`, `<cstddef>`, `<string>`, `<memory>`, `<vector>`
  - `engine/ecs/EntityID.h` (a lightweight typedef)
  - Do NOT include `Registry.h`, `bgfx/bgfx.h`, or any rendering headers in the PCH.

- **Translation unit organization.** Each inspector (TransformInspector, MaterialInspector, etc.) must be its own `.cpp` file. Do not create "unity builds" that merge inspector files -- they each pull in different component headers and would defeat incremental compilation.

- **Target: a single-file change in an inspector `.cpp` must recompile in <3 seconds.** A full clean build of `engine_editor` must complete in <30 seconds (excluding third-party dependencies).

---

### 18.2 Startup Time

**What happens at editor launch (from the architecture):**

1. Create native window (`NSWindow` / `HWND`)
2. Create split view layout and all panel native views
3. Initialize bgfx with the viewport's `CAMetalLayer` / `HWND`
4. Load shaders (PBR, shadow, skinned PBR, skinned shadow -- 4 programs)
5. Create default textures (white 1x1, neutral normal, white cubemap)
6. Load the last-open scene from `.sama-project` (`editorSettings.lastOpenScene`)
7. Build the hierarchy panel's tree from the loaded scene
8. Generate asset browser thumbnails
9. Load plugins from `plugins/` directory

**Risks:**

- Loading a large scene (10K entities, many textures/meshes) blocks the main thread during step 6. The user stares at a blank window.
- Shader compilation on first launch (no pipeline cache) can take 100ms+ per program on Metal.
- Asset browser thumbnail generation (mesh preview renders) is unbounded in cost.
- Plugin loading (`dlopen` + initialization) is serial and unpredictable.

**Required mitigations:**

- **Window must be visible before any asset loading.** The startup sequence must be:
  1. Create window and show it with the editor layout (empty panels) -- this must complete in <500ms.
  2. Display a lightweight splash/loading indicator in the viewport area (a simple "Loading..." label or progress bar rendered by the native UI, not bgfx).
  3. Initialize bgfx and load shaders.
  4. Begin async scene load on a background thread via `AssetManager`.
  5. As assets arrive, incrementally populate the hierarchy panel and viewport.
  6. Generate asset browser thumbnails lazily (only for visible items, on a background thread).

- **Deferred shader compilation.** Use bgfx's pipeline state cache (`BGFX_CONFIG_MAX_DRAW_CALLS` is already set). On first launch, accept the one-time cost. On subsequent launches, the Metal pipeline cache should make shader compilation near-instant. Do not block window creation on shader load.

- **Plugin loading is async and non-blocking.** Plugins load on a background thread after the main window is visible. Plugin UI elements appear as they finish loading.

- **Targets:**
  - Editor window visible and responsive: **<1 second** from process launch.
  - Empty scene fully rendered in viewport: **<2 seconds**.
  - Scene with 1000 entities fully loaded and hierarchy populated: **<3 seconds**.
  - Scene with 10,000 entities fully loaded: **<10 seconds** (with progressive hierarchy population -- the tree fills in as entities load).

---

### 18.3 UI Responsiveness

**Interaction paths analyzed:**

1. **Hierarchy panel click** (select entity): reads `EntityID` from `NSOutlineView` selection, updates `EditorState::selection`, triggers inspector refresh.
2. **Inspector field edit** (type a number): validates input, writes to ECS component, marks transform dirty, viewport re-renders next frame.
3. **Gizmo drag** (translate/rotate/scale): per-frame mouse delta projection, component write, viewport re-render.
4. **Viewport orbit** (RMB drag): camera matrix update, re-render.
5. **Search/filter in hierarchy**: string match against all entity names, rebuild visible tree.

**Risks:**

- **Scene graph traversal on every hierarchy click.** If the hierarchy panel calls `scene::getChildren()` recursively for the entire tree on every selection change, a 10K-entity scene could take several milliseconds.
- **Full scene re-render on every property change.** If typing a digit in a position field triggers a full shadow + opaque + transparent + post-process pass, the editor will feel sluggish with complex scenes.
- **Synchronous file operations.** Saving a scene on the main thread blocks all UI. Auto-save every 60 seconds (Section 10.3) must not cause a hitch.
- **Inspector refresh polling.** If the inspector calls `refresh()` on all component inspectors every frame (even when nothing changed), this wastes CPU.

**Required mitigations:**

- **Event-driven hierarchy updates.** The hierarchy panel must not rebuild from the ECS every frame. Instead, it rebuilds only when:
  - An entity is created or destroyed (listen for ECS events or use a dirty flag on `EditorState`).
  - An entity is reparented.
  - The search filter text changes.
  - The panel is first shown.
  Between these events, the native `NSOutlineView` / `TreeView` data remains static.

- **Dirty-flag rendering.** The viewport must track whether the scene is "dirty" (a component changed, the camera moved, a gizmo is being dragged). If nothing changed since the last frame, skip the full render pass and present the previous frame's content. This reduces GPU load to zero when the user is editing inspector fields without moving the camera.

- **Debounced inspector updates.** When the user is actively typing in a text field, do not commit to the ECS on every keystroke. Instead:
  - On keystroke: update a local "pending value" and re-render the viewport with a preview (write to a temporary transform, not the ECS).
  - On focus loss or Enter key: commit the final value to the ECS and create the undo command.
  - Exception: slider drags commit per-frame (they already batch into a single undo command on mouse-up).

- **Async file I/O.** All file operations (save, auto-save, scene load, asset import) must run on a background thread. The main thread receives a completion callback. During save, the status bar shows "Saving..." but the UI remains fully interactive.

- **Targets:**
  - Any UI event handler (click, keystroke, menu action): **<16ms** (one frame at 60fps).
  - Viewport response to gizmo drag: **<1 frame** of latency (the gizmo position updates in the same frame as the mouse input).
  - Hierarchy panel search on 10K entities: **<50ms** to filter and update the tree.
  - Scene save (10K entities): **<500ms** on a background thread, zero main-thread hitch.

---

### 18.4 Asset Loading Performance

**Pipeline analyzed (from existing engine patterns):**

1. `StdFileSystem` reads raw bytes from disk.
2. `GltfParser` parses JSON/binary glTF.
3. `AssetManager` orchestrates loading, calls `RenderResources` to upload meshes and textures.
4. Texture data is decoded (PNG/JPG decompression) and uploaded via `bgfx::createTexture2D`.
5. Mesh data is uploaded via `bgfx::createVertexBuffer` / `bgfx::createIndexBuffer`.

**Risks:**

- **Main-thread blocking.** If `AssetManager::load()` is synchronous (as the current `StdFileSystem` implies), loading a scene with 50 unique meshes and 200 textures freezes the editor for seconds.
- **Large texture decompression.** A 4096x4096 PNG takes 50-100ms to decompress on CPU. A scene with 20 such textures means 1-2 seconds of CPU time just for decompression.
- **Many small file reads.** A glTF scene with external `.bin` + separate texture files can trigger hundreds of small disk reads, which is slow on spinning disks and suboptimal even on SSDs due to syscall overhead.
- **Duplicate asset loading.** If two entities reference the same mesh file, it must not be loaded twice.

**Required mitigations:**

- **Async loading is mandatory.** The existing `AssetManager` pattern must be extended (or already supports) async loading. All scene loads must:
  1. Parse the scene file on a background thread.
  2. Enqueue asset load requests.
  3. Process asset loads on a thread pool (one thread per CPU core, minus one for the main thread).
  4. Upload GPU resources on the main thread (bgfx requirement) but only the `bgfx::createTexture` / `createVertexBuffer` calls, which are fast submission calls.
  5. Entities appear in the viewport progressively as their assets finish loading.

- **Memory-mapped file I/O.** For large binary assets (baked `.mesh.bin`, `.ktx2` textures from Phase 14), use `mmap` (macOS) / `MapViewOfFile` (Windows) instead of `fread`. This avoids copying data into userspace and lets the OS manage page faults efficiently.

- **Texture streaming.** Large textures should load a low-resolution mip first (fast, small) and stream in higher mips on demand. This requires the baked `.ktx2` format (Phase 14) which stores mip levels independently. For the initial phases (pre-baking), accept full texture loads but do them asynchronously.

- **Asset deduplication.** `RenderResources` must use path-based caching for meshes and textures. If a mesh at path `assets/cube.glb` is already loaded, return the existing handle. This is likely already implemented but must be verified and enforced.

- **Targets:**
  - Opening a scene with 100 entities and 20 unique assets: **<500ms** to first frame, **<2 seconds** to fully loaded.
  - Opening a scene with 1,000 entities and 100 unique assets: **<1 second** to first frame (with placeholder materials), **<5 seconds** to fully loaded.
  - Opening a scene with 10,000 entities and 500 unique assets: **<3 seconds** to first frame, **<15 seconds** to fully loaded.
  - Hot-reloading a single texture (file watcher trigger): **<200ms** from file change to viewport update.

---

### 18.5 Memory Usage

**Memory sources analyzed:**

1. **ECS storage.** `Registry` uses sparse sets. Each component type has a dense array. `TransformComponent` is 64 bytes; with 10K entities that is 640KB. Total ECS for a 10K-entity scene with typical components: ~5-10MB.
2. **Render resources.** Mesh vertex/index buffers live on the GPU (managed by bgfx). CPU-side mesh data can be freed after upload unless needed for CPU raycasting. Textures: a 2048x2048 RGBA8 texture is 16MB on GPU. A scene with 50 such textures uses 800MB of VRAM.
3. **Asset cache.** `RenderResources` holds CPU-side copies of materials, mesh metadata, texture metadata. Typically small (<1MB).
4. **Undo stack.** Each `ICommand` captures before/after state. A `SetTransformCommand` is ~192 bytes (two transforms + entity ID + vtable). A `DeleteEntityCommand` captures the full component set of the deleted entity plus all children -- potentially kilobytes per command.
5. **UI state.** `EditorState` is small (<1KB). Native UI objects (NSOutlineView data, NSTextField instances) are managed by the platform and typically modest.
6. **Frame arena.** The existing `FrameArena` (default 2MB, configurable via `EngineDesc::frameArenaSize`) resets every frame. This is already well-designed.
7. **Play mode snapshot.** `EditorState::playModeSnapshot` serializes the entire scene to a JSON string in memory. A 10K-entity scene could be 5-20MB of JSON.
8. **Log entries.** `EditorLog::entries()` stores a `std::vector<LogEntry>` that grows unboundedly.

**Risks:**

- **Unbounded undo history.** The `CommandStack` has a configurable `maxDepth_` (default 100), which is good. However, `DeleteEntityCommand` for a subtree of 500 entities could capture megabytes of component data. 100 such commands = hundreds of MB.
- **Texture cache growth.** If the editor loads scenes A, B, and C in sequence without unloading, all textures remain in GPU memory.
- **Leaked asset references.** If a `MeshComponent` references a mesh handle that was destroyed (e.g., after a scene close), the handle is a dangling reference and the memory may not be reclaimed.
- **Play mode snapshot.** Entering and exiting play mode repeatedly without clearing `playModeSnapshot` wastes memory (though it is overwritten each time, so only one copy exists).
- **Log entry accumulation.** A long editing session with verbose logging could accumulate thousands of log entries, each with a heap-allocated `std::string`.

**Required mitigations:**

- **Undo stack size limit.** Keep the default at 100 commands. Additionally, impose a **total memory budget** of 50MB for the undo stack. When a new command would exceed this budget, evict the oldest commands. Each `ICommand` must implement `size_t estimatedMemoryUsage() const` to enable this tracking.

- **LRU asset cache with eviction.** `RenderResources` must track which meshes/textures are currently referenced by entities in the active scene. Unreferenced assets are candidates for eviction. Implement an LRU policy: when total texture memory exceeds a configurable limit (default 1GB VRAM), evict the least-recently-used unreferenced textures.

- **Scene close cleanup.** When the editor opens a new scene (File > Open, File > New), all assets from the previous scene must be explicitly released. This means:
  1. Destroy all bgfx texture/buffer handles for assets not shared with the new scene.
  2. Clear the undo stack (it references entities/components from the old scene).
  3. Clear the play mode snapshot.

- **Log ring buffer.** `EditorLog` must use a ring buffer capped at 10,000 entries (configurable). Oldest entries are evicted when the cap is reached. Use `std::deque<LogEntry>` or a fixed-size circular buffer rather than an unbounded `std::vector`.

- **Frame arena discipline.** All per-frame temporary allocations in editor code must use `engine::memory::FrameArena` (via `Engine::frameArena().resource()`). This includes:
  - Temporary entity lists for hierarchy traversal
  - Gizmo intersection test scratch data
  - String formatting for status bar updates
  - The arena must be reset exactly once per frame in `endFrame()` (already the case in `Engine::endFrame()`).

- **Targets:**
  - Empty editor (no scene loaded): **<200MB** RSS (including the bgfx runtime, native UI framework overhead, and the editor binary itself).
  - Editor with a 1K-entity scene: **<400MB** RSS.
  - Editor with a 10K-entity scene and 100 unique textures (2048x2048): **<500MB** RSS (CPU side) + GPU memory for textures.
  - All arena allocators must reset per frame. No arena memory may persist across frames.
  - No `std::shared_ptr` cycles. If `shared_ptr` is used at all (discouraged), the dependency graph must be acyclic and documented.

---

### 18.6 Memory Leak Prevention

**RAII is mandatory.** All resource ownership in editor code must follow RAII. Specific requirements:

- **No raw `new` or `malloc` outside of arena allocators and pool allocators.** All heap allocations must go through `std::unique_ptr`, `std::make_unique`, `PoolAllocator::allocate()`, or `FrameArena::resource()`. Raw `new` is permitted only inside `PoolAllocator` and `FrameArena` internals (which already use placement new correctly).

- **bgfx resource lifecycle.** Every `bgfx::createTexture*`, `bgfx::createVertexBuffer`, `bgfx::createIndexBuffer`, `bgfx::createFrameBuffer`, and `bgfx::createProgram` call must have a corresponding `bgfx::destroy*` call. Use RAII wrappers:
  ```cpp
  // Suggested pattern (not mandating a specific wrapper, but the guarantee must hold)
  struct FrameBufferGuard
  {
      bgfx::FrameBufferHandle handle = BGFX_INVALID_HANDLE;
      ~FrameBufferGuard() { if (bgfx::isValid(handle)) bgfx::destroy(handle); }
  };
  ```

- **Platform-specific requirements:**

  - **macOS (Cocoa/Objective-C):** Each `.mm` file that creates Objective-C objects must use `@autoreleasepool` blocks. Specifically:
    - The editor's main run loop iteration must be wrapped in `@autoreleasepool { ... }`.
    - Any Cocoa object created in a background thread must have its own `@autoreleasepool`.
    - ARC (Automatic Reference Counting) must be enabled for all `.mm` files (`-fobjc-arc` compiler flag). Do not use manual `retain`/`release`.
    - Delegate and data source references from Cocoa views (e.g., `NSOutlineView.dataSource`) must be set to `nil` in `destroyNativeView()` to break retain cycles.

  - **Windows (Win32/COM):** All COM interface pointers must use `ComPtr<T>` (from `<wrl/client.h>`). Raw `IUnknown*` pointers that are not wrapped in `ComPtr` are prohibited. All `HWND` resources must be destroyed via `DestroyWindow` in the panel's destructor or `destroyNativeView()`. GDI objects (`HBRUSH`, `HFONT`, `HBITMAP`) must be wrapped in RAII guards.

- **Undo system leak prevention:**
  - `ICommand` subclasses must not hold strong references (owning pointers, `shared_ptr`) to entities that may have been destroyed. Use `EntityID` values, which can be validated against the `Registry` before use.
  - `DeleteEntityCommand` captures component data by value, not by pointer. When the command is evicted from the undo stack, the captured data is destroyed with the command object.
  - `CreateEntityCommand::undo()` must destroy the entity and release its components cleanly. `CreateEntityCommand::redo()` must re-create from captured data without leaking the previous entity.

- **Asset system leak prevention:**
  - Asset cache entries in `RenderResources` must use reference counting or weak references. When an asset's reference count drops to zero (no entities reference it and no inspector is displaying it), it becomes eligible for eviction.
  - On scene close, iterate all asset handles and destroy those with zero references. Log a warning if any handles remain referenced after the scene's `Registry` is destroyed (indicates a leak).

- **Plugin system leak prevention:**
  - On plugin unload (`dlclose` / `FreeLibrary`), verify:
    1. All panels registered by the plugin have been removed from the window.
    2. All menu items registered by the plugin have been removed.
    3. All inspector registrations from the plugin have been removed.
    4. No callbacks registered by the plugin remain in any event handler or delegate.
  - If any of these checks fail, log an error and force-remove the dangling registrations before unloading the library.

- **Testing and CI integration:**
  - **AddressSanitizer (ASAN) + LeakSanitizer (LSAN) must be enabled in CI.** Add a CMake build configuration:
    ```cmake
    if(SAMA_SANITIZE)
        target_compile_options(engine_editor PRIVATE -fsanitize=address,leak)
        target_link_options(engine_editor PRIVATE -fsanitize=address,leak)
    endif()
    ```
  - The CI pipeline must run the editor in headless mode (or with a short-lived test harness) under ASAN/LSAN and fail the build if any leaks are detected.
  - **Periodic Instruments runs (macOS).** At least once per phase milestone, run the editor under Instruments' "Leaks" and "Allocations" instruments to catch leaks that sanitizers miss (e.g., Objective-C retain cycles).
  - **Periodic Valgrind runs (Linux, when supported).** Memcheck for the cross-platform editor core.

---

### 18.7 Implementation Guidelines

The following checklist applies to ALL editor code. Every pull request must verify compliance.

**Header hygiene:**
- [ ] Forward-declare everything possible in `.h` files. Include full definitions only in `.cpp` / `.mm`.
- [ ] No platform headers (`AppKit`, `windows.h`, `commctrl.h`) in any `.h` file.
- [ ] Pimpl all platform types. The only data member in a platform panel header is `std::unique_ptr<Impl>`.
- [ ] `IEditorPanel`, `IEditorWindow`, `IComponentInspector` headers include only `<cstdint>` and forward declarations.

**Memory allocators (use the engine's existing infrastructure):**
- [ ] Use `FrameArena` (`engine::memory::FrameArena`) for all per-frame temporaries: scratch entity lists, formatted strings, intersection results.
- [ ] Use `InlinedVector<T, N>` (`engine::memory::InlinedVector`) for small, bounded collections: panel lists (`InlinedVector<IEditorPanel*, 8>`), selection sets (`InlinedVector<EntityID, 16>`), gizmo axis hit results (`InlinedVector<float, 3>`).
- [ ] Use `PoolAllocator<T, MaxCount>` (`engine::memory::PoolAllocator`) for undo commands. Size the pool to `maxDepth_` (default 100). When the pool is full and a new command arrives, destroy the oldest command and recycle its slot.
- [ ] No `std::shared_ptr` unless the ownership graph is documented and proven acyclic. Prefer `std::unique_ptr`.

**I/O:**
- [ ] Async by default for all file I/O (scene save/load, asset import, auto-save).
- [ ] Never call `fread`, `fwrite`, or `std::ifstream::read` on the main thread for files larger than 4KB.
- [ ] Use `mmap` / `MapViewOfFile` for baked binary assets.

**Rendering:**
- [ ] Dirty-flag the viewport. Do not re-render if nothing changed.
- [ ] Gizmo and grid rendering must complete in <2ms total.
- [ ] Selection highlight (stencil outline) must not double the draw call count; use a single stencil pass, not per-entity passes.

**Profiling:**
- [ ] Profile before optimizing, but design for performance from the start.
- [ ] Every system that runs per-frame must be measurable via `std::chrono::high_resolution_clock` wrappers (Section 10, Resource Usage Inspector).
- [ ] Add `SAMA_PROFILE_SCOPE("name")` markers (to be implemented) at the top of every per-frame function for future integration with Tracy, Superluminal, or Instruments.

**Testing:**
- [ ] ASAN/LSAN enabled in CI for every editor build.
- [ ] Memory targets (Section 18.5) are tested via an automated benchmark that loads a reference scene and checks RSS.
- [ ] Startup time target (Section 18.2) is tested via an automated benchmark that measures time from process launch to first frame.

This section is a binding contract. Deviations require explicit justification documented in the pull request description and approved before merge.