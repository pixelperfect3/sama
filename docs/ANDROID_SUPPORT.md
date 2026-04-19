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

bgfx already supports OpenGL ES 3.x and Vulkan on Android, so the renderer requires zero shader or pipeline changes. The work is in the platform layer, asset pipeline, and packaging toolchain.

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
- [ ] Build smoke test: empty window with clear color on an Android device/emulator (not yet verified on hardware)

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

## Phase F — APK Packaging

**Effort:** Medium | **Dependencies:** Phases A, D, E

- [x] Build script / CMake target for APK assembly (Gradle-free)
  - Cross-compile engine + game → `libsamagame.so` per ABI
  - Run `sama-asset-tool` for the target tier
  - Assemble APK structure:
    ```
    MyGame.apk
      lib/arm64-v8a/libsamagame.so
      lib/armeabi-v7a/libsamagame.so    (optional)
      assets/
        textures/   (ASTC compressed)
        meshes/
        shaders/    (SPIRV + ESSL)
        audio/
        manifest.json
      AndroidManifest.xml
      res/
        mipmap-*/ic_launcher.png
    ```
  - Use `aapt2` for resource compilation, `zipalign` for alignment, `apksigner` for signing
- [x] Keystore management
  - Debug keystore auto-generated on first build
  - Release keystore path configured in `project.json` or passed via CLI
- [x] CLI interface
  ```bash
  sama-build android --tier mid --keystore release.jks --output MyGame.apk
  sama-build android --tier high --debug --install  # build + adb install
  ```
- [x] `adb install` integration for quick deploy to connected device
- [x] Build time optimization
  - Incremental native builds (only recompile changed sources)
  - Cache processed assets (skip re-compression if source unchanged)

---

## Phase G — Editor Integration

**Effort:** Low | **Dependencies:** Phase F

- [ ] File -> Build -> Android submenu
  - Three options: Low / Mid / High tier
  - Or a build dialog with tier dropdown + keystore picker
- [ ] Build progress indicator
  - Progress bar or spinner in the status bar
  - Build log streamed to the Console tab
- [ ] Auto-install option
  - "Build & Run" button: builds APK, installs via `adb`, launches on device
  - Requires `adb` in PATH and a connected device
- [ ] Build configuration persistence
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

## Key Design Decisions

- **Vulkan only, no GLES fallback.** Google has required Vulkan 1.1 for all new devices since Android 10 (2019) and Vulkan 1.3 since Android 14 (2023). As of 2026, Vulkan coverage is ~95%+ across active devices: 100% high-end (2022+), ~98% mid-range (2020+), ~85-90% low-end (2019+). The remaining ~5% are pre-2019 budget devices — a shrinking tail not worth the complexity of maintaining a GLES backend. If GLES fallback is ever needed, bgfx makes it trivial (change `RendererType::Vulkan` to `RendererType::OpenGLES`), but we don't plan for it.

- **NativeActivity, not GameActivity.** NativeActivity is simpler (no Java/Kotlin boilerplate), sufficient for games, and avoids the complexity of JNI bridging. Switch to GameActivity only if we need system UI integration (notifications, in-app purchases, etc.).

- **Gradle-free build.** Direct use of `aapt2` + `zipalign` + `apksigner` keeps the build fast and avoids the 30+ second Gradle startup tax. The tradeoff is we must manage resource compilation ourselves, but games have minimal Android resources (just an icon and manifest).

- **ASTC texture compression.** ASTC is universally supported on all Vulkan-capable Android devices. It offers better quality-per-bit than ETC2 and supports all block sizes from 4x4 (highest quality) to 12x12 (smallest size). The `astcenc` encoder is already in third_party.

- **Tier system over automatic quality scaling.** Auto-detecting the right quality level at runtime is unreliable (GPU model strings are inconsistent, available memory varies with background apps). Explicit tiers let developers test each configuration and guarantee performance. Runtime detection can be added later as a hint for the default tier selection.

- **No hot-reload on Android.** Desktop development uses the editor for rapid iteration. Android builds are for testing and release. Hot-reload would require a TCP bridge and asset streaming protocol — high complexity, low value when the editor already provides instant feedback.
