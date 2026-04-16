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

## Phase A — Android NDK Bootstrap

**Effort:** Medium | **Dependencies:** None

- [ ] CMake toolchain file for Android NDK cross-compilation
  - Target ABIs: `arm64-v8a` (primary), `armeabi-v7a` (optional 32-bit)
  - Minimum API level: 24 (Android 7.0 — covers 95%+ of active devices)
  - Use `CMAKE_TOOLCHAIN_FILE` from NDK (`$ANDROID_NDK/build/cmake/android.toolchain.cmake`)
- [ ] NativeActivity entry point (`engine/platform/android/AndroidApp.cpp`)
  - Pure C++ via `android_native_app_glue` — no Java/Kotlin required for MVP
  - Handle `APP_CMD_INIT_WINDOW`, `APP_CMD_TERM_WINDOW`, `APP_CMD_GAINED_FOCUS`, `APP_CMD_LOST_FOCUS`
  - Wire into `Engine::init()` / `Engine::beginFrame()` / `Engine::endFrame()` lifecycle
- [ ] Minimal `AndroidManifest.xml` template
  - `android:hasCode="false"` (native-only)
  - `android.hardware.vulkan.level` feature requirement (Vulkan 1.1+)
  - Landscape orientation default (configurable)
- [ ] Verify bgfx initializes with Vulkan backend on Android
  - `bgfx::Init::type = bgfx::RendererType::Vulkan`
  - bgfx's `PlatformData` needs `nativeWindowHandle` from `ANativeWindow`
  - bgfx creates `VkSurfaceKHR` via `vkCreateAndroidSurfaceKHR` internally
- [ ] Build smoke test: empty window with clear color on an Android device/emulator

---

## Phase B — Android Platform Layer

**Effort:** Low | **Dependencies:** Phase A

- [ ] `AndroidFileSystem` implementing `IFileSystem`
  - Backed by `AAssetManager` — reads from APK's `assets/` folder
  - `AAsset_read()` for synchronous loads, `AAsset_getLength()` for size queries
  - Follows the same interface as `StdFileSystem` so all existing loaders work unchanged
- [ ] `AndroidWindow` wrapping `ANativeWindow`
  - Pass `ANativeWindow*` to bgfx via `bgfx::PlatformData::nativeWindowHandle`
  - Vulkan only: bgfx creates `VkSurfaceKHR` via `vkCreateAndroidSurfaceKHR` internally
  - No EGL — our code never touches graphics API surfaces, just manages the `ANativeWindow*` lifecycle
  - Surface size tracking for framebuffer resize on orientation change
  - Content scale factor (display density via `AConfiguration_getDensity`) for HiDPI-aware rendering
- [ ] Lifecycle handling
  - Pause: stop rendering, release EGL surface
  - Resume: re-create surface, resume rendering
  - `onSaveInstanceState` / `onRestoreInstanceState` for game state preservation
- [ ] Audio backend
  - AAudio (API 26+) or OpenSL ES (API 24+) for SoLoud
  - SoLoud already has both backends — just enable the right one in CMake

---

## Phase C — Touch Input

**Effort:** Low | **Dependencies:** Phase A

- [ ] Map `AInputEvent` touch events to `InputState`
  - Single touch → mouse position + left button
  - Sufficient for orbit camera and UI interaction
- [ ] Virtual joystick overlay for movement
  - Transparent on-screen joystick (left side) for WASD-equivalent
  - Rendered via `UiRenderer` as a semi-transparent circle + thumb
- [ ] Multi-touch gestures
  - Pinch-to-zoom → scroll delta (for camera zoom)
  - Two-finger pan → right-drag equivalent (for camera orbit)
- [ ] Keyboard support (for devices with physical keyboards or Bluetooth)
  - Map `AKEYCODE_*` to `engine::input::Key` enum
- [ ] Gyroscope / accelerometer input
  - Add `GyroState` to `InputState` (pitch/yaw/roll rates + gravity vector + available flag)
  - Android: `ASensorManager` + `ASENSOR_TYPE_GYROSCOPE` / `ASENSOR_TYPE_GAME_ROTATION_VECTOR`
  - Use cases: motion aiming, tilt steering, camera look, AR orientation
  - Configurable sensitivity and dead zone
  - Platform-agnostic API so iOS (`CMMotionManager`) and Switch (Joy-Con) can plug in later

---

## Phase D — Asset Pipeline CLI

**Effort:** Medium | **Dependencies:** None (can run in parallel with A)

- [ ] `sama-asset-tool` command-line executable
  ```bash
  sama-asset-tool --target android --tier mid --input assets/ --output build/android/mid/assets/
  ```
- [ ] Texture compression
  - Source: PNG, KTX, KTX2, DDS
  - Output: ASTC via `astcenc` (already in third_party as astc-codec)
  - Quality per tier: 4x4 (high), 6x6 (mid), 8x8 (low)
  - Max texture size per tier: 2048 (high), 1024 (mid), 512 (low) — downscale if needed
- [ ] Shader cross-compilation
  - bgfx `shaderc` already compiles to SPIRV — invoke it for Android Vulkan target
  - Output SPIRV only (no ESSL — Vulkan-only target)
  - Embed compiled shaders in the asset bundle
- [ ] Mesh processing (optional)
  - LOD generation for low tier (mesh decimation via meshoptimizer or similar)
  - Vertex cache optimization (`meshopt_optimizeVertexCache`)
- [ ] Audio transcoding
  - WAV → Opus for smaller APK size
  - Keep original format option for latency-sensitive sounds
- [ ] Asset manifest
  - `manifest.json` listing all processed assets with checksums, sizes, and tier tags
  - Used by the engine at runtime to verify asset integrity

---

## Phase E — Tier System in ProjectConfig

**Effort:** Medium | **Dependencies:** Phase D

- [ ] Extend `ProjectConfig` JSON with tier definitions
  ```json
  {
      "tiers": {
          "low": {
              "maxTextureSize": 512,
              "textureCompression": "astc_8x8",
              "shadowMapSize": 512,
              "maxBones": 64,
              "enableIBL": false,
              "enableSSAO": false,
              "enableBloom": false,
              "targetFPS": 30
          },
          "mid": {
              "maxTextureSize": 1024,
              "textureCompression": "astc_6x6",
              "shadowMapSize": 1024,
              "maxBones": 128,
              "enableIBL": true,
              "enableSSAO": false,
              "enableBloom": true,
              "targetFPS": 30
          },
          "high": {
              "maxTextureSize": 2048,
              "textureCompression": "astc_4x4",
              "shadowMapSize": 2048,
              "maxBones": 128,
              "enableIBL": true,
              "enableSSAO": true,
              "enableBloom": true,
              "targetFPS": 60
          }
      }
  }
  ```
- [ ] `TierConfig` struct parsed at startup, feeds into `RenderSettings` and `EngineDesc`
- [ ] Runtime tier detection (optional): auto-select tier based on GPU model / available memory
  - `GL_RENDERER` string matching or Vulkan `VkPhysicalDeviceProperties`
  - Fallback: default to "mid" if detection fails
- [ ] Asset loader picks tier-appropriate assets
  - Check `assets/<tier>/texture.ktx` first, fall back to `assets/texture.ktx`
  - Or use the asset manifest to resolve paths

---

## Phase F — APK Packaging

**Effort:** Medium | **Dependencies:** Phases A, D, E

- [ ] Build script / CMake target for APK assembly (Gradle-free)
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
- [ ] Keystore management
  - Debug keystore auto-generated on first build
  - Release keystore path configured in `project.json` or passed via CLI
- [ ] CLI interface
  ```bash
  sama-build android --tier mid --keystore release.jks --output MyGame.apk
  sama-build android --tier high --debug --install  # build + adb install
  ```
- [ ] `adb install` integration for quick deploy to connected device
- [ ] Build time optimization
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

## Phase H — AAB for Play Store

**Effort:** Low | **Dependencies:** Phase F

- [ ] Generate Android App Bundle (`.aab`) instead of APK
  - Uses `bundletool` to create the AAB from the same compiled resources
  - Play Store handles per-device APK generation
- [ ] Split APKs per ABI
  - `arm64-v8a` and `armeabi-v7a` as separate config splits
  - Reduces per-device download size
- [ ] Asset packs for large games (>150MB base APK limit)
  - `install-time` asset pack for core assets
  - `fast-follow` pack for optional content (additional levels, HD textures)
  - Uses Play Asset Delivery API
- [ ] Play Console metadata
  - Generate `output-metadata.json` compatible with Play Console upload

---

## Key Design Decisions

- **Vulkan only, no GLES fallback.** Google has required Vulkan 1.1 for all new devices since Android 10 (2019) and Vulkan 1.3 since Android 14 (2023). As of 2026, Vulkan coverage is ~95%+ across active devices: 100% high-end (2022+), ~98% mid-range (2020+), ~85-90% low-end (2019+). The remaining ~5% are pre-2019 budget devices — a shrinking tail not worth the complexity of maintaining a GLES backend. If GLES fallback is ever needed, bgfx makes it trivial (change `RendererType::Vulkan` to `RendererType::OpenGLES`), but we don't plan for it.

- **NativeActivity, not GameActivity.** NativeActivity is simpler (no Java/Kotlin boilerplate), sufficient for games, and avoids the complexity of JNI bridging. Switch to GameActivity only if we need system UI integration (notifications, in-app purchases, etc.).

- **Gradle-free build.** Direct use of `aapt2` + `zipalign` + `apksigner` keeps the build fast and avoids the 30+ second Gradle startup tax. The tradeoff is we must manage resource compilation ourselves, but games have minimal Android resources (just an icon and manifest).

- **ASTC texture compression.** ASTC is universally supported on all Vulkan-capable Android devices. It offers better quality-per-bit than ETC2 and supports all block sizes from 4x4 (highest quality) to 12x12 (smallest size). The `astcenc` encoder is already in third_party.

- **Tier system over automatic quality scaling.** Auto-detecting the right quality level at runtime is unreliable (GPU model strings are inconsistent, available memory varies with background apps). Explicit tiers let developers test each configuration and guarantee performance. Runtime detection can be added later as a hint for the default tier selection.

- **No hot-reload on Android.** Desktop development uses the editor for rapid iteration. Android builds are for testing and release. Hot-reload would require a TCP bridge and asset streaming protocol — high complexity, low value when the editor already provides instant feedback.
