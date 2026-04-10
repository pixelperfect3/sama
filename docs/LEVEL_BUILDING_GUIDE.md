# Building a level in the Sama editor

This is a hands-on tutorial for using `sama_editor` to construct a playable
game level from scratch — placing geometry, lighting it, wiring up physics,
and running it. It does not assume any prior engine knowledge. By the end
you'll have a level you can press Space on and watch react.

If you already know your way around Unity or Godot, skim sections 1-2 then
jump to the worked example in section 12.

---

## 1. Build and launch

```sh
# From the engine repo root:
cmake --build build --target sama_editor -j$(sysctl -n hw.ncpu)
./build/sama_editor
```

The editor only ships on macOS today (uses native AppKit panels). The
Metal viewport renders into the center pane via bgfx.

When the window comes up you'll see:

```
+--------------------------------------------------------------+
|  File  Edit  Entity  Component  View                          |  ← native menu bar
+----------+----------------------------------+----------------+
|          |                                  |                |
| Hier-    |                                  |   Properties   |
| archy    |        3D viewport               |                |
|          |        (Metal, bgfx)             |  (selected     |
|          |                                  |   entity's     |
|          |                                  |   components)  |
+----------+----------------------------------+----------------+
| [ Console ] [ Resources ]                                    |
+--------------------------------------------------------------+
```

A starter scene with a red cube on a gray ground plane is loaded
automatically so the viewport is never empty.

---

## 2. Camera navigation

Hover the mouse over the viewport, then:

| Input | Action |
|---|---|
| **Right-click drag** | Orbit the camera around its target point |
| **Scroll wheel** | Zoom in/out |
| **W / A / S / D** | Slide the camera target forward/left/back/right (relative to camera yaw) |

The camera can't go below the ground plane — it's a standard orbit camera,
not a free-fly. If you lose your scene, scroll out and orbit until you
find it again.

---

## 3. Selecting and inspecting entities

Three ways to select an entity:

1. **Click it in the viewport** — left-click on any mesh; the editor
   ray-casts against bounding boxes and picks the closest hit.
2. **Click its name in the Hierarchy panel** (left side).
3. **Cycle through with the keyboard** (selection moves with arrow
   keys when the viewport has focus).

Once selected, the right-side **Properties panel** shows the entity's
components: Transform, Mesh, Material, Light, RigidBody, Collider, etc.
Each shows up as a labeled section with editable fields.

The transform **gizmo** appears at the entity's origin in the viewport
whenever something is selected. By default it's in Translate mode (3
arrows). Press:

- **W** — Translate (arrows)
- **E** — Rotate (rings)
- **R** — Scale (boxes at axis ends)

Drag a gizmo handle to manipulate the entity. The Properties panel
updates in real time as you drag.

---

## 4. Creating empty entities and primitives

The native menu bar's **Entity** menu has:

- **Create Empty** — entity with just a Transform. Useful as a parent
  node for grouping other entities.
- **Create Cube** — entity with Transform + Mesh + Material + Visible.
  Spawns a unit cube at the world origin with a default gray material.
- **Create Light** — entity with a default `DirectionalLightComponent`.

You can also right-click in the Hierarchy panel for the same options
plus "Create Child Entity" (which parents the new entity to the
selected one).

---

## 5. Importing 3D models

The editor supports glTF (`.glb`/`.gltf`) and Wavefront OBJ (`.obj`).

1. **File → Import Asset...** (or `Cmd+I`)
2. Pick a `.glb` from the file dialog.
3. The asset spawns at the world origin with all its meshes, materials,
   and (for glTF) its skeleton hierarchy intact.
4. Move/rotate/scale it with the gizmo as needed.

The import is **synchronous** for now — the editor blocks until the
asset is fully uploaded. Large scenes (>50 MB) will pause the editor for
a second or two; this is a known limitation tracked in
`docs/NOTES.md` → Editor → TODO.

---

## 6. Editing component values in the Properties panel

The Properties panel groups fields by component. For the default cube
you'll see:

```
Red Cube (id:1)
[ Transform ]
   Pos X: 0.000   Pos Y: 0.000   Pos Z: 0.000
   Rot X: 0.000   Rot Y: 0.000   Rot Z: 0.000
   Scale X: 1.000 Scale Y: 1.000 Scale Z: 1.000
[ Material ]
   Albedo:    [color well]    (R,G,B)
   Rough:     [slider 0-1]
   Metal:     [slider 0-1]
   Emiss:     [float field]
[ + Add Component ]
```

Click any numeric field, type a new value, hit **Enter** or click away
to commit. The viewport updates instantly. The color well opens the
native macOS color picker.

Clicking **+ Add Component** at the bottom of the panel opens a popup
menu with all addable component types — see section 9 for the physics
ones.

---

## 7. Managing the hierarchy

The left **Hierarchy panel** shows entities as an indented tree.
Component tags appear after the name: `[T]` Transform, `[M]` Mesh,
`[L]` Light, `[P]` Physics body.

Operations:

- **Click name** — select.
- **Click name twice (slowly)** — rename inline. Type, then Enter to
  commit, Esc to cancel. Pressing Delete or Backspace while editing
  edits the text, not the entity.
- **Drag onto another entity** — reparent. The dragged entity becomes
  a child of the drop target.
- **Right-click** — context menu with "Create Child Entity",
  "Detach from Parent", "Delete".
- **Delete key** (when an entity is selected and the hierarchy panel is
  the focused responder) — delete the selected entity. Children are
  destroyed too.

Reparenting preserves the world transform: dragging a child onto a
new parent recomputes the local transform so the entity doesn't visibly
move.

---

## 8. Lighting the scene

A scene needs at least one light to render anything not pure black.
The default scene has one directional light pointing down-and-forward.

To add another:

1. **Entity → Create Light** — spawns a new entity with a
   `DirectionalLightComponent`.
2. Or select an existing entity → **+ Add Component → Directional Light**
   (or Point Light).

Edit in the Properties panel:

- **Directional Light**: `Dir X/Y/Z` (light direction), `Intens`.
  Color is currently fixed to white; the editor's color picker for
  lights is on the TODO list. The directional light shows a small
  cylinder + arrows gizmo in the viewport so you can see where it's
  pointing.
- **Point Light**: `Intens`, `Radius` (falloff distance).

You can rotate a directional light using the entity's transform Rot
fields — the light's `direction` is automatically derived from the
parent entity's rotation, so the gizmo lets you "aim" the light.

---

## 9. Physics: making objects fall and collide

This is the most powerful feature for level building. Physics is
**only stepped during Play mode** (Space key) — in Editing mode
everything is static so you can position things by hand.

### 9.1 Add a static ground

The default scene's ground is a thin slab (`scale = {20, 0.01, 20}`).
Make it physical:

1. Select **Ground** in the Hierarchy.
2. Click **+ Add Component** at the bottom of the Properties panel.
3. Choose **Rigid Body**.
4. In the new "Rigid Body" section that appears, set the **Type**
   dropdown to **Static**. (Static = never moves but blocks dynamic
   bodies. Dynamic = subject to gravity and forces. Kinematic = moved
   by code.)
5. Click **+ Add Component** again → **Box Collider**.
6. The collider auto-fits to the entity's transform scale, so a Ground
   at scale `{20, 0.01, 20}` automatically gets `halfExtents = {10,
   0.005, 10}` — exactly matching the visual slab. No manual editing
   needed.

### 9.2 Add a dynamic cube

1. Select the **Red Cube** (or spawn a new one with **Entity → Create
   Cube**).
2. Move it up a few units (Pos Y = 5) so it has somewhere to fall from.
3. **+ Add Component → Rigid Body** — leave Type as **Dynamic**.
4. **+ Add Component → Box Collider** — auto-fits to the unit cube's
   `halfExtents = {0.5, 0.5, 0.5}`.

That's it. You don't need to set mass / friction / restitution manually
unless you want non-default physical behavior; the defaults (mass 1,
friction 0.5, restitution 0.3) are reasonable starting values for
arcade-feel physics.

### 9.3 Press Space to play

- **Space** = enter Play mode.
- The editor takes a snapshot of every entity's transform, then starts
  stepping `JoltPhysicsEngine` at 60 Hz.
- The cube falls and lands on the ground. The "PLAYING" indicator
  appears in the HUD.
- **Space** again = pause (simulation stops, transforms freeze).
- **Escape** = stop. The transform snapshot is restored and all Jolt
  bodies are destroyed. The next time you press Space, bodies are
  re-created from the authored components — you start from a clean
  state every Play.

Important: while in Play mode, **the gizmo and Properties panel writes
are gated off**. You can't drag objects or type new transform values
during simulation, because that would fight `PhysicsSystem` and
produce confusing results. Press Esc first to edit, then Space again
to re-test.

### 9.4 Authoring physics shapes other than boxes

The Collider's **Shape** dropdown supports Box, Sphere, Capsule, and
Mesh. Sphere and Capsule use the `Radius` field; Box uses
`HalfX/HalfY/HalfZ`. Mesh colliders are not yet implemented and
silently fall back to a Box.

For complex shapes, the convention is:

- **Visual mesh**: detailed (your imported glTF / OBJ).
- **Collider**: an approximation — usually one Box or Sphere covering
  the visual silhouette, or a few Boxes for a compound shape (compound
  shapes need multiple entities parented under one rigid body, and
  this is only partially supported in v1).

Don't try to use a 50,000-triangle mesh as a collider. The physics
engine will refuse it.

---

## 10. Materials

Spawning a primitive auto-creates a default gray material. To author it:

1. Select the entity.
2. Find the **Material** section in the Properties panel.
3. Click the **Albedo** color well to open the macOS color picker —
   pick any color, the viewport updates as you drag.
4. Drag the **Rough** slider for surface roughness (0 = mirror, 1 =
   matte chalk).
5. Drag the **Metal** slider for metallic-ness (0 = dielectric like
   plastic/wood, 1 = metal like steel/gold).
6. Type into the **Emiss** field for emissive intensity (anything > 0
   makes the material glow regardless of lighting).

For textured materials (albedo map, normal map, roughness map, etc.),
the path is **import a glTF** that bakes the textures in — the editor
doesn't yet have a per-channel texture picker UI. That's tracked in
`docs/NOTES.md` → Editor → TODO ("proper material editor").

---

## 11. Saving and loading scenes

The editor uses a JSON scene format. Every component type registered
with `SceneSerializer` round-trips losslessly: Transform,
WorldTransform, Camera, lights, Mesh, Material, Visible,
ShadowVisible, RigidBody, Collider, Name, Hierarchy.

| Action | Shortcut | Menu |
|---|---|---|
| **New scene** | `Cmd+N` | File → New Scene |
| **Open scene** | `Cmd+O` | File → Open Scene... |
| **Save scene** | `Cmd+S` | File → Save Scene |
| **Save as** | `Cmd+Shift+S` | File → Save Scene As... |

`Cmd+S` on a never-saved scene falls through to Save As. Files have
no fixed extension; convention is `.json`. Mesh assets and textures
are referenced by path, not embedded — keep them in `assets/` so the
serializer can find them on load.

---

## 12. Worked example: a falling cube on a static plane

Let's build the canonical "drop a cube on the floor" scene from
nothing in under two minutes.

1. **`Cmd+N`** — start a new scene. The viewport goes empty.
2. **Entity → Create Empty**, then in the Properties panel:
   - Rename: click the name "Entity #N" in the Hierarchy panel twice,
     type `Floor`, Enter.
   - Set Pos: Y = 0
   - Set Scale: X = 20, Y = 0.01, Z = 20
3. **+ Add Component → Mesh (cube)** — gives the floor a visible cube
   mesh stretched into a thin slab.
4. **+ Add Component → Rigid Body** — set the Type dropdown to **Static**.
5. **+ Add Component → Box Collider** — auto-fits to scale.
6. **Entity → Create Cube** — spawns a unit cube at the origin.
   - Rename to `Player` (or whatever).
   - Set Pos Y = 8 (well above the floor).
   - Edit the Material color to red so it stands out.
7. **+ Add Component → Rigid Body** (default Dynamic).
8. **+ Add Component → Box Collider** (default 0.5 half-extents).
9. **`Cmd+S`** → save as `falling_cube.json` in your project's `assets/`.
10. **Space** — the cube falls onto the floor.
11. **Esc** — restore.

You can now iterate: drag the player higher with the gizmo, give it
some initial Y rotation so it lands at an angle, add more cubes on top
to make a stack. Press Space repeatedly to test each variant.

---

## 13. Light a scene properly

The default lighting is one directional light at full intensity. For
a more cinematic look:

1. **Entity → Create Light** to add a second directional light.
2. Set its Pos rotation to come from a different angle (e.g. Rot Y = 90).
3. Lower its `Intens` to 0.4 — this is your fill light.
4. The original light at intensity 1.0 is your key light.
5. (Optional) Add a Point Light entity and place it where a lamp or
   torch would go. Set `Radius` to maybe 5 and `Intens` to 2.0.

The PBR pipeline does shadow mapping for the first directional light
automatically — no setup needed.

---

## 14. Undo / redo

| Shortcut | Action |
|---|---|
| `Cmd+Z` | Undo |
| `Cmd+Shift+Z` | Redo |

The undo stack records: entity creation, deletion, transform changes
from the gizmo (committed at end of drag), and reparenting. It does
NOT yet record: material edits, light edits, component additions, or
property panel field commits — those are immediate and not undoable.
That's a known gap.

---

## 15. Keyboard shortcut reference

| Key | Action |
|---|---|
| **Space** | Play / pause |
| **Esc** | Stop (when playing/paused) |
| **W / E / R** | Gizmo mode: Translate / Rotate / Scale |
| **A** | Toggle in-viewport "Add Component" overlay menu |
| **F** *(in `apps/ui_test`)* | Cycle font backend |
| **Cmd+N** | New scene |
| **Cmd+O** | Open scene |
| **Cmd+S** | Save scene |
| **Cmd+Shift+S** | Save scene as |
| **Cmd+I** | Import asset |
| **Cmd+Z** | Undo |
| **Cmd+Shift+Z** | Redo |
| **Delete** / **Backspace** | Delete selected entity (when not editing a text field) |
| **Right-mouse drag** | Orbit camera |
| **Scroll** | Zoom |
| **W/A/S/D** *(viewport hover, no Cmd)* | Pan camera target |

---

## 16. Tips and gotchas

- **Run from the repo root**, not from inside `build/`. The editor
  resolves asset paths via `_NSGetExecutablePath` so it works from
  anywhere, but other engine internals (shader hot-reload, scene
  loading) may still expect cwd-relative paths.
- **A static ground plane MUST be set to Type = Static** in its Rigid
  Body. If you leave it as Dynamic, gravity will pull it through itself
  and the cube will follow it forever into the void.
- **Physics half-extents default to `0.5 * scale`**. If you scale a cube
  up to size 4 *and* manually edit the collider half-extents to 0.5,
  the visible mesh and physics shape won't match. Either set the scale
  and let the collider auto-fit, or scale = 1 and set half-extents
  manually — don't do both.
- **Box Collider half-extents below 0.05 are clamped silently**. The
  underlying Jolt body needs at least one millimeter of thickness to
  generate a valid convex hull. The engine refuses to create the body
  and logs a warning to stderr if any axis is below `1e-4`; values
  between `1e-4` and `0.05` work but the convex radius is shrunk.
- **Play mode resets every time**. Pressing Space → Esc → Space
  destroys all Jolt bodies and re-creates them. You can't "carry over"
  velocities or accumulated state across Play sessions. This is by
  design — see `docs/NOTES.md` → "Physics in Play Mode — Design".
- **The HUD font is JetBrains Mono via MSDF**. If you swap out
  `assets/fonts/JetBrainsMono-msdf.{json,png}` for a different font,
  the editor reads them on init. Falls back to bgfx debug text if the
  files are missing. Same applies to the embedded fallback BitmapFont.
- **The console panel** (bottom tab) shows engine and editor log
  output, ring-buffered to the last 100 entries. Useful when something
  doesn't work — `EditorLog::error` and `EditorLog::warn` calls land
  here.
- **The resources panel** (bottom tab, sibling of Console) shows live
  draw call count, triangle count, FPS, frame time, and frame arena
  usage. Watch this if you're worried about perf.

---

## 17. What's missing today (and where to find it later)

- A material texture picker (texture maps for albedo / normal / roughness
  beyond what glTF imports give you).
- Animation timeline / keyframe editor.
- Visual scripting / behavior graph.
- Lua hot-reload.
- Compound physics colliders.
- Per-entity bounding-box selection outline (only the gizmo is
  highlighted today, not the mesh itself).

All of these are tracked in `docs/NOTES.md` → Editor → TODO with their
trigger conditions ("first time you need a material that isn't a glTF
import", etc.). Nothing here is technically deferred forever; it's a
matter of which feature lands first when a real game needs it.

---

## 18. Where to look in the source

If you're modifying the editor and need to find where a feature lives:

- **Main loop / per-frame update**: `editor/EditorApp.cpp`
- **Hierarchy panel**: `editor/panels/HierarchyPanel.cpp` +
  `editor/platform/cocoa/CocoaHierarchyView.mm`
- **Properties panel**: `editor/panels/PropertiesPanel.cpp` +
  `editor/platform/cocoa/CocoaPropertiesView.mm`
- **Component inspectors**: `editor/inspectors/*.cpp` (one per
  component type)
- **Transform gizmo**: `editor/gizmo/TransformGizmo.cpp` +
  `editor/gizmo/GizmoRenderer.cpp`
- **Native window + menu bar**: `editor/platform/cocoa/CocoaEditorWindow.mm`
- **Scene serialization**: `engine/scene/SceneSerializer.cpp`
- **Physics integration**: `editor/EditorApp.cpp::Impl::resetPhysicsBodies`
  and the `addComponentToSelection` helper

For deeper architecture, see `docs/EDITOR_ARCHITECTURE.md` (the binding
contract for editor code, including the §18.7 implementation hygiene
checklist).
