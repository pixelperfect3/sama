# Nimbus Engine

A modern C++20 3D game engine built with an ECS architecture, PBR rendering, and a modular subsystem design.

## Features

- **PBR Rendering** -- 11-phase pipeline including cascaded shadow maps (CSM), clustered forward lighting, screen-space ambient occlusion (SSAO), bloom, image-based lighting (IBL), sprite rendering, and FXAA post-processing. Built on bgfx for cross-platform GPU abstraction.
- **ECS Architecture** -- Sparse-set component storage with a compile-time DAG scheduler (`constexpr buildSchedule<Systems...>()`) that resolves system dependencies at compile time with zero runtime overhead.
- **Scene Graph** -- Parent-child hierarchy encoded directly in ECS components (`HierarchyComponent`, `ChildrenComponent`). Dirty-flag optimized `TransformSystem` composes local TRS into cached world matrices each frame.
- **Physics** -- Jolt Physics integration behind an `IPhysicsEngine` interface. Supports rigid bodies (static, dynamic, kinematic), collision detection with event callbacks, raycasting (closest hit and all hits), and fixed-timestep simulation.
- **Audio** -- SoLoud with miniaudio backend behind an `IAudioEngine` interface. 3D spatial audio with distance attenuation, 4 sound categories (SFX, Music, UI, Ambient) routed through independent mix buses, and fire-and-forget or component-driven playback.
- **Skeletal Animation** -- GPU skinning via `u_model[]` bone matrix array, glTF skeleton and animation clip extraction, pose sampling with binary search interpolation, and crossfade clip blending.
- **Custom Memory Allocators** -- `FrameArena` (per-frame bump allocator via `std::pmr::monotonic_buffer_resource`), `InlinedVector` (small-buffer optimized vector), and `PoolAllocator` (fixed-size object pool). Zero per-frame heap allocations in the hot path.
- **JSON Serialization** -- rapidjson wrapper (`JsonDocument`, `JsonValue`, `JsonWriter`) with pimpl isolation. Scene save/load, render settings, and input binding persistence.
- **Asset Pipeline** -- glTF/GLB loading via cgltf with multi-primitive mesh merging, tangent generation, skeleton/animation extraction, and async background decoding through `AssetManager`.
- **Engine Core** -- Single-init `Engine` class managing renderer, physics, audio, asset manager, and frame arena. Shared `OrbitCamera` with mouse/keyboard controls.

## Demos

All demos are built as standalone executables and run from the `build/` directory.

| Demo | Description |
|------|-------------|
| `helmet_demo` | PBR-lit damaged helmet model with rotating directional light and IBL ambient |
| `hierarchy_demo` | Interactive scene graph with draggable cubes demonstrating parent-child transforms |
| `physics_demo` | Falling cubes on a tilting plane using Jolt Physics rigid body simulation |
| `audio_demo` | Spatial 3D audio sources with per-category volume sliders using SoLoud |
| `animation_demo` | Skeletal animation playback with blend controls and timeline scrubbing |

## Building

### Prerequisites

- CMake 3.20+
- C++20 compiler (Apple Clang via Xcode is the primary platform)
- macOS with Xcode (primary development platform)

### Build Commands

```bash
# Initial configure (once)
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build everything
cmake --build build -j$(sysctl -n hw.ncpu)

# Build a specific target
cmake --build build --target helmet_demo -j$(sysctl -n hw.ncpu)
```

### Running Demos

Run from the `build/` directory so asset paths resolve correctly:

```bash
cd build
./helmet_demo
./hierarchy_demo
./physics_demo
./audio_demo
./animation_demo
```

## Running Tests

```bash
# All unit tests (~412 cases)
build/engine_tests

# Screenshot tests (11 cases)
build/engine_screenshot_tests

# Regenerate golden reference images
build/engine_screenshot_tests --update-goldens

# Run tests by tag
build/engine_tests "[physics]"
build/engine_tests "[audio]"
build/engine_tests "[animation]"
build/engine_tests "[json]"
build/engine_tests "[serializer]"
build/engine_tests "[memory]"
build/engine_tests "[scenegraph]"
build/engine_tests "[transform]"
build/engine_tests "[config]"
```

## Architecture

The engine is organized into modular static libraries, each owning a specific subsystem. Systems declare their component read/write sets as type aliases, and the compile-time DAG scheduler resolves execution order and parallelism opportunities.

Detailed architecture documents:

- [docs/NOTES.md](docs/NOTES.md) -- Project decisions, threading model, build setup, and development log
- [docs/SCENE_GRAPH_ARCHITECTURE.md](docs/SCENE_GRAPH_ARCHITECTURE.md) -- Hierarchy components, TransformSystem, mutation API
- [docs/PHYSICS_ARCHITECTURE.md](docs/PHYSICS_ARCHITECTURE.md) -- Jolt integration, ECS components, collision callbacks, raycasting
- [docs/AUDIO_ARCHITECTURE.md](docs/AUDIO_ARCHITECTURE.md) -- SoLoud integration, spatial audio, sound categories
- [docs/ANIMATION_ARCHITECTURE.md](docs/ANIMATION_ARCHITECTURE.md) -- Skeleton, clips, GPU skinning, glTF extraction
- [docs/MEMORY_ARCHITECTURE.md](docs/MEMORY_ARCHITECTURE.md) -- FrameArena, InlinedVector, PoolAllocator, allocation budget
- [docs/JSON_ARCHITECTURE.md](docs/JSON_ARCHITECTURE.md) -- rapidjson wrapper, scene serialization, config files
- [docs/EDITOR_ARCHITECTURE.md](docs/EDITOR_ARCHITECTURE.md) -- Editor tooling and debug UI

## Technology Stack

| Library | Purpose | Integration |
|---------|---------|-------------|
| [bgfx](https://github.com/bkaradzic/bgfx) | Cross-platform rendering (Metal, Vulkan) | FetchContent via bgfx.cmake |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | Rigid body physics simulation | FetchContent (v5.2.0) |
| [SoLoud](https://github.com/jarikomppa/soloud) | Audio engine (mixing, 3D spatialization) | Vendored in third_party/soloud |
| [miniaudio](https://github.com/mackron/miniaudio) | Platform audio backend | SoLoud backend |
| [rapidjson](https://github.com/Tencent/rapidjson) | JSON parsing and writing | FetchContent (header-only) |
| [cgltf](https://github.com/jsmn-impl/cgltf) | glTF/GLB asset parsing | FetchContent (header-only) |
| [GLM](https://github.com/g-truc/glm) | Math library (vectors, matrices, quaternions) | FetchContent (v1.0.1) |
| [GLFW](https://github.com/glfw/glfw) | Windowing and input | FetchContent (v3.3.9) |
| [Catch2](https://github.com/catchorg/Catch2) | Testing framework | FetchContent (v3.4.0) |
| [ankerl::unordered_dense](https://github.com/martinus/unordered_dense) | Flat open-addressing hash map | Vendored single header |

## Code Style

- C++20, Allman braces, 4-space indent, 100-character line limit
- `UpperCamelCase` classes, `camelCase` functions/variables, `ALL_CAPS` constants, `trailing_` private members
- Namespaces: `engine::subsystem` (e.g., `engine::scene`, `engine::rendering`)
- Run `clang-format -i` on all modified C++ files before committing

See [CLAUDE.md](CLAUDE.md) for the full contributor guide.

## License

This is a personal project. License TBD.
