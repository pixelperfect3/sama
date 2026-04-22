# Android Export — Architecture & Roadmap

This document tracks the design and implementation of Android APK/AAB export for the Sama engine, supporting low/mid/high-end device tiers with appropriate asset quality.

---

## Overview

```
Game Project
  assets/                     Source assets (full quality)
  project.json                ProjectConfig with per-tier settings
  build/
    android/
      low/   -> APK (~30MB,  512px textures, simplified shaders)
      mid/   -> APK (~60MB,  1024px textures, standard shaders)
      high/  -> APK (~120MB, 2048px textures, full PBR + IBL)
    desktop/
```

The export pipeline cross-compiles the engine + game via the Android NDK, processes assets for the target tier (texture compression, shader compilation, optional LOD), and packages everything into a signed APK or AAB.

bgfx supports Vulkan on Android. The renderer requires one platform-specific change: setting `formatColor = RGBA8` (Android surfaces don't support bgfx's default BGRA8). The rest of the work is in the platform layer, asset pipeline, and packaging toolchain.

---

## Dependency Graph

```
A (NDK bootstrap) ----> B (platform layer) ----> F (APK packaging) ----> G (editor)
                  ----> C (touch input)     /                       ----> H (AAB)
D (asset pipeline) ---> E (tier system) ---/
```

Phases A+D can run in parallel. B+C depend on A. E depends on D. F needs A+D+E. G and H are thin wrappers on F.

---

## Phase A — Android NDK Bootstrap (DONE)

**Effort:** Medium | **Dependencies:** None

- [x] CMake toolchain file for Android NDK cross-compilation
  - Target ABIs: `arm64-v8a` (primary), `armeabi-v7a` (optional 32-bit)
  - Minimum API level: 24 (Android 7.0 — covers 95%+ of active devices)
  - Uses `CMAKE_TOOLCHAIN_FILE` from NDK (`$ANDROID_NDK/build/cmake/android.toolchain.cmake`)
  - Build script: `android/build_android.sh [arm64-v8a|armeabi-v7a] [Debug|Release]`
- [x] NativeActivity entry point (`engine/platform/android/AndroidApp.h/.cpp`)
  - `engine::platform::runAndroidApp(android_app*)` — blocks until activity is destroyed
  - Pure C++ via `android_native_app_glue` — no Java/Kotlin required
  - Handles `APP_CMD_INIT_WINDOW`, `APP_CMD_TERM_WINDOW`, etc.
- [x] Minimal `AndroidManifest.xml` template (`android/AndroidManifest.xml`)
  - `android:hasCode="false"` (native-only)
  - `android.hardware.vulkan.level` feature requirement (Vulkan 1.1+)
  - Landscape orientation default (configurable)
- [x] CMakeLists.txt integration
  - `SAMA_ANDROID` option gates desktop-only targets (GLFW, editor, demos, tests)
  - `android/CMakeLists.txt` entry point forces `SAMA_ANDROID=ON` and includes the main build
  - `sama_android` shared library target with `-u ANativeActivity_onCreate` link flag
  - Desktop guards (`if(NOT SAMA_ANDROID)`) around GLFW, engine_core, engine_game, editor, demos
- [x] Verify bgfx initializes with Vulkan backend on Android
  - `bgfx::Init::type = bgfx::RendererType::Vulkan`
  - bgfx's `PlatformData` needs `nativeWindowHandle` from `ANativeWindow`
  - bgfx creates `VkSurfaceKHR` via `vkCreateAndroidSurfaceKHR` internally
- [x] Build smoke test: verified rendering on physical hardware (Pixel 9, Vulkan, 2251x1080)

---

## Phase B — Android Platform Layer (DONE)

**Effort:** Low | **Dependencies:** Phase A

- [x] `AndroidFileSystem` implementing `IFileSystem`
  - Backed by `AAssetManager` — reads from APK's `assets/` folder
  - `AAsset_getBuffer()` for zero-copy reads, `AAsset_getLength()` for size queries
  - Follows the same interface as `StdFileSystem` so all existing loaders work unchanged
  - Path resolution with `..` and `.` normalization for relative asset references
  - Source: `engine/platform/android/AndroidFileSystem.h/.cpp`
- [x] `AndroidWindow` wrapping `ANativeWindow`
  - Pass `ANativeWindow*` to bgfx via `bgfx::PlatformData::nativeWindowHandle`
  - Vulkan only: bgfx creates `VkSurfaceKHR` via `vkCreateAndroidSurfaceKHR` internally
  - No EGL — our code never touches graphics API surfaces, just manages the `ANativeWindow*` lifecycle
  - Surface size tracking for framebuffer resize on orientation change (`updateSize()`)
  - Content scale factor from DPI density (160 dpi = 1.0x baseline)
  - Source: `engine/platform/android/AndroidWindow.h/.cpp`
- [ ] Lifecycle handling (deferred)
  - Pause: stop rendering, release EGL surface
  - Resume: re-create surface, resume rendering
  - `onSaveInstanceState` / `onRestoreInstanceState` for game state preservation
- [ ] Audio backend (deferred)
  - AAudio (API 26+) or OpenSL ES (API 24+) for SoLoud
  - SoLoud already has both backends — just enable the right one in CMake

---

## Phase C — Touch Input (DONE)

**Effort:** Low | **Dependencies:** Phase A

- [x] Map `AInputEvent` touch events to `InputState`
  - Single touch drives `mouseX_`/`mouseY_` and left mouse button for desktop-code compatibility
  - Multi-touch tracked with stable pointer IDs in `InputState::touches_`
  - `ACTION_DOWN`/`POINTER_DOWN` -> `Phase::Began`, `ACTION_MOVE` -> `Phase::Moved`, `ACTION_UP`/`POINTER_UP` -> `Phase::Ended`, `ACTION_CANCEL` -> all ended
  - `endFrame()` clears per-frame flags (pressed/released), removes ended touches, promotes Began to Moved
  - Source: `engine/platform/android/AndroidInput.h/.cpp`
- [x] Virtual joystick overlay for movement
  - Configurable center position, radius, dead zone, and opacity (all normalized screen coords)
  - Produces normalized direction vector `[-1,1]` with dead zone remapping and radius clamping
  - 1.5x activation radius for forgiving touch targeting
  - Source: `engine/platform/android/VirtualJoystick.h/.cpp`
  - TODO: rendering via UiRenderer (positional logic implemented, visual overlay pending)
- [ ] Multi-touch gestures (deferred)
  - Pinch-to-zoom → scroll delta (for camera zoom)
  - Two-finger pan → right-drag equivalent (for camera orbit)
- [x] Keyboard support (for devices with physical keyboards or Bluetooth)
  - Full `AKEYCODE_*` to `engine::input::Key` mapping: A-Z, 0-9, F1-F12, Numpad 0-9, modifiers, punctuation, navigation
  - `AKEYCODE_BACK` mapped to `Key::Escape` for menu/back behavior
  - Two implementations: `AndroidKeyMap` (cross-platform testable with local constants) and `AndroidInput::mapKeyCode` (uses real Android headers)
  - Source: `engine/platform/android/AndroidKeyMap.h/.cpp`, `engine/platform/android/AndroidInput.cpp`
  - Tests: `tests/platform/TestAndroidKeyMap.cpp` (11 test cases)
- [x] Gyroscope / accelerometer input
  - `AndroidGyro` manages `ASensorManager` lifecycle with enable/disable for battery savings
  - Polls `ASENSOR_TYPE_GYROSCOPE` (pitch/yaw/roll rates) and `ASENSOR_TYPE_ACCELEROMETER` (gravity vector normalized to [-1,1])
  - Configurable sample rate (default ~60Hz)
  - Platform-agnostic `GyroState` in `InputState` with `available` flag
  - Source: `engine/platform/android/AndroidGyro.h/.cpp` (compiled only on Android via `#ifdef __ANDROID__`)

---

## Phase D — Asset Pipeline CLI (DONE)

**Effort:** Medium | **Dependencies:** None (can run in parallel with A)

- [x] `sama-asset-tool` command-line executable (`tools/asset_tool/`)
  ```bash
  sama-asset-tool --input assets/ --output build/android/mid/assets/ --target android --tier mid
  sama-asset-tool --help          # show all options
  sama-asset-tool --dry-run       # preview without writing
  sama-asset-tool --verbose       # detailed progress
  ```
  - `AssetProcessor` orchestrates the full pipeline: discover, process, write manifest
  - `TextureProcessor` discovers and processes texture assets (PNG, KTX, KTX2, DDS)
  - `ShaderProcessor` discovers and processes shader assets
  - `CliArgs` struct parsed from command line (--input, --output, --target, --tier, --verbose, --dry-run)
- [x] Tier configs (`TierConfig` struct)
  - `low`: max 512px textures, ASTC 8x8
  - `mid`: max 1024px textures, ASTC 6x6
  - `high`: max 2048px textures, ASTC 4x4
- [x] Asset manifest
  - `manifest.json` listing all processed assets with type, source, output, format, dimensions
  - `AssetEntry` struct tracks type, source/output paths, format, width/height, original dimensions
- [x] 17 tests covering the asset tool pipeline
- **Known limitation:** ASTC encoding is stubbed — `astc-codec` (third_party) is decode-only. Textures are currently copied to the output directory with a TODO logged for each texture that would be ASTC-compressed. Full ASTC encoding requires the `astcenc` CLI tool.
- [ ] Mesh processing (optional) — LOD generation, vertex cache optimization (deferred)
- [ ] Audio transcoding — WAV to Opus (deferred)

---

## Phase E — Tier System in ProjectConfig (DONE)

**Effort:** Medium | **Dependencies:** Phase D

- [x] Extend `ProjectConfig` JSON with tier definitions
  ```json
  {
      "activeTier": "mid",
      "tiers": {
          "low": {
              "maxTextureSize": 512,
              "textureCompression": "astc_8x8",
              "shadowMapSize": 512,
              "shadowCascades": 1,
              "maxBones": 64,
              "enableIBL": false,
              "enableSSAO": false,
              "enableBloom": false,
              "enableFXAA": true,
              "depthPrepass": false,
              "renderScale": 0.75,
              "targetFPS": 30
          },
          "mid": { ... },
          "high": { ... }
      }
  }
  ```
  - Full list of TierConfig fields: `maxTextureSize`, `textureCompression`, `shadowMapSize`, `shadowCascades`, `maxBones`, `enableIBL`, `enableSSAO`, `enableBloom`, `enableFXAA`, `depthPrepass`, `renderScale`, `targetFPS`
  - Partial tier definitions supported — unspecified fields keep TierConfig defaults
  - Custom tier names supported (not limited to low/mid/high)
- [x] `TierConfig` struct parsed at startup, feeds into `RenderSettings` and `EngineDesc`
  - `getActiveTier()` — looks up active tier name in user tiers first, then built-in defaults, falls back to "mid"
  - `tierToRenderSettings()` — converts TierConfig to RenderSettings (shadow resolution/cascades/filter, IBL, SSAO, bloom, FXAA, depth prepass, render scale)
  - `toEngineDesc()` — applies active tier's shadow settings when tiers are configured, otherwise uses legacy RenderConfig
  - `defaultTiers()` — returns three built-in presets: low (weak mobile), mid (mainstream), high (flagship)
  - Source: `engine/game/ProjectConfig.h/.cpp`
  - Tests: `tests/game/TestTierConfig.cpp` (14 test cases covering defaults, getActiveTier, tierToRenderSettings, JSON parsing, toEngineDesc)
- [ ] Runtime tier detection (deferred): auto-select tier based on GPU model / available memory
  - `GL_RENDERER` string matching or Vulkan `VkPhysicalDeviceProperties`
  - Fallback: default to "mid" if detection fails
- [x] Asset loader picks tier-appropriate assets
  - `resolveAssetPath(basePath, relativePath, tier)` — checks `<basePath>/<tier>/<relativePath>` first, falls back to `<basePath>/<relativePath>`
  - Pure utility function, no IFileSystem dependency
  - Source: `engine/assets/TierAssetResolver.h/.cpp`
  - Tests: 3 test cases in `tests/game/TestTierConfig.cpp` (tier-specific path exists, fallback, empty tier)

---

## Phase F — APK Packaging (DONE)

**Effort:** Medium | **Dependencies:** Phases A, D, E

- [x] Build script for APK assembly (Gradle-free): `android/build_apk.sh`
  - 7-step pipeline: NDK build → asset processing → staging → aapt2 link → zip native lib + assets → zipalign → apksigner
  - Cross-compile engine + game → `libsama_android.so` per ABI via `android/build_android.sh`
  - Run `sama-asset-tool` for the target tier (falls back to raw copy if tool not built)
  - Assemble APK structure:
    ```
    Game.apk
      lib/arm64-v8a/libsama_android.so
      assets/          (processed by sama-asset-tool)
      AndroidManifest.xml
    ```
  - Uses `aapt2` for resource compilation, `zipalign` for 4-byte alignment, `apksigner` for signing
  - Validates all dependencies upfront (NDK, SDK, aapt2, zipalign, apksigner, android.jar, cmake, keytool)
  - Auto-discovers latest build-tools version from SDK, falls back to PATH
  - Reports APK size on completion
- [x] Keystore management: `android/create_debug_keystore.sh`
  - Debug keystore auto-generated on first build (`$HOME/.android/debug.keystore`)
  - Release keystore path passed via `--keystore` CLI option
  - Non-interactive creation via `keytool -dname`
- [x] CLI interface
  ```bash
  ./android/build_apk.sh                                     # default: mid tier, arm64-v8a, release
  ./android/build_apk.sh --tier high --debug --install       # debug build + adb install
  ./android/build_apk.sh --tier low --keystore release.jks   # release signing
  ./android/build_apk.sh --app-name "My Game" --package com.mygame.app --output MyGame.apk
  ```
- [x] `adb install` integration via `--install` flag for quick deploy to connected device
- [x] Build time optimization
  - Incremental native builds (CMake only recompiles changed sources)
  - Intermediate files cleaned after APK generation (base.apk, unsigned.apk, aligned.apk)

### Known Issues (Phase F)

- **Custom keystore with no `--ks-pass`:** When using `--keystore`, `apksigner` will prompt interactively for the password. In CI/automated builds, pass the keystore password via the `apksigner` environment or extend the script with a `--ks-pass` option.
- **Stale staging directory:** The staging directory (`build/android/apk_staging/`) is not cleaned between runs. Stale files from previous builds could be included in the APK. Add `rm -rf "$STAGING_DIR"` before step 3 for clean builds.
- **AndroidManifest.xml Vulkan feature:** The manifest uses `android.hardware.vulkan` as the feature name. The canonical name is `android.hardware.vulkan.level` with `android:version="1"` for Vulkan 1.1. The current value works on most devices but may not be recognized by all Play Store filters.

---

## Phase G — Editor Integration (DONE)

**Effort:** Low | **Dependencies:** Phase F

- [x] Build -> Android submenu in `CocoaEditorWindow.mm`
  - Three menu items: Low / Mid / High tier
  - Triggers `build_android_low` / `build_android_mid` / `build_android_high` actions
- [x] Background build thread in `EditorApp.cpp`
  - Spawns `build_apk.sh` with the selected tier on a detached `std::thread`
  - Build output logged to the Console panel via `EditorLog`
  - Non-blocking: editor remains interactive during build
- [ ] Build progress indicator (deferred)
  - Progress bar or spinner in the status bar
  - Build log streamed to the Console tab in real time
- [ ] Auto-install option (deferred)
  - "Build & Run" button: builds APK, installs via `adb`, launches on device
  - Requires `adb` in PATH and a connected device
- [ ] Build configuration persistence (deferred)
  - Remember last-used tier, keystore path, output directory in editor preferences

---

## Phase H — AAB for Play Store (DONE)

**Effort:** Low | **Dependencies:** Phase F

- [x] Generate Android App Bundle (`.aab`) instead of APK
  - `android/build_aab.sh` — standalone shell script, no Gradle
  - Uses `bundletool` to create the AAB from a base module zip
  - Reuses `build_android.sh` for NDK cross-compilation and `sama-asset-tool` for asset processing
  - Play Store handles per-device APK generation from the AAB
- [x] Multi-ABI support
  - Builds both `arm64-v8a` and `armeabi-v7a` by default for maximum device coverage
  - `--skip-armeabi` flag for arm64-only builds (smaller bundle, modern devices only)
  - Each ABI placed in `base/lib/<abi>/` within the module structure
- [x] AAB signing with `jarsigner`
  - `--keystore`, `--ks-pass`, `--ks-alias`, `--key-pass` options
  - Unsigned build supported for later signing before Play Store upload
- [x] Dependency validation at startup
  - Checks for `bundletool`, `jarsigner`, `cmake`, `aapt2`, Android NDK/SDK, `android.jar`
  - Clear install instructions for each missing tool
- [ ] Asset packs for large games (>150MB base APK limit) (deferred)
  - `install-time` asset pack for core assets
  - `fast-follow` pack for optional content (additional levels, HD textures)
  - Uses Play Asset Delivery API
- [ ] Play Console metadata (deferred)
  - Generate `output-metadata.json` compatible with Play Console upload

---

## Cross-Platform Game Runner (The Missing Piece)

With the completion of the Android game runner, **IGame implementations now work identically on desktop and Android with zero platform-specific code.** This was the key integration that ties all the Android phases together -- previously the platform layer, input, and asset pipeline existed but there was no way to actually run a game on Android using the same code as desktop.

### What changed

1. **`Engine` works on both platforms.** The public API (`resources()`, `inputState()`, `beginFrame()`/`endFrame()`, shader programs, framebuffer dimensions) is identical. Platform differences are hidden behind `#ifdef __ANDROID__` in the implementation:
   - Desktop: GLFW window, ImGui, mouse/keyboard input
   - Android: `ANativeWindow`, touch/gyro input, no ImGui

2. **`GameRunner` has desktop and Android entry points.** `run()` for desktop, `runAndroid()` for Android. Both call the same internal `runLoop()` which implements the fixed-timestep frame loop (accumulator-based, 60Hz default, configurable). The only difference is how `Engine` is initialized.

3. **`samaCreateGame()` factory function.** On Android, there is no `main()`. Instead, games define an `extern` factory function:
   ```cpp
   // In your game's .cpp file:
   engine::game::IGame* samaCreateGame()
   {
       return new MyGame();
   }
   ```
   The engine's `AndroidApp.cpp` calls this to get the game instance, wraps it in a `GameRunner`, and calls `runAndroid()`. This uses extern linkage -- the game's `.cpp` is compiled into the same shared library as the engine, so the linker resolves the symbol.

4. **`engine_core` and `engine_game` CMake targets build on both platforms.** On desktop they use GLFW; on Android they use the Android platform layer. The game code links against the same targets either way.

### Cross-platform game pattern

Write your game once:

```cpp
// MyGame.h
#include "engine/game/IGame.h"

class MyGame : public engine::game::IGame
{
public:
    void onInit(engine::core::Engine& engine, engine::ecs::Registry& registry) override;
    void onUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry, float dt) override;
    void onFixedUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry, float fixedDt) override;
    void onShutdown(engine::core::Engine& engine, engine::ecs::Registry& registry) override;
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

The `MyGame` class is identical in both cases -- 100% shared code.

---

## Verified on Hardware

First successful render: **Pixel 9, Vulkan backend, 2251x1080 resolution.**

The `android_test` app runs at 60fps with the full stack verified: **Vulkan + SPIRV shaders + UiRenderer + BitmapFont text rendering + gyroscope + touch input.** This is not just a clear-screen test — real shader programs load from APK assets and render text overlays via the engine's UiRenderer system.

### Shader Compilation Pipeline

SPIRV shaders are pre-compiled on the host machine and bundled into the APK:

```bash
# Compile all engine shaders for SPIRV (Android/Vulkan)
./android/compile_shaders.sh

# Output: shaders/spirv/*.bin files
# These are copied into the APK's assets/shaders/spirv/ directory by build_apk.sh
```

At runtime, `AndroidFileSystem` loads `.bin` shader binaries from APK assets via `AAssetManager`. The shader loading path is identical to desktop — only the binary format differs (SPIRV vs Metal).

### bgfx Swapchain Image Count (RESOLVED)

bgfx previously hardcoded `NUM_SWAPCHAIN_IMAGE=4` in its Vulkan backend. The Pixel 9 (and other modern Android devices) requires 5 swapchain images, causing Vulkan init to fail silently and fall back to OpenGL ES.

**Resolution:** We updated bgfx.cmake from the stale `widberg/bgfx.cmake` fork to the official `bkaradzic/bgfx.cmake` repo. The upstream bgfx now uses `kMaxBackBuffers = bx::max(BGFX_CONFIG_MAX_BACK_BUFFERS, 10)`, which natively supports up to 10 swapchain images. No CMake patch is needed.

### Vulkan Surface Format (RGBA8 vs BGRA8)

bgfx defaults `Resolution::formatColor` to `TextureFormat::BGRA8` (`VK_FORMAT_B8G8R8A8_UNORM`). On Android, Mali and Adreno GPUs typically only expose `VK_FORMAT_R8G8B8A8_UNORM` (RGBA8) as a Vulkan surface format. BGRA8 is optional on mobile per the Vulkan spec. If the requested format doesn't match a supported surface format, bgfx's swapchain creation fails silently and it falls back to OpenGL ES.

**Fix:** `Renderer::init()` sets `init.resolution.formatColor = bgfx::TextureFormat::RGBA8` on Android. RGBA8 is mandatory for all Vulkan-capable Android devices per the Android CDD.

**Why not auto-detect?** bgfx's `getCaps()->formats[]` with `BGFX_CAPS_FORMAT_TEXTURE_BACKBUFFER` reports which formats the surface supports, but this data is populated *during* `bgfx::init()` — after the swapchain is already created with `formatColor`. There is no pre-init query API. A pre-init Vulkan surface query (creating a temporary VkInstance) would work but is unnecessarily complex given that RGBA8 is universally supported on Android. If a future device requires a different format, the fix would be to add a pre-init Vulkan surface format query in `Renderer::init()`.

### Emulator Testing

Android emulators on macOS Apple Silicon (M3) support Vulkan via gfxstream -> MoltenVK -> Metal translation. Three AVDs are configured for tier testing:

| AVD | Device | API | Resolution | RAM | Use case |
|-----|--------|-----|------------|-----|----------|
| `sama_low` | Pixel | 31 | 720x1280 | 2GB | Low-end / minimum spec |
| `sama_mid` | Pixel 6 | 33 | 1080x2400 | 4GB | Mid-range baseline |
| `sama_high` | Pixel 7 Pro | 34 | 1440x3120 | 6GB | High-end / flagship |

**Launch and test:**
```bash
# Start emulator (headless)
$HOME/Android/Sdk/emulator/emulator -avd sama_mid -gpu host -no-snapshot -no-audio -no-window &

# Wait for boot, install, launch
adb wait-for-device
adb install -r build/android/Game.apk
adb shell am start -n com.sama.game/android.app.NativeActivity

# Check logs
adb logcat -d | grep "SamaEngine"

# Kill emulator
adb emu kill
```

**bgfx fragment shading rate patch:** bgfx unconditionally chains `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR` into `vkGetPhysicalDeviceProperties2` pNext. This is valid per the Vulkan spec (drivers should ignore unknown pNext structs), but the emulator's gfxstream layer aborts on unrecognized struct types. We patch this in `patches/bgfx_emulator_compat.patch` to only chain the struct when the extension is supported. This patch is applied via `PATCH_COMMAND` in CMakeLists.txt.

**Emulator limitations:** Vulkan 1.1 only (via MoltenVK), limited extensions, no fragment shading rate. The MoltenVK backend supports BGRA8 (unlike real Mali hardware), so the RGBA8 surface format issue is not reproduced on emulators. Use real hardware to validate GPU-specific behavior.

### Known Limitations (Current State)

- **PBR rendering not yet ported.** UiRenderer and BitmapFont text rendering work via SPIRV shaders, but the full PBR pipeline (shadows, IBL, SSAO) has not been ported to Android yet.
- **Post-processing disabled.** `Engine::beginFrame()` on Android calls `renderer_.beginFrameDirect()` which bypasses the post-process framebuffer setup entirely. There is no bloom, FXAA, or tone mapping on Android.
- **SSAO disabled.** `SsaoSystem` returns early on Android (shader handle is invalid).
- **No ImGui on Android.** The engine skips ImGui initialization entirely on Android; `imguiWantsMouse()` always returns false.

### Bugs Fixed During Hardware Bring-up

1. **Event loop deadlock.** The initial implementation used a single `ALooper_pollAll(-1, ...)` call that blocked indefinitely once the window was ready. Fix: recompute the timeout each iteration of the inner loop -- use `-1` (blocking) only when the window is not ready or the app is not focused, and `0` (non-blocking) when rendering should proceed.

2. **Missing `beginFrameDirect` call.** On Android, the post-process pipeline is disabled (shaders are stubs), so `beginFrame()` on Android must call `renderer_.beginFrameDirect()` instead of the desktop's ImGui-based begin frame. Without this, bgfx had no active encoder and `bgfx::touch(0)` was a no-op.

3. **Touch input not reaching game code.** `AndroidInputBackend` only emitted `TouchBegin`/`TouchMove`/`TouchEnd` raw events but never synthesized `MouseButtonDown`/`MouseButtonUp`/`MouseMove`. Game code using `isMouseButtonHeld(MouseButton::Left)` for UI buttons (the standard desktop pattern) never saw touch input. Fix: the primary touch pointer now synthesizes corresponding mouse events, making all desktop mouse-based game code work on Android automatically.

4. **`libc++_shared.so` not packaged in APK.** The NDK build uses `ANDROID_STL=c++_shared` (required for exception support across shared library boundaries), but the runtime library was not being copied into the APK's `lib/<abi>/` directory. Fix: `build_apk.sh` now copies `libc++_shared.so` from the NDK sysroot into the staging directory alongside `libsama_android.so`.

---

## Key Design Decisions

- **Vulkan only, no GLES fallback.** Google has required Vulkan 1.1 for all new devices since Android 10 (2019) and Vulkan 1.3 since Android 14 (2023). As of 2026, Vulkan coverage is ~95%+ across active devices: 100% high-end (2022+), ~98% mid-range (2020+), ~85-90% low-end (2019+). The remaining ~5% are pre-2019 budget devices — a shrinking tail not worth the complexity of maintaining a GLES backend. If GLES fallback is ever needed, bgfx makes it trivial (change `RendererType::Vulkan` to `RendererType::OpenGLES`), but we don't plan for it.

- **NativeActivity, not GameActivity.** NativeActivity is simpler (no Java/Kotlin boilerplate), sufficient for games, and avoids the complexity of JNI bridging. Switch to GameActivity only if we need system UI integration (notifications, in-app purchases, etc.).

- **Gradle-free build.** Direct use of `aapt2` + `zipalign` + `apksigner` keeps the build fast and avoids the 30+ second Gradle startup tax. The tradeoff is we must manage resource compilation ourselves, but games have minimal Android resources (just an icon and manifest).

- **ASTC texture compression.** ASTC is universally supported on all Vulkan-capable Android devices. It offers better quality-per-bit than ETC2 and supports all block sizes from 4x4 (highest quality) to 12x12 (smallest size). The `astcenc` encoder is already in third_party.

- **Tier system over automatic quality scaling.** Auto-detecting the right quality level at runtime is unreliable (GPU model strings are inconsistent, available memory varies with background apps). Explicit tiers let developers test each configuration and guarantee performance. Runtime detection can be added later as a hint for the default tier selection.

- **No hot-reload on Android.** Desktop development uses the editor for rapid iteration. Android builds are for testing and release. Hot-reload would require a TCP bridge and asset streaming protocol — high complexity, low value when the editor already provides instant feedback.

- **Single Engine class with `#ifdef` vs EngineAndroid subclass.** We chose `#ifdef __ANDROID__` inside one `Engine` class rather than an inheritance hierarchy (`EngineDesktop` / `EngineAndroid`). The public API is identical on both platforms -- only the init path, window management, and input backend differ. A subclass design would force game code to use `Engine&` references everywhere (which they already do), but would also fragment the implementation across two files with duplicated frame logic. The `#ifdef` approach keeps all frame lifecycle code in one place, and the compile-time branching means zero runtime cost.

- **SPIRV shaders loaded from APK assets at runtime.** The alternative was embedding shader binaries directly into the `.so` via `xxd` or `#include` of binary arrays. We chose APK asset loading because: (1) it keeps the shared library small and avoids bloating the text segment, (2) shaders can be updated without recompiling native code, (3) it mirrors the desktop pattern where shaders are loaded from the filesystem, and (4) `AAssetManager` provides zero-copy access which is as fast as embedded data. The tradeoff is a dependency on the asset packaging step in `build_apk.sh`, but this is already required for textures and models.

- **bgfx dependency: official bkaradzic/bgfx.cmake.** We use the official bgfx CMake wrapper (`bkaradzic/bgfx.cmake`) rather than the stale `widberg/bgfx.cmake` fork. The official repo includes the swapchain image count fix (`kMaxBackBuffers = max(BGFX_CONFIG_MAX_BACK_BUFFERS, 10)`), eliminating the need for our old CMake patch. The bgfx.cmake update also required adapting to API changes: `shaderc_parse` → `_bgfx_shaderc_parse`, `mtxFromCols3` → `mtxFromCols`, `instMul` → `mul`, ImGui `KeyMap`/`KeysDown` → `AddKeyEvent`, and disabling WGSL shader support (`BGFX_PLATFORM_SUPPORTS_WGSL=0`).

- **`samaCreateGame()` extern linkage vs registration.** The Android entry point uses `extern engine::game::IGame* samaCreateGame()` -- a function the game defines and the engine calls. The alternative was a registration pattern (e.g., `REGISTER_GAME(MyGame)` macro expanding to a static initializer). We chose extern linkage because: (1) it is explicit and easy to understand, (2) there is exactly one game per APK so a registry is unnecessary, (3) static initialization order issues are avoided entirely, and (4) the pattern is familiar from `main()` itself.

- **Shared `runLoop()` factored out.** `GameRunner` has platform-specific entry points (`run()` and `runAndroid()`) but the actual frame loop -- fixed-timestep accumulator, `IGame` callback dispatch, `beginFrame`/`endFrame` -- lives in a single `runLoop(Engine&)` method. This guarantees that desktop and Android have identical frame timing behavior and that bug fixes to the loop apply to both platforms.
