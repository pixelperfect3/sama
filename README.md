# Sama Engine

A C++20 game engine with PBR rendering, ECS architecture, and cross-platform mobile support.
Built on bgfx (Metal / Vulkan), Jolt Physics, and SoLoud.

<!-- Suggested screenshot row goes here:
     ![helmet_demo](docs/images/helmet_demo.png)
     ![physics_demo](docs/images/physics_demo.png)
     ![editor](docs/images/editor.png)
     Add 3 PNGs to docs/images/ and uncomment.
-->

## Status

| Platform | Backend | Status |
|----------|---------|--------|
| macOS | Metal | ✅ Primary development platform |
| iOS Simulator | Metal | ✅ PBR + shadows + audio + touch + gyro + tier detection |
| iOS Device | Metal | ⚠️ Builds; not yet tested on hardware (no IPA pipeline) |
| Android | Vulkan | ✅ PBR + shadows + post-processing + ASTC + audio (verified Pixel 9) |
| Linux / Windows | OpenGL / DX | Untested — bgfx supports them; CMake doesn't gate them yet |

The same `IGame` runs on every supported platform with no `#ifdef` in game code.

## Features

- **PBR rendering** — cascaded shadow maps, clustered forward lighting, IBL, SSAO, bloom, FXAA
- **ECS** — sparse-set storage, compile-time DAG scheduler resolves system order at compile time
- **Scene graph** — parent-child hierarchy as ECS components; dirty-flag world matrix updates
- **Physics** — Jolt with rigid bodies, raycasting, fixed-timestep simulation, collision events
- **Audio** — SoLoud with 3D spatial audio, mix categories, miniaudio backend per platform
- **Skeletal animation** — GPU skinning, glTF import, clip blending, IK
- **Asset pipeline** — glTF import, async loading, per-tier ASTC texture compression
- **Editor** — native macOS scene/entity editor (see [Editor](#editor) section below)

## Quick start (macOS)

Sama's primary development platform is macOS. iOS and Android instructions are further below.

**Prerequisites:** macOS with Xcode 15+, CMake 3.20+. C++20 standard library ships with Apple Clang.

```bash
# Configure once
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build everything (engine + demos + tests)
cmake --build build -j$(sysctl -n hw.ncpu)

# Run a demo (must run from build/ so asset paths resolve)
cd build && ./helmet_demo
```

Linux and Windows aren't gated by CMake yet but bgfx supports them — try the same commands; please
open an issue if anything's missing.

## Demos

| Demo | What it shows |
|------|---------------|
| `helmet_demo` | PBR-lit damaged helmet with rotating directional light + IBL ambient |
| `hierarchy_demo` | Interactive scene graph with draggable cubes (parent-child transforms) |
| `physics_demo` | Falling cubes on a tilting plane via Jolt rigid body simulation |
| `audio_demo` | 3D spatial audio sources with per-category volume sliders |
| `animation_demo` | Skeletal animation playback with crossfade and timeline scrubbing |
| `ik_demo` | Inverse kinematics with foot placement on uneven terrain |
| `ik_hand_demo` | Mouse-driven arm IK on a T-pose model |
| `scene_demo` | Scene serialization — save/load entire scenes to JSON |
| `ui_test` | MSDF text + UI widget rendering (panels, buttons, sliders) |

All demos build to `build/` and must run from there for asset paths to resolve.

## Editor

Sama ships with a native macOS Cocoa editor for building scenes, inspecting entities, and iterating
on gameplay without rebuilding from scratch. Built as a separate `sama_editor` target. macOS only —
Windows/Linux ports are designed but not implemented (see
[`docs/EDITOR_ARCHITECTURE.md`](docs/EDITOR_ARCHITECTURE.md) §1.5).

<!-- Suggested screenshot:
     ![editor](docs/images/editor.png)
     Annotated screenshot showing scene viewport (center), hierarchy (left),
     properties (right), console + asset browser (bottom). Add a PNG to
     docs/images/editor.png and uncomment.
-->

**What it does:**

- Scene hierarchy panel — drag-to-reparent, multi-select, search/filter
- Properties inspector — transforms, materials, lights, rigid bodies, colliders, animation
- Asset browser — textures, meshes, sounds, scenes
- Animation panel — clip preview + event timeline
- Console panel — engine logs and `EditorLog` output
- Translate/rotate/scale gizmo with snapping
- Play mode — runs the active scene with physics; reverts on stop
- Build menu — `Build > Android > {Low, Mid, High}` invokes `build_apk.sh` for the selected tier; status bar shows live phase progress (parsed from `[N/7]` markers) and a Cancel button. `Build > Android > Build & Run` (Cmd+R) auto-installs and launches the APK on the connected device after build. `Build > Android > Settings…` configures the default tier, keystore, output path, package ID, and Build & Run preference (persisted to `NSUserDefaults` across editor restarts).

**Build and run:**

```bash
cmake --build build --target sama_editor -j$(sysctl -n hw.ncpu)
build/sama_editor
```

**Documentation:**

- [`docs/LEVEL_BUILDING_GUIDE.md`](docs/LEVEL_BUILDING_GUIDE.md) — hands-on tutorial: camera, hierarchy, properties, physics, worked falling-cube example
- [`docs/EDITOR_ARCHITECTURE.md`](docs/EDITOR_ARCHITECTURE.md) — design decisions, panel-by-panel architecture, platform abstraction layer

## Tests

```bash
build/engine_tests              # 644 cases / 6493 assertions
build/engine_screenshot_tests   # 22 cases — visual regression
build/engine_screenshot_tests --update-goldens   # regenerate reference images
```

Filter by tag: `build/engine_tests "[physics]"`. See `engine_tests --list-tags` for the full list.

## Using Sama in your game

Pick one umbrella CMake target based on what you need:

| Target | Includes | Good for |
|--------|----------|----------|
| `sama_minimal` | renderer + scene + ECS + game loop + memory | UI apps, prototypes |
| `sama_3d` | minimal + physics + audio + animation/IK + asset loading | most 3D games (recommended) |
| `sama_2d` | minimal + JSON I/O (sprite rendering is in the renderer) | 2D games, UI tools |
| `sama` | everything (input, IO, threading) | full-featured games |

For finer control, link individual `engine_*` libraries directly — see `add_library(engine_*)`
entries in the top-level `CMakeLists.txt`.

### Minimal game (`apps/my_game/`)

```cpp
// MyGame.h
#include "engine/game/IGame.h"

class MyGame : public engine::game::IGame {
public:
    void onInit(engine::core::Engine& engine, engine::ecs::Registry& registry) override;
    void onUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry, float dt) override;
    void onFixedUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry, float fixedDt) override;
    void onShutdown(engine::core::Engine& engine, engine::ecs::Registry& registry) override;
};
```

```cpp
// main.mm  — desktop entry point
#include "engine/game/GameRunner.h"
#include "MyGame.h"
int main() {
    MyGame game;
    engine::game::GameRunner runner(game);
    return runner.run("project.json");
}
```

```cmake
# apps/my_game/CMakeLists.txt
add_executable(my_game main.mm MyGame.cpp)
target_link_libraries(my_game PRIVATE sama_3d)

if(APPLE)
    target_link_libraries(my_game PRIVATE
        "-framework Cocoa" "-framework Metal" "-framework QuartzCore"
        "-framework IOKit" "-framework CoreFoundation")
endif()
```

Then `add_subdirectory(apps/my_game)` from the top-level `CMakeLists.txt`.

See `docs/AGENTS.md` Section 4 for the complete entity-setup template.

### Standalone project (Sama as a dependency)

```cmake
# your_game/CMakeLists.txt
include(FetchContent)
FetchContent_Declare(sama
    GIT_REPOSITORY https://github.com/pixelperfect3/sama.git
    GIT_TAG main)        # pin to a commit hash in production
FetchContent_MakeAvailable(sama)

add_executable(my_game main.mm MyGame.cpp)
target_link_libraries(my_game PRIVATE sama_3d)
```

Sama auto-detects when consumed as a subdirectory and disables demos/tests/editor. First configure
downloads bgfx/Jolt/SoLoud (~5–10 min); subsequent builds are fast.

To force-enable: `set(SAMA_BUILD_DEMOS ON CACHE BOOL "" FORCE)` (and similar for tests / editor)
before `FetchContent_MakeAvailable`.

## Mobile

The same `IGame` runs on desktop, iOS, and Android. The only platform-specific code is a one-line
factory function for mobile entry points:

```cpp
// MyGame_mobile.cpp  (compiled for iOS + Android only)
#include "MyGame.h"
engine::game::IGame* samaCreateGame() { return new MyGame(); }
```

The engine's mobile entry points (iOS `_SamaAppDelegate`, Android `AndroidApp`) call this factory,
wrap the result in a `GameRunner`, and run the same fixed-timestep frame loop as desktop.

### iOS

```bash
./ios/build_ios.sh
xcodebuild -project build/ios/Sama.xcodeproj -scheme ios_test \
    -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 15' build
```

The build script generates an Xcode project via CMake; `xcodebuild` produces the simulator app.
Per-tier asset bundling, runtime tier detection, and CADisplayLink frame rate control are all
wired up. IPA packaging and code signing for device deployment are not yet automated.

See [`docs/IOS_SUPPORT.md`](docs/IOS_SUPPORT.md) for the full architecture, tier matrix, and roadmap.

### Android

```bash
# Native library (debug, arm64-v8a)
./android/build_android.sh arm64-v8a Debug

# Signed APK (default: mid tier, release-signed with debug keystore)
./android/build_apk.sh

# Auto-install on connected device, debug build
./android/build_apk.sh --tier high --debug --install

# AAB for Play Store
./android/build_aab.sh --tier high --keystore release.jks --output MyGame.aab
```

**Tier presets:** `low` (512px, ASTC 8x8, ~30MB APK), `mid` (1024px, ASTC 6x6, ~60MB),
`high` (2048px, ASTC 4x4, ~120MB).

**Prerequisites:** Android NDK r27d+ (`export ANDROID_NDK=...`), Android SDK platform-tools and
build-tools 34.0.0+, Java 17+. `bundletool` for AAB generation.

Hardware verified on Pixel 9: Vulkan + PBR + cast shadows + post-processing + audio + touch + gyro,
60fps at 2251×1080. Debug overlays via dear-imgui (bgfx examples/common/imgui wrapper) also work on
Android — single-finger taps land on ImGui widgets via the existing touch→primary-mouse synthesis.

See [`docs/ANDROID_SUPPORT.md`](docs/ANDROID_SUPPORT.md) for the full architecture and roadmap.

## Architecture

The engine is organized into modular static libraries; each subsystem owns its components,
systems, and resources. See `docs/` for full architecture documentation, including:

- `NOTES.md` — project decisions, threading model, build setup
- `RENDERING_ARCHITECTURE.md`, `SCENE_GRAPH_ARCHITECTURE.md`, `PHYSICS_ARCHITECTURE.md`
- `ANIMATION_ARCHITECTURE.md`, `AUDIO_ARCHITECTURE.md`, `IK_ARCHITECTURE.md`
- `ASSET_ARCHITECTURE.md`, `JSON_ARCHITECTURE.md`, `MEMORY_ARCHITECTURE.md`
- `INPUT_ARCHITECTURE.md`, `MATH_ARCHITECTURE.md`, `THREADING_ARCHITECTURE.md`
- `EDITOR_ARCHITECTURE.md`, `GAME_LAYER_ARCHITECTURE.md`
- `IOS_SUPPORT.md`, `ANDROID_SUPPORT.md` — mobile platform roadmaps
- `AGENTS.md` — API cheat sheet with copy-pasteable code

## Acknowledgments

Sama is built on the following open-source libraries.

| Library | Purpose | License |
|---------|---------|---------|
| [bgfx](https://github.com/bkaradzic/bgfx) (via [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake)) | Cross-platform rendering (Metal, Vulkan) | BSD 2-Clause |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | Rigid body physics | MIT |
| [SoLoud](https://github.com/jarikomppa/soloud) | Audio engine | zlib/libpng |
| [miniaudio](https://github.com/mackron/miniaudio) | Platform audio backends (CoreAudio, AAudio, WASAPI) | MIT-0 |
| [Dear ImGui](https://github.com/ocornut/imgui) | Debug UI overlays | MIT |
| [rapidjson](https://github.com/Tencent/rapidjson) | JSON parsing | MIT |
| [cgltf](https://github.com/jkuhlmann/cgltf) | glTF/GLB asset parsing | MIT |
| [GLM](https://github.com/g-truc/glm) | Math library | MIT |
| [GLFW](https://github.com/glfw/glfw) | Desktop windowing and input | zlib/libpng |
| [stb](https://github.com/nothings/stb) | Image loading/writing | MIT / Public Domain |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | OBJ mesh parsing | MIT |
| [ankerl::unordered_dense](https://github.com/martinus/unordered_dense) | Fast flat hash map | MIT |
| [astcenc](https://github.com/ARM-software/astc-encoder) | ASTC texture compression | Apache 2.0 |
| [Catch2](https://github.com/catchorg/Catch2) | Test framework | BSL-1.0 |

## Contributing

See [`CLAUDE.md`](CLAUDE.md) for the contributor guide (code style, commit format, testing
conventions, AI assistant instructions).

## License

Source-available, personal project. License terms TBD — please open an issue if you'd like to use
this in a commercial project.
