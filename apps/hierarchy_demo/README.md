# Hierarchy Demo

Interactive demo of the engine's ECS-native scene graph. Nine cubes are arranged in a 3-level parent-child hierarchy. Dragging any cube moves its entire subtree.

## Tree Structure

```
Root (white, center)
├── Child 1 (orange, left)
│   ├── Node 3 (cyan)
│   ├── Node 4 (cyan)
│   └── Node 5 (cyan)
└── Child 2 (orange, right)
    ├── Node 6 (cyan)
    ├── Node 7 (cyan)
    └── Node 8 (cyan)
```

## Controls

| Input | Action |
|-------|--------|
| Left click | Select a cube (highlighted yellow) |
| Left drag | Move selected cube — children follow via scene graph |
| Right drag | Orbit camera around the scene center |
| Scroll | Zoom in / out |

## ImGui Panel

The left-side panel shows the live hierarchy tree. Click a node to select it. The panel displays local and world positions for the selected cube.

## How It Works

- Each cube is an ECS entity with `TransformComponent` (local TRS) and hierarchy components (`HierarchyComponent` / `ChildrenComponent`).
- `TransformSystem::update()` walks the tree top-down each frame, composing `world = parent_world * local_TRS` into `WorldTransformComponent`.
- Dragging converts the screen-space mouse delta into a world-space delta on a camera-facing plane, then transforms it into the parent's local space before applying to the selected entity's `TransformComponent.position`.
- The standard PBR pipeline renders all cubes with directional lighting and shadows.

## Building

```bash
cmake --build build --target hierarchy_demo
./build/hierarchy_demo
```
