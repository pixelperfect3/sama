# Sama Engine

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
| `ik_demo` | Inverse kinematics with foot placement on uneven terrain |
| `ik_hand_demo` | Interactive mouse-driven arm IK on a T-pose model |

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
# All unit tests (~437 cases)
build/engine_tests

# Screenshot tests (12 cases)
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
build/engine_tests "[ik]"
```

## Using Sama in Your Game

Sama provides four umbrella CMake targets so you can link only the subsystems you need:

| Target | Includes | Good for |
|--------|----------|----------|
| `sama_minimal` | renderer + scene + ECS + game loop + memory | simple 3D scenes, UI apps, prototypes |
| `sama_3d` | minimal + physics + audio + animation/IK + asset loading | most 3D games (recommended default) |
| `sama_2d` | minimal + JSON I/O (sprite rendering is in the renderer) | 2D games, UI tools |
| `sama` | everything (includes input, IO, threading) | full-featured games |

### Quick start (game in the Sama repo)

Create your game under `apps/`:

```
sama/
└── apps/
    └── my_game/
        ├── CMakeLists.txt
        ├── main.mm
        ├── MyGame.h
        └── MyGame.cpp
```

**`apps/my_game/CMakeLists.txt`:**

```cmake
add_executable(my_game main.mm MyGame.cpp)

# Pick ONE umbrella target
target_link_libraries(my_game PRIVATE sama_3d)

# macOS frameworks (required by bgfx Metal + window)
if(APPLE)
    target_link_libraries(my_game PRIVATE
        "-framework Cocoa"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()
```

Add to the top-level `CMakeLists.txt`:
```cmake
add_subdirectory(apps/my_game)
```

Build only your game:
```bash
cmake --build build --target my_game -j$(sysctl -n hw.ncpu)
```

### Minimal game code

```cpp
// apps/my_game/main.mm
#include "engine/game/GameRunner.h"
#include "MyGame.h"

int main() {
    MyGame game;
    engine::game::GameRunner runner(game);
    return runner.run("project.json");  // or runner.run() for defaults
}
```

See `docs/AGENTS.md` Section 4 for the complete minimal game template (entity setup, lighting, camera).

### Standalone project (Sama as a dependency)

```cmake
# your_game/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(MyGame CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
    sama
    GIT_REPOSITORY https://github.com/pixelperfect3/sama.git
    GIT_TAG        main  # pin to a specific commit in production
)
FetchContent_MakeAvailable(sama)

add_executable(my_game main.mm MyGame.cpp)
target_link_libraries(my_game PRIVATE sama_3d)

if(APPLE)
    target_link_libraries(my_game PRIVATE
        "-framework Cocoa" "-framework Metal" "-framework QuartzCore"
        "-framework IOKit" "-framework CoreFoundation"
    )
endif()
```

Sama auto-detects when it's consumed as a subdirectory and disables demos/tests/editor by default, so only the engine libraries get built. First configure downloads bgfx/Jolt/SoLoud/etc. (~5-10 min); subsequent builds are fast.

**Force-enable options if you want them:**
```cmake
set(SAMA_BUILD_DEMOS ON CACHE BOOL "" FORCE)
set(SAMA_BUILD_TESTS ON CACHE BOOL "" FORCE)
set(SAMA_BUILD_EDITOR ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sama)
```

### Manually picking libraries (advanced)

If you want finer control than the umbrella targets, link individual libraries:

```cmake
target_link_libraries(my_game PRIVATE
    engine_core engine_game engine_rendering engine_scene engine_ecs engine_memory
    # omit engine_physics, engine_audio, etc. if not needed
)
```

All individual libraries are listed in `CMakeLists.txt` — look for `add_library(engine_*)` entries.

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

## Acknowledgments

Sama Engine is built on the following open-source libraries. Thank you to all the authors and contributors who make their work freely available.

| Library | Purpose | License | Version |
|---------|---------|---------|---------|
| [bgfx](https://github.com/bkaradzic/bgfx) | Cross-platform rendering abstraction (Metal, Vulkan) | BSD 2-Clause | via [bgfx.cmake](https://github.com/widberg/bgfx.cmake) |
| [bimg](https://github.com/bkaradzic/bimg) | Image loading and texture processing (bundled with bgfx) | BSD 2-Clause | — |
| [bx](https://github.com/bkaradzic/bx) | Base library for bgfx (allocators, math, platform) | BSD 2-Clause | — |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | Rigid body physics simulation | MIT | v5.2.0 |
| [SoLoud](https://github.com/jarikomppa/soloud) | Audio engine (mixing, filters, 3D spatialization) | zlib/libpng | 20200207 |
| [miniaudio](https://github.com/mackron/miniaudio) | Platform audio backend (CoreAudio, WASAPI, AAudio) | MIT-0 | v0.11.25 |
| [Dear ImGui](https://github.com/ocornut/imgui) | Debug UI overlays in demo apps (bundled with bgfx) | MIT | — |
| [rapidjson](https://github.com/Tencent/rapidjson) | JSON parsing and writing | MIT | v1.1.0 |
| [cgltf](https://github.com/jkuhlmann/cgltf) | glTF/GLB asset parsing (single-header) | MIT | v1.14 |
| [GLM](https://github.com/g-truc/glm) | Math library (vectors, matrices, quaternions) | MIT | v1.0.1 |
| [GLFW](https://github.com/glfw/glfw) | Windowing, input, and context creation | zlib/libpng | v3.3.9 |
| [stb](https://github.com/nothings/stb) | Image loading/writing (stb_image, stb_image_write) | MIT / Public Domain | latest |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | OBJ mesh file parsing | MIT | vendored |
| [ankerl::unordered_dense](https://github.com/martinus/unordered_dense) | Fast flat open-addressing hash map | MIT | vendored |
| [Catch2](https://github.com/catchorg/Catch2) | Unit and screenshot testing framework | BSL-1.0 | v3.4.0 |

## Code Style

- C++20, Allman braces, 4-space indent, 100-character line limit
- `UpperCamelCase` classes, `camelCase` functions/variables, `ALL_CAPS` constants, `trailing_` private members
- Namespaces: `engine::subsystem` (e.g., `engine::scene`, `engine::rendering`)
- Run `clang-format -i` on all modified C++ files before committing

See [CLAUDE.md](CLAUDE.md) for the full contributor guide.

## License

This is a personal project. License TBD.
