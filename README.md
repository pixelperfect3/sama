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
- **Engine Core** -- Single-init `Engine` class managing renderer, physics, audio, asset manager, and frame arena. Works on both desktop (GLFW) and Android (ANativeWindow) with identical public API. `GameRunner` provides a fixed-timestep frame loop on both platforms. Shared `OrbitCamera` with mouse/keyboard controls.

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

## Android

Sama supports cross-compiling for Android via the NDK. The engine uses Vulkan on Android (bgfx handles the backend) with a pure C++ NativeActivity entry point -- no Java or Kotlin required.

**Games run on Android with zero platform-specific code.** The same `IGame` implementation that runs on desktop works on Android -- the `Engine`, `GameRunner`, and all ECS systems operate identically on both platforms. This is the recommended workflow: develop and iterate on desktop, then build an APK for Android testing and release.

For the full roadmap, design decisions, and phase-by-phase status, see [docs/ANDROID_SUPPORT.md](docs/ANDROID_SUPPORT.md).

### Prerequisites

- **Android NDK r27d+** -- download from [developer.android.com/ndk/downloads](https://developer.android.com/ndk/downloads) or install via Android Studio's SDK Manager
- **Java 17+** -- required by `sdkmanager` and Android build tools (`aapt2`, `apksigner`)
- **Platform tools** -- `adb` for deploying to devices; install via `sdkmanager --install "platform-tools"` or from [developer.android.com/tools/releases/platform-tools](https://developer.android.com/tools/releases/platform-tools)
- **Set `ANDROID_NDK`** -- the build script defaults to `$HOME/Android/Sdk/ndk/26.1.10909125`; override by exporting the variable:
  ```bash
  export ANDROID_NDK=/path/to/your/ndk
  ```

### Building for Android

**Using the build script (recommended):**

```bash
# Default: arm64-v8a, Release
./android/build_android.sh

# Specify ABI and build type
./android/build_android.sh arm64-v8a Debug
./android/build_android.sh armeabi-v7a Release
```

**Manual CMake invocation:**

```bash
cmake -S . -B build/android/arm64-v8a \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release \
    -DSAMA_ANDROID=ON

cmake --build build/android/arm64-v8a -j$(sysctl -n hw.ncpu)
```

**Available ABIs:** `arm64-v8a` (primary, 64-bit ARM), `armeabi-v7a` (32-bit ARM). The build produces a `libsama_android.so` shared library.

### Asset Pipeline

The `sama-asset-tool` CLI processes assets for Android device tiers (texture resizing, format tagging, manifest generation):

```bash
# Build the asset tool (desktop target)
cmake --build build --target sama-asset-tool -j$(sysctl -n hw.ncpu)

# Process assets for each tier
build/sama-asset-tool --input assets/ --output build/android/low/assets/  --target android --tier low
build/sama-asset-tool --input assets/ --output build/android/mid/assets/  --target android --tier mid
build/sama-asset-tool --input assets/ --output build/android/high/assets/ --target android --tier high

# Preview without writing files
build/sama-asset-tool --input assets/ --output out/ --target android --tier mid --dry-run --verbose
```

**Tiers:**

| Tier | Max Texture Size | ASTC Block | Typical APK Size |
|------|-----------------|------------|-----------------|
| `low` | 512px | 8x8 | ~30 MB |
| `mid` | 1024px | 6x6 | ~60 MB |
| `high` | 2048px | 4x4 | ~120 MB |

### Android Project Structure

```
android/
    build_android.sh          NDK build script (ABI + build type args)
    build_apk.sh              APK packaging (Gradle-free, tier, signing, install)
    build_aab.sh              AAB generation for Play Store (bundletool)
    create_debug_keystore.sh  Auto-creates debug keystore for signing
    AndroidManifest.xml       NativeActivity manifest (no Java)
    CMakeLists.txt            Sets SAMA_ANDROID=ON, includes main build

engine/platform/android/
    AndroidApp.h/.cpp         Entry point: engine::platform::runAndroidApp()
    AndroidFileSystem.h/.cpp  AAssetManager-backed IFileSystem
    AndroidWindow.h/.cpp      ANativeWindow wrapper for bgfx
    AndroidInput.h/.cpp       Touch/keyboard input mapping
    AndroidKeyMap.h/.cpp      AKEYCODE_* to engine::input::Key mapping
    AndroidGyro.h/.cpp        ASensorManager gyro/accelerometer
    VirtualJoystick.h/.cpp    On-screen virtual joystick for movement

tools/asset_tool/
    main.cpp                  sama-asset-tool CLI entry point
    AssetProcessor.h/.cpp     Pipeline orchestrator
    TextureProcessor.h/.cpp   Texture discovery and processing
    ShaderProcessor.h/.cpp    Shader discovery and processing
```

### Building an APK

Once the native library builds successfully, package it into a signed APK:

```bash
# Default: mid tier, arm64-v8a, release, debug-signed
./android/build_apk.sh

# Specify tier, debug build, and auto-install via adb
./android/build_apk.sh --tier high --debug --install

# Custom app name, package ID, and output path
./android/build_apk.sh --app-name "My Game" --package com.mygame.app --output MyGame.apk

# Release signing with a custom keystore
./android/build_apk.sh --keystore path/to/release.jks --output MyGame.apk
```

**Additional prerequisites for APK builds:**

- **Android SDK** -- set `ANDROID_SDK_ROOT` or `ANDROID_HOME`
- **Build tools** -- `aapt2`, `zipalign`, `apksigner` (install via `sdkmanager --install 'build-tools;34.0.0'`)
- **Platform** -- `android.jar` (install via `sdkmanager --install 'platforms;android-34'`)
- **Java 17+** -- required by `apksigner` and `keytool`

A debug keystore is created automatically at `$HOME/.android/debug.keystore` on first build.

### Writing a Cross-Platform Game

Write your game as an `IGame` implementation -- no `#ifdef` needed:

```cpp
// MyGame.h
#include "engine/game/IGame.h"

class MyGame : public engine::game::IGame
{
public:
    void onInit(engine::core::Engine& engine, engine::ecs::Registry& registry) override
    {
        // Create entities, load assets, set up game state
    }

    void onUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry, float dt) override
    {
        // Input, camera, game logic (runs every frame)
    }

    void onFixedUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry, float fixedDt) override
    {
        // Physics-rate gameplay (60Hz fixed timestep)
    }

    void onShutdown(engine::core::Engine& engine, engine::ecs::Registry& registry) override
    {
        // Cleanup
    }
};
```

Desktop entry point (`main.mm`):
```cpp
#include "engine/game/GameRunner.h"
#include "MyGame.h"

int main()
{
    MyGame game;
    engine::game::GameRunner runner(game);
    return runner.run("project.json");
}
```

Android entry point (`MyGame_android.cpp`):
```cpp
#include "engine/game/IGame.h"
#include "MyGame.h"

engine::game::IGame* samaCreateGame()
{
    return new MyGame();
}
```

The engine handles all platform differences internally. `Engine::beginFrame()`/`endFrame()`, `resources()`, `inputState()`, shader programs, and framebuffer dimensions all have the same API on both platforms.

### Current Status

**Hardware verified:** Vulkan + UiRenderer + BitmapFont text rendering on **Pixel 9** (2251x1080, 60fps) with SPIRV shaders, touch input, and gyroscope support.

**Implemented:**

- Phase A (NDK Bootstrap) -- CMake toolchain, build script, NativeActivity entry point, AndroidManifest, bgfx Vulkan initialization
- Phase B (Platform Layer) -- `AndroidFileSystem`, `AndroidWindow`, Vulkan surface via bgfx
- Phase C (Touch Input) -- touch-to-mouse mapping, multi-touch, virtual joystick, keyboard, gyroscope/accelerometer
- Phase D (Asset Pipeline) -- `sama-asset-tool` CLI with texture/shader processing, tier configs, asset manifest generation, 17 tests
- Phase E (Tier System) -- `TierConfig` in `ProjectConfig`, `TierAssetResolver`, `tierToRenderSettings()`, 14 tests
- Phase F (APK Packaging) -- `build_apk.sh` with aapt2 + zipalign + apksigner, debug keystore, adb install
- Phase G (Editor Integration) -- Build > Android menu in the editor (Phase G agent)
- Phase H (AAB for Play Store) -- `build_aab.sh` with `bundletool`, multi-ABI support
- **Cross-Platform Game Runner** -- `Engine`, `GameRunner`, and `IGame` work on both desktop and Android with identical APIs. `samaCreateGame()` factory pattern for Android entry point. `engine_core` and `engine_game` CMake targets build on both platforms.

### Building AAB for Play Store

```bash
# Build AAB with both ABIs (arm64-v8a + armeabi-v7a)
./android/build_aab.sh \
    --tier high \
    --keystore ~/keys/release.jks \
    --package com.mystudio.mygame \
    --output MyGame.aab

# Build arm64-v8a only (smaller, covers most modern devices)
./android/build_aab.sh \
    --tier mid \
    --skip-armeabi \
    --output MyGame.aab

# Build unsigned AAB (sign later before upload)
./android/build_aab.sh --tier mid

# Test the AAB locally
bundletool build-apks --bundle=MyGame.aab --output=Game.apks --local-testing
bundletool install-apks --apks=Game.apks
```

**Requirements:** `bundletool` (`brew install bundletool`), Android NDK/SDK, Java 17+.

### Adding Android Support to Your Game

Follow these steps to make any `IGame` implementation run on Android:

**1. Implement your game class (same as desktop -- no platform-specific code):**

```cpp
// MyGame.h
#include "engine/game/IGame.h"

class MyGame : public engine::game::IGame
{
public:
    void onInit(engine::core::Engine& engine, engine::ecs::Registry& registry) override;
    void onUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry, float dt) override;
    // ... other IGame methods
};
```

**2. Add the `samaCreateGame()` factory function:**

```cpp
// MyGame_android.cpp (or at the bottom of MyGame.cpp behind #ifdef __ANDROID__)
#include "engine/game/IGame.h"
#include "MyGame.h"

engine::game::IGame* samaCreateGame()
{
    return new MyGame();
}
```

**3. Add your source file to the `sama_android` target in CMakeLists.txt:**

```cmake
if(SAMA_ANDROID)
    target_sources(sama_android PRIVATE
        apps/my_game/MyGame.cpp
        apps/my_game/MyGame_android.cpp
    )
endif()
```

**4. Build and install:**

```bash
./android/build_apk.sh --tier mid --install
```

The engine's `AndroidApp.cpp` calls your `samaCreateGame()`, wraps it in a `GameRunner`, and runs the same fixed-timestep frame loop as desktop. Your game code is 100% shared.

### Current Status (Hardware Verified)

**Vulkan + UiRenderer + BitmapFont text rendering working on Pixel 9** (2251x1080, 60fps). The full rendering stack is verified: Vulkan backend, SPIRV shaders loaded from APK assets, UiRenderer text overlay, gyroscope, and touch input.

**Note:** bgfx hardcodes `NUM_SWAPCHAIN_IMAGE=4` which is insufficient for some Vulkan drivers (Pixel 9 needs 5). The engine patches this to 8 via a CMake compile definition on the bgfx target. See `docs/ANDROID_SUPPORT.md` for details.

**Known limitations:**

- Full PBR pipeline (shadows, IBL, SSAO) not yet ported to Android -- UiRenderer and BitmapFont text work, but 3D scene rendering requires additional shader porting.
- Post-processing (bloom, FXAA, tone mapping) disabled on Android.
- ASTC texture encoding is stubbed -- textures are currently copied as-is. Full ASTC compression requires the `astcenc` CLI tool.
- No ImGui on Android.

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
