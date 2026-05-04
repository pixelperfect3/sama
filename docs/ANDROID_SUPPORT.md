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
  - Swapchain `formatColor` is now picked dynamically by a pre-init Vulkan surface-format probe (`engine/rendering/AndroidVulkanFormatProbe.{h,cpp}`) — `dlopen`s libvulkan, walks `vkGetPhysicalDeviceSurfaceFormatsKHR` results against an `RGBA8 -> BGRA8 -> RGB10A2` priority list, falls back to RGBA8 (the Android CDD baseline) on any failure.  See "Vulkan Surface Format" below for the full write-up.
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
- [x] Lifecycle handling
  - `APP_CMD_PAUSE` / `APP_CMD_RESUME` handled in `Engine::handleAndroidCmd`. On pause: SoLoud is paused via `IAudioEngine::setPauseAll(true)`, gyro polling is disabled (battery), and a `paused_` flag flips `beginFrame()`'s `ALooper_pollAll` from non-blocking to blocking (`-1`) so the OS can suspend the thread. On resume: gyro re-enabled, audio unpaused, polling resumes normal cadence.
  - `APP_CMD_TERM_WINDOW` clears the cached `ANativeWindow*` so `beginFrame()` skips its bgfx-touching path until `APP_CMD_INIT_WINDOW` rebinds a fresh surface (Vulkan's `VkSurfaceKHR` is tied to the previous `ANativeWindow*`; rendering past `TERM_WINDOW` would crash inside `vkAcquireNextImageKHR`). On `APP_CMD_INIT_WINDOW` after a resume, `bgfx::setPlatformData` + `Renderer::resize` re-bind the new handle without a full bgfx re-init.
  - **Known limitation:** `onSaveInstanceState` / `onRestoreInstanceState` not implemented — those are Java-only callbacks that NativeActivity does not surface to native code. Games that need cross-launch state should persist their own data via `getExternalFilesDir()` (path passed in via `android_app::activity->externalDataPath`).
  - Verified on `sama_mid` AVD (Android 13, arm64-v8a): clean PAUSE → LOST_FOCUS → TERM_WINDOW sequence on Home; clean RESUME → INIT_WINDOW → "Window recreated: 1080x2400 — resetting bgfx" → GAINED_FOCUS sequence on re-foreground; rendering and gyro resume without crash.
- [x] Audio backend
  - `Engine::initAndroid` constructs a `SoLoudAudioEngine` using SoLoud's miniaudio backend. miniaudio's NULL-context init auto-selects **AAudio** (API 26+, lower latency, modern) and falls back to **OpenSL ES** on older devices. Both are `dlopen()`'d at runtime (`MA_NO_RUNTIME_LINKING` is not defined) so no compile-time link to `libaaudio.so` / `libOpenSLES.so` is needed. AAudio output requires no manifest permission.
  - Engine now owns `audio_` on Android (mirrors iOS): `engine.audio()` returns an `IAudioEngine&`. `engine_audio` is linked into `engine_core` for `SAMA_ANDROID`. On audio init failure (e.g. emulator without an audio route) we fall back to `NullAudioEngine` so games can call `engine.audio()` unconditionally.
  - New `IAudioEngine::setPauseAll(bool)` method drives the lifecycle pause/resume — implemented as `SoLoud::Soloud::setPauseAll` for the real engine, no-op for `NullAudioEngine`.
  - Smoke test: `apps/android_test/AndroidTestGame.cpp` generates a procedural 440Hz/0.3s WAV in `onInit`, loads it through SoLoud, and plays it once at startup; every new touch retriggers the clip. Verified on `sama_mid` AVD — logcat shows `Audio: SoLoud (miniaudio) initialised` and `audio: init-time play handle=16389`.

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
  - Visual overlay: `engine::ui::renderVirtualJoystick(joy, drawList, screenW, screenH, cfg)` in `engine/ui/VirtualJoystickRenderer.h`. Free function so `VirtualJoystick` stays UI-free; renders the base disk, optional dead-zone ring, and stick disk into any `UiDrawList` using the existing rounded-rect SDF (no new UiDrawList primitives). Wired into `apps/android_test/AndroidTestGame.cpp` as a smoke test — verified on `sama_mid` AVD (1080x2400, Pixel 6, API 33): overlay appears in the lower-left corner and the white stick disk follows finger drag within the base disk.
- [x] Multi-touch gestures
  - `engine::input::GestureRecognizer` (cross-platform, in `engine/input/`) reads `InputState::touches()` each frame and emits per-frame pinch (distance delta) + two-finger pan (midpoint delta) for the lowest two stable touch IDs.
  - Pinch-to-zoom: `gesture.pinchDelta` in pixels per frame — positive = fingers spreading.
  - Two-finger pan: `gesture.panDeltaX/Y` in pixels per frame.
  - Re-anchors without spike when one tracked finger lifts and another lands (touch-id swap), and treats `Phase::Ended` touches as gone for selection.
  - Cross-platform — same recognizer works on iOS once the iOS touch backend is wired (future task).
  - Source: `engine/input/GestureRecognizer.{h,cpp}`
  - Tests: `tests/input/TestGestureRecognizer.cpp` (12 cases / 62 assertions)
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

## Phase D — Asset Pipeline CLI (MOSTLY DONE — 2 items deferred)

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
- [x] **ASTC encoding** via ARM's `astcenc` library (FetchContent at configure time). Architecture: `tools/asset_tool/AstcEncoder.cpp` wraps `astcenc_compress_image`, built into the separate `engine_astcenc_bridge` object library and linked only into `sama_asset_tool` (the CLI binary). `engine_asset_tool` (the lib used by `engine_tests`) instead links `AstcEncoderStub.cpp`, which exposes a function-pointer registration so the real encoder self-registers via static-init when the bridge is linked. This split avoids astc-codec symbol collisions with bgfx's bundled (decode-only) `astc-codec` inside the test binary. Verified end-to-end by `ios/smoke_asset_tool.sh`: produces real ASTC bytes at low/mid/high tiers (8x8 / 6x6 / 4x4) and validates the KTX `glInternalFormat` against ARM's reference format codes (`0x000093b7`, `0x000093b4`, `0x000093b0`).

### Deferred (Phase D)

- [ ] Mesh processing — LOD generation, vertex cache optimization. Today the pipeline ships meshes through unchanged.
- [ ] Audio transcoding — WAV to Opus. Today the pipeline copies audio sources unchanged. Both items are tracked in **Remaining Limitations** above.

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
- [x] Runtime tier detection: auto-select tier based on GPU model / available memory
  - `engine/platform/android/AndroidTierDetect.{h,cpp}` — case-insensitive GPU substring match (Adreno 4-7xx, Mali-T8/G3/G5/G6/G7xx, Immortalis, Xclipse 9xx, PowerVR — full table is the header comment in `AndroidTierDetect.cpp`) plus a RAM heuristic from `/proc/meminfo`'s `MemTotal` field (`<3 GB` Low, `3-5 GB` Mid, `>=5 GB` High). The Low cutoff matches iOS (`<3 GB`); the High cutoff is `5 GB` on Android vs `6 GB` on iOS — see `AndroidTierDetect.cpp` for the rationale (most Android flagships ship 6/8/12 GB, mid-range ships 4 GB, so 5 GB cleanly splits the two).
  - Engine logs the detected tier from `Engine::initAndroid` so the choice is visible in logcat ("Tier detected: <Low|Mid|High|Unknown> (RAM N MB)").
  - `GameRunner::runAndroid(configPath)` substitutes the detected tier name into `ProjectConfig::activeTier` ONLY when the project did not specify one OR set the new `"activeTier": "auto"` sentinel — explicit `"low"` / `"mid"` / `"high"` / custom values are preserved as-is.
  - Combination logic (full rationale in the implementation file's header comment): agreeing GPU+RAM signals reinforce; disagreeing signals fall back to Mid (conservative); `RAM=0` trusts the GPU class; no signal at all returns `Unknown` and maps to `"mid"`.
  - Tests: `tests/platform/TestAndroidTierDetect.cpp` (17 cases / 88 assertions covering substring matching, /proc/meminfo parsing, combination logic boundaries, and string helpers — runs on the macOS host build).
  - Verified on the `sama_mid` AVD (Pixel 6, API 33): logcat shows `Tier detected: Low (RAM 1965 MB)` because the running emulator only reports ~2 GB to the guest (the AVD is provisioned for 4 GB but the kernel sees less).
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
  ./android/build_apk.sh --tier low --keystore release.jks   # release signing (interactive password)
  ./android/build_apk.sh --keystore release.jks --ks-pass 'hunter2' --ks-key-alias mykey  # CI: literal password
  ./android/build_apk.sh --keystore release.jks --ks-pass-env SAMA_KS_PASS                # CI: env var (preferred)
  ./android/build_apk.sh --app-name "My Game" --package com.mygame.app --output MyGame.apk
  ```
- [x] `adb install` integration via `--install` flag for quick deploy to connected device
- [x] Build time optimization
  - Incremental native builds (CMake only recompiles changed sources)
  - Intermediate files cleaned after APK generation (base.apk, unsigned.apk, aligned.apk)
- [x] Vulkan feature filter: AndroidManifest declares the canonical pair
  `android.hardware.vulkan.level` (version=1) and
  `android.hardware.vulkan.version` (version=0x00401000 = Vulkan 1.1).
  Picked up by Play Store device-eligibility filters; bare
  `android.hardware.vulkan` was non-canonical.

### Resolved Issues (Phase F)

The following issues were noted during Phase F and have since been fixed:

- **Non-interactive keystore signing:** Added `--ks-pass`, `--ks-pass-env`,
  `--ks-key-alias`, `--key-pass`, and `--key-pass-env` to both
  `build_apk.sh` (apksigner) and `build_aab.sh` (jarsigner). Falls back
  to interactive prompt when `--keystore` is supplied without a password
  source, preserving the legacy behavior. `--ks-pass-env` is preferred in
  CI to keep secrets out of `ps -ef` and shell history.
- **Stale staging directory:** Both scripts now wipe their staging
  directory at the start of asset processing (`[stage] Cleaning staging
  directory: …`). Use `--no-clean-staging` for fast local iteration.
- **AndroidManifest.xml Vulkan feature:** Replaced the bare
  `android.hardware.vulkan` feature with the canonical
  `android.hardware.vulkan.level` (version=1) + `android.hardware.vulkan.version`
  (version=0x00401000 / Vulkan 1.1) pair.

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
- [x] Build progress indicator
  - Status bar pinned to the bottom of the editor window: spinning
    `NSProgressIndicator`, phase label parsed from `build_apk.sh`'s
    `[N/7]` markers (`[1/7] Building native library...`, etc.), and a
    Cancel button that `SIGTERM`s the running build process.
  - Idle state shows "Ready" with the spinner hidden; the final status
    reads "Build succeeded (14.07 MB)" / "Build failed (exit N)" /
    "Build cancelled" so the result is visible without opening the
    Console panel.
  - Build process spawned via `posix_spawn` so the editor can hold the
    PID for cancel; previously `popen()` had no portable PID handle.
- [x] Auto-install option ("Build & Run")
  - `Build > Android > Build & Run` (Cmd+R) invokes
    `build_apk.sh --tier <persisted> --debug --install`, then
    `adb shell am start -n <package>/android.app.NativeActivity`
    against the persisted device serial (or first connected).
  - Pre-flight `adb devices -l` check shows an alert with install
    instructions if no device is plugged in / no emulator is running,
    so the user doesn't waste 2 minutes building before noticing.
  - Disabled (warning logged) while another build is in flight; uses
    a single `androidBuildRunning` atomic flag.
- [x] Build configuration persistence
  - `Build > Android > Settings…` opens a modal sheet for default tier,
    keystore path, keystore password env var (recommended over plain
    text), APK output path, package ID, last device serial, and a
    "Build & Run after build" toggle.
  - Persisted to `NSUserDefaults` under keys `android.defaultTier`,
    `android.keystorePath`, `android.keystorePassEnvVar`,
    `android.outputApkPath`, `android.packageId`,
    `android.lastDeviceSerial`, `android.buildAndRun`. Defaults applied
    on first launch (`tier=mid`, `package=com.sama.game`, output empty
    = `build/android/Game.apk`).
  - Settings flow into every build invocation: per-tier menu items,
    Build & Run, and the cancel-aware build thread all read the
    persisted snapshot at spawn time. When the "Build & Run after
    build" toggle is on, the per-tier menu items also auto-install +
    launch on success — so the user can pick `Build > Android > Mid`
    and have it behave like Build & Run without using Cmd+R.

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
- [x] **Install-time asset packs** for games over the 150 MB base APK soft cap
  - `--asset-pack name:source-dir` flag on `build_aab.sh`, repeatable
  - Each pack is staged as its own bundle module with a
    `dist:type="asset-pack"` + `<dist:install-time/>` manifest, aapt2-linked
    into proto format, zipped, and passed to `bundletool build-bundle`
    alongside `base.zip` via `--modules=base.zip,pack1.zip,pack2.zip`
  - Pack assets land at `assets/<name>/` on the device after Play Store
    install — same path the runtime `AAssetManager` already reads, **no
    engine code change required**
  - Validation up front: pack name is a valid split id, isn't `base`, no
    duplicates, source dir exists, per-pack uncompressed size ≤ 1.5 GiB
    (Play Store hard limit), warning if total install-time delivery
    exceeds 4 GiB (Play Console will reject the upload)
  - Example invocation:
    ```bash
    ./android/build_aab.sh --tier high \
        --asset-pack audio:assets/audio \
        --asset-pack hd_textures:assets/textures/high \
        --keystore release.jks --output MyGame.aab
    ```
- [x] **Play Console `output-metadata.json` generation**
  - `--metadata` flag writes the file the Android Gradle Plugin emits and
    that the Play Developer API + Fastlane's `supply` action consume
  - `applicationId` from `--package`, `versionCode` and `versionName`
    from `android/AndroidManifest.xml`; defaults to `1` and `"1.0"`
    with a stderr warning when missing

### Known limitation: dynamic asset pack delivery

Only **install-time** packs are supported. The two dynamic delivery modes
that Play Asset Delivery offers — **fast-follow** (downloaded right after
install completes, but in the background) and **on-demand** (downloaded
when the game requests them) — both require Google Play Core's
`AssetPackManager`, which is a Java-only API. Tracking download state,
requesting cellular vs Wi-Fi delivery, surfacing user consent prompts,
and pausing/resuming downloads all require calls into Play Core from the
running app.

Sama uses pure NativeActivity (see "NativeActivity, not GameActivity" in
**Key Design Decisions** below), so adopting Play Core would mean adding
a JNI bridge and shipping a Java half of the application. That's a
deliberate non-goal of this round — the build pipeline now generates a
correct install-time AAB, which is enough for any game under the per-pack
1.5 GiB / per-bundle 4 GiB Play Store install-time delivery limits.

**Workaround for game devs who need >150 MB total:** use install-time
packs (the `--asset-pack` flag above). They're delivered with the base
APK during the user's first install, no runtime code change is required,
and from the engine's perspective the assets just appear at the same
`assets/...` paths the existing loaders already read.

**For very large games (>1.5 GiB per pack or >4 GiB total install-time):**
the workaround stops scaling. Either split the game so the install-time
delivery fits inside 4 GiB total, or pick up the deferred work below.

**If/when fast-follow / on-demand is needed,** the path is roughly:
1. Add a thin Kotlin/Java wrapper activity that owns Play Core and
   forwards lifecycle to NativeActivity (or convert to GameActivity,
   which already has Play Core integration patterns).
2. JNI-bridge `AssetPackManager.requestPackStates`, `fetch`, `getPackLocation`
   into a new `engine::platform::android::AssetPackManager` C++ wrapper.
3. Extend `AndroidFileSystem` to consult the manager's resolved pack
   paths before falling back to the base APK's `assets/`.
4. Surface download progress and error states via a new `IGame`
   callback so games can render their own loading screen.

---

## Cross-Platform Game Runner (The Missing Piece)

With the completion of the Android game runner, **IGame implementations now work identically on desktop and Android with zero platform-specific code.** This was the key integration that ties all the Android phases together -- previously the platform layer, input, and asset pipeline existed but there was no way to actually run a game on Android using the same code as desktop.

### What changed

1. **`Engine` works on both platforms.** The public API (`resources()`, `inputState()`, `beginFrame()`/`endFrame()`, shader programs, framebuffer dimensions) is identical. Platform differences are hidden behind `#ifdef __ANDROID__` in the implementation:
   - Desktop: GLFW window, ImGui, mouse/keyboard input
   - Android: `ANativeWindow`, touch/gyro input, ImGui via the bgfx examples/common/imgui wrapper (see "ImGui on Android" below)

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

The `android_test` app runs at 60fps with the full stack verified: **Vulkan + SPIRV shaders + PBR (lit DamagedHelmet) + cast shadows + UiRenderer + BitmapFont text rendering + gyroscope + touch input.** Shader programs load from APK assets via `AndroidFileSystem`; the rendering path is the same Engine API that desktop demos use.

Ongoing emulator coverage on the `sama_mid` AVD (Pixel 6, API 33, 1080x2400) catches regressions during Phase B–H bring-up: SoLoud + miniaudio init (`Audio: SoLoud (miniaudio) initialised`), lifecycle pause/resume on Home + re-foreground, runtime tier detection (`Tier detected: Low (RAM 1965 MB)` — the AVD only surfaces ~2 GB to the guest), virtual joystick overlay drag, GestureRecognizer pinch/pan demo, and the ImGui smoke window with tap-driven button. The editor's `Build > Android > Settings…` dialog and Build & Run (Cmd+R) flow are exercised against the same AVD.

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

**Fix:** `Renderer::init()` calls `engine::rendering::probeAndroidSwapchainFormat(window)` (`engine/rendering/AndroidVulkanFormatProbe.{h,cpp}`) BEFORE `bgfx::init` runs. The probe `dlopen`s `libvulkan.so`, creates a temporary `VkInstance` + `VkSurfaceKHR`, calls `vkGetPhysicalDeviceSurfaceFormatsKHR`, and walks a fixed priority list (`RGBA8 -> BGRA8 -> RGB10A2 -> RGBA8 fallback`) to pick the best `bgfx::TextureFormat`. On any failure (Vulkan loader missing, surface creation fails, no supported formats), the probe returns `RGBA8` — the Android CDD-mandated baseline that every Vulkan device must support — so the function is strictly safer than the historical hardcoded path. The picked format is logged to logcat (`VulkanFormatProbe: N surface formats reported, picked <format>` and `Vulkan swapchain format: <format>`) so operators can confirm the right branch executed on a given device. The pure priority-list selector is host-testable in `tests/rendering/TestAndroidVulkanFormatProbe.cpp` (`[android][vulkan][format_probe]`, 8 cases).

**Why dynamic loading?** The probe `dlopen`s `libvulkan.so` rather than linking against it. A hypothetical future device that drops Vulkan support still loads the binary cleanly — the `dlopen` just fails and the probe returns the safe RGBA8 default. See `docs/NOTES.md` for the dynamic-loading vs link-time tradeoff write-up.

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

### Current State

**Working on Android (verified):**
- **PBR + shadows.** `ShaderLoader.cpp` has a full `#ifdef __ANDROID__` branch implementing every shader-loader function (`loadPbrProgram`, `loadShadowProgram`, `loadSkinnedPbrProgram`, `loadSkinnedShadowProgram`, `loadGizmoProgram`, `loadMsdfProgram`, `loadSkyboxProgram`, `loadSlugProgram`, `loadSpriteProgram`, `loadRoundedRectProgram`, `loadUnlitProgram`) by loading SPIRV `.bin` files from APK assets via `AndroidFileSystem`. `android_test` renders the lit DamagedHelmet with cast shadows on Pixel 9.
- **Post-process shaders available + exercised end-to-end.** `loadBloomThresholdProgram`, `loadBloomDownsampleProgram`, `loadBloomUpsampleProgram`, `loadTonemapProgram`, `loadFxaaProgram` are all implemented in the Android branch. `compile_shaders.sh --all` emits the SPIRV. Apps opt in to the post-process pipeline the same way as desktop demos: by calling `renderer().beginFrame()` + `renderer().postProcess().submit()` instead of using the Engine's default `beginFrameDirect()` path. `apps/android_test/AndroidTestGame.cpp` now exposes a tap-to-toggle (upper-right corner) that flips between the direct path and the opt-in post-process path — a small white-hot emissive cube parked next to the helmet acts as the regression-catch signal: any per-shader bloom / tonemap / FXAA regression on Android shows up as a missing or broken halo. Verified on the `sama_mid` AVD: visible scene difference between the two modes, toggle bidirectional, no helmet/shadow loss in the post path. Screenshots were captured to `/tmp/android_no_post.png` + `/tmp/android_post.png` for the in-flight verification (not committed; PNGs in `/tmp/` aren't portable artifacts).
- **SSAO available.** `SsaoSystem.cpp` calls `loadSsaoProgram()` on Android (which is implemented in the Android branch of `ShaderLoader.cpp`). It only returns early if the handle is invalid — same fallback as desktop's Noop renderer.
- **IBL.** `IblResources.cpp` is platform-agnostic; the same code path works on Android as desktop.
- **Audio (Phase B).** `Engine::initAndroid` constructs `SoLoudAudioEngine` with the miniaudio backend (auto-AAudio API 26+, OpenSL ES fallback). `engine.audio()` returns an `IAudioEngine&` mirroring iOS. Falls back to `NullAudioEngine` if the device has no audio route. Verified on `sama_mid` AVD: `Audio: SoLoud (miniaudio) initialised` in logcat.
- **Lifecycle pause/resume (Phase B).** `Engine::handleAndroidCmd` services `APP_CMD_PAUSE`/`RESUME` (audio paused via `IAudioEngine::setPauseAll`, gyro disabled, `ALooper_pollAll` flips to blocking) and `APP_CMD_TERM_WINDOW`/`INIT_WINDOW` (clears + re-binds `ANativeWindow*` so Vulkan doesn't crash inside `vkAcquireNextImageKHR`). Verified on `sama_mid` AVD: clean Home → re-foreground cycle without crash.
- **Multi-touch + gestures (Phase C).** `engine::input::GestureRecognizer` (cross-platform, `engine/input/`) emits per-frame pinch / two-finger pan from `InputState::touches()`, with re-anchor on touch-id swap. Wired into `apps/android_test/AndroidTestGame.cpp` as a smoke test.
- **Virtual joystick overlay (Phase C).** `engine::ui::renderVirtualJoystick(joy, drawList, screenW, screenH, cfg)` in `engine/ui/VirtualJoystickRenderer.h` draws the base disk, optional dead-zone ring, and stick disk into any `UiDrawList`. Verified on `sama_mid` AVD (1080x2400, Pixel 6, API 33).
- **ImGui on Android.** Engine wires `imguiCreate` / `imguiBeginFrame` / `imguiEndFrame` into the Android branch of `Engine.cpp`; touch events synthesise primary-mouse so dear-imgui widgets respond to taps. `Engine::imguiWantsMouse()` works the same as desktop, so games can gate touch handlers with `if (!engine.imguiWantsMouse()) { ... }`. SPIRV bytecode is embedded in the bgfx wrapper (no extra shader bundling). See "ImGui on Android (working)" below for details.
- **Runtime tier detection (Phase E).** `engine/platform/android/AndroidTierDetect.{h,cpp}::detectAndroidTier()` combines GPU substring matching (Adreno / Mali / Immortalis / Xclipse / PowerVR) with a `/proc/meminfo` `MemTotal` heuristic (`<3 GB` Low, `3-5 GB` Mid, `≥5 GB` High). `GameRunner::runAndroid` substitutes the detected tier into `ProjectConfig::activeTier` when the project sets `"activeTier": "auto"` (or leaves it empty); explicit tier names are preserved. Verified on `sama_mid` AVD: logcat shows `Tier detected: Low (RAM 1965 MB)` because the AVD only surfaces ~2 GB to the guest.
- **Install-time asset packs + Play Console metadata (Phase H).** `android/build_aab.sh --asset-pack name:source-dir` (repeatable) stages each pack as a bundle module with `dist:type="asset-pack"` + `<dist:install-time/>`, validated for split-id correctness, per-pack ≤ 1.5 GiB and total ≤ 4 GiB Play Store limits. `--metadata` writes `output-metadata.json` (versionCode/versionName parsed from `AndroidManifest.xml`) for the Play Developer API and Fastlane `supply`. Pack assets land at `assets/<name>/` post-install — no engine code change required.

### Remaining Limitations

- [ ] **`onSaveInstanceState` / `onRestoreInstanceState` not surfaced by NativeActivity.** Documented limitation; games that need cross-launch state should persist their own data via `android_app::activity->externalDataPath` (which maps to `getExternalFilesDir()`).
- [ ] **Mesh LOD generation and audio transcoding (WAV → Opus).** Phase D nice-to-haves; deferred. Today the asset pipeline runs textures + shaders only.
- [ ] **Fast-follow / on-demand asset packs.** Only **install-time** packs are supported. Dynamic delivery requires a JNI bridge to Google Play Core's `AssetPackManager`, which would mean shipping a Kotlin/Java half of the application. See "Known limitation: dynamic asset pack delivery" under Phase H for the full path.
- [ ] **`ProjectConfig::loadFromFile` on Android uses raw `fopen()`.** It cannot read APK assets, so `apps/android_test/project.json` (the Phase E gap-fill template) is bundled but not loaded at runtime. The empty-`activeTier` → auto-detect fallback works regardless. Wiring `runAndroid(app, configPath)` through `AndroidFileSystem` and calling `ProjectConfig::loadFromString` is the follow-up.
- [ ] **iOS ImGui not wired yet.** Engine still skips ImGui on iOS. The path is: plumb iOS touch input similarly to Android, verify `engine_debug` cross-compiles for iOS, confirm Metal `vs_ocornut_imgui_mtl` bytecode is present (it is). Future task — out of scope for the Android round.

### ImGui on Android (working)

ImGui (the bgfx examples/common/imgui wrapper around dear-imgui) runs on Android via the same engine API as desktop:

- `Engine::initAndroid()` calls `imguiCreate(16.f)` after the renderer is up. The wrapper's static context allocates its bgfx programs, vertex layout, and a font atlas texture.
- `Engine::beginFrame()` (Android branch in `engine/core/Engine.cpp`) calls `imguiBeginFrame(mx, my, buttons, scroll=0, fbW, fbH, -1, kViewImGui)` with the synthesized primary touch from `AndroidInputBackend` mapped to `IMGUI_MBUT_LEFT`.
- `Engine::endFrame()` calls `imguiEndFrame()` which submits the dear-imgui draw lists into `kViewImGui` (view 15).
- `Engine::imguiWantsMouse()` returns `ImGui::GetIO().WantCaptureMouse`, the same as desktop, so games can `if (!engine.imguiWantsMouse()) { ... }` to gate their own touch handlers.

No extra shader bundling step is needed: the bgfx imgui wrapper embeds SPIRV (and ESSL/GLSL/Metal) bytecode for `vs_ocornut_imgui` / `fs_ocornut_imgui` / `vs_imgui_image` / `fs_imgui_image` via `BGFX_EMBEDDED_SHADER`, and `bgfx::createEmbeddedShader` picks the SPIRV variant at runtime when the active renderer is Vulkan.

Smoke tested in `apps/android_test/AndroidTestGame.cpp` — renders a window with a frame counter and a "Press me" button; tapping the button increments a counter and logs to logcat. The bgfx imgui wrapper compiles cleanly for `SAMA_ANDROID` (already linked into `engine_core` via `engine_debug`) — the only change required was wiring the `imguiCreate / imguiBeginFrame / imguiEndFrame / imguiDestroy` calls into the Android branch of `Engine.cpp` (`#elif defined(__ANDROID__)`). The iOS branch (`#else`) does not call them yet; when iOS imgui lands it will mirror Android by editing that branch directly.

iOS imgui wiring is still pending (separate follow-up; would need iOS-side touch → ImGui IO plumbing).

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

- **Tier system over automatic quality scaling, with opt-in runtime detection.** Auto-detecting the right quality level at runtime is unreliable (GPU model strings are inconsistent, available memory varies with background apps). Explicit tiers let developers test each configuration and guarantee performance. As of Phase E, runtime detection is wired in as an opt-in hint: a project can set `"activeTier": "auto"` (or leave it empty) and `GameRunner::runAndroid` will substitute the result of `AndroidTierDetect::detectAndroidTier()` before constructing `EngineDesc`. Explicit tier names in `project.json` always win, so games that have validated a specific tier on every target device can keep their pinned configuration.

- **No hot-reload on Android.** Desktop development uses the editor for rapid iteration. Android builds are for testing and release. Hot-reload would require a TCP bridge and asset streaming protocol — high complexity, low value when the editor already provides instant feedback.

- **Single Engine class with `#ifdef` vs EngineAndroid subclass.** We chose `#ifdef __ANDROID__` inside one `Engine` class rather than an inheritance hierarchy (`EngineDesktop` / `EngineAndroid`). The public API is identical on both platforms -- only the init path, window management, and input backend differ. A subclass design would force game code to use `Engine&` references everywhere (which they already do), but would also fragment the implementation across two files with duplicated frame logic. The `#ifdef` approach keeps all frame lifecycle code in one place, and the compile-time branching means zero runtime cost.

- **SPIRV shaders loaded from APK assets at runtime.** The alternative was embedding shader binaries directly into the `.so` via `xxd` or `#include` of binary arrays. We chose APK asset loading because: (1) it keeps the shared library small and avoids bloating the text segment, (2) shaders can be updated without recompiling native code, (3) it mirrors the desktop pattern where shaders are loaded from the filesystem, and (4) `AAssetManager` provides zero-copy access which is as fast as embedded data. The tradeoff is a dependency on the asset packaging step in `build_apk.sh`, but this is already required for textures and models.

- **bgfx dependency: official bkaradzic/bgfx.cmake.** We use the official bgfx CMake wrapper (`bkaradzic/bgfx.cmake`) rather than the stale `widberg/bgfx.cmake` fork. The official repo includes the swapchain image count fix (`kMaxBackBuffers = max(BGFX_CONFIG_MAX_BACK_BUFFERS, 10)`), eliminating the need for our old CMake patch. The bgfx.cmake update also required adapting to API changes: `shaderc_parse` → `_bgfx_shaderc_parse`, `mtxFromCols3` → `mtxFromCols`, `instMul` → `mul`, ImGui `KeyMap`/`KeysDown` → `AddKeyEvent`, and disabling WGSL shader support (`BGFX_PLATFORM_SUPPORTS_WGSL=0`).

- **`samaCreateGame()` extern linkage vs registration.** The Android entry point uses `extern engine::game::IGame* samaCreateGame()` -- a function the game defines and the engine calls. The alternative was a registration pattern (e.g., `REGISTER_GAME(MyGame)` macro expanding to a static initializer). We chose extern linkage because: (1) it is explicit and easy to understand, (2) there is exactly one game per APK so a registry is unnecessary, (3) static initialization order issues are avoided entirely, and (4) the pattern is familiar from `main()` itself.

- **Shared `runLoop()` factored out.** `GameRunner` has platform-specific entry points (`run()` and `runAndroid()`) but the actual frame loop -- fixed-timestep accumulator, `IGame` callback dispatch, `beginFrame`/`endFrame` -- lives in a single `runLoop(Engine&)` method. This guarantees that desktop and Android have identical frame timing behavior and that bug fixes to the loop apply to both platforms.
