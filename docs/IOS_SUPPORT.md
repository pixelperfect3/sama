# iOS Export — Architecture & Roadmap

This document tracks the design and implementation of iOS IPA export for the Sama engine, supporting low/mid/high-end device tiers with appropriate asset quality.

---

## Overview

```
Game Project
  assets/                     Source assets (full quality)
  project.json                ProjectConfig with per-tier settings
  build/
    ios/
      low/   -> IPA (~25MB,  512px textures, minimal effects)
      mid/   -> IPA (~50MB,  1024px textures, standard PBR)
      high/  -> IPA (~100MB, 2048px textures, full PBR + IBL + SSAO)
    desktop/
    android/
```

bgfx supports Metal on iOS. The renderer requires zero shader changes — bgfx's `shaderc` compiles to Metal shading language. All Apple GPUs are TBDR (tile-based deferred rendering), so depth prepass is disabled by default.

---

## Device Tier Classification

### Low Tier — 30 FPS, 512px textures, no SSAO/IBL

| Device | Chip | GPU | Metal |
|--------|------|-----|-------|
| iPhone 7 / 7 Plus | A10 Fusion | GT7600 Plus | Metal 2 |
| iPhone SE 2nd gen | A13 Bionic | Apple GPU (4-core) | Metal 3 |
| iPad 6th gen | A10 Fusion | GT7600 Plus | Metal 2 |
| iPod Touch 7th gen | A10 Fusion | GT7600 Plus | Metal 2 |

### Mid Tier — 30 FPS, 1024px textures, IBL + bloom, no SSAO

| Device | Chip | GPU | Metal |
|--------|------|-----|-------|
| iPhone 11 / 11 Pro | A13 Bionic | Apple GPU (4-core) | Metal 3 |
| iPhone 12 / 12 mini | A14 Bionic | Apple GPU (4-core) | Metal 3 |
| iPhone SE 3rd gen | A15 Bionic | Apple GPU (4/5-core) | Metal 3 |
| iPad Air 4th gen | A14 Bionic | Apple GPU (4-core) | Metal 3 |
| iPad 9th gen | A13 Bionic | Apple GPU (4-core) | Metal 3 |

### High Tier — 60 FPS, 2048px textures, full PBR + IBL + SSAO + bloom

| Device | Chip | GPU | Metal |
|--------|------|-----|-------|
| iPhone 14 Pro / 15 | A16 / A17 Pro | Apple GPU (5/6-core) | Metal 3 |
| iPhone 16 / 16 Pro | A18 / A18 Pro | Apple GPU (5/6-core) | Metal 3 |
| iPad Pro M1/M2/M4 | M-series | Apple GPU (8-10 core) | Metal 3 |
| iPad Air M2 | M2 | Apple GPU (10-core) | Metal 3 |
| iPad mini 7th gen | A17 Pro | Apple GPU (5-core) | Metal 3 |

---

## Render Settings Per Tier

| Setting | Low | Mid | High |
|---------|-----|-----|------|
| Max texture size | 512 | 1024 | 2048 |
| Texture compression | ASTC 8x8 | ASTC 6x6 | ASTC 4x4 |
| Shadow map size | 512 | 1024 | 2048 |
| Shadow cascades | 1 | 2 | 3 |
| Shadow filter | Hard | PCF4x4 | PCF4x4 |
| Max bones | 64 | 128 | 128 |
| IBL | Off | On | On |
| SSAO | Off | Off | On |
| Bloom | Off | On | On |
| FXAA | On | On | On |
| Depth prepass | Off (TBDR) | Off (TBDR) | Off (TBDR) |
| Target FPS | 30 | 30 | 60 |
| Render scale | 0.75 | 1.0 | 1.0 |

---

## iOS-Specific Considerations

- **Metal only.** All iOS devices since iPhone 5s support Metal. bgfx handles Metal backend selection automatically. No OpenGL ES fallback needed.

- **TBDR architecture on ALL Apple GPUs.** Depth prepass is always disabled (`depthPrepassEnabled = false`). This avoids the double vertex processing penalty that TBDR GPUs pay for prepass. Alpha-tested depth prepass (`depthPrepassAlphaTestedOnly`) is safe and can be enabled for foliage-heavy scenes.

- **ASTC universally supported.** All Metal-capable devices support ASTC texture compression. Same block sizes as Android (4x4/6x6/8x8).

- **Tier detection is reliable.** Unlike Android (inconsistent GPU model strings), iOS devices can be identified precisely via `[UIDevice currentDevice].model` + `NSProcessInfo.processInfo.physicalMemory`. A lookup table of chip→tier is sufficient and stable (Apple releases ~3 chips/year).

- **No sideloading without jailbreak.** Apps must be signed and distributed via App Store, TestFlight, or enterprise certificate. The build pipeline must produce a signed IPA.

---

## Implementation Phases

### Phase A — Xcode Project / CMake Integration ✅
- [x] CMake toolchain for iOS cross-compilation (`cmake/ios.toolchain.cmake`, `SAMA_IOS=ON`)
- [x] Xcode project generation via `cmake -G Xcode -DSAMA_IOS=ON` (wrapped in `ios/build_ios.sh`)
- [x] Minimum deployment target: iOS 15.0 (covers 95%+ of active devices)
- [x] Universal binary (arm64 device + arm64/x86_64 simulator)
- [x] Info.plist template with bundle ID, version, orientation, etc. (`ios/Info.plist.in`)
- [x] bgfx Metal backend verification on iOS Simulator — purple clear → full PBR scene with helmet + shadows on iPhone 15 sim at ~67 fps

### Phase B — iOS Platform Layer ✅
- [x] `IosFileSystem` — reads from app bundle (`NSBundle.mainBundle.resourcePath`); absolute-path fallback
- [x] `IosWindow` — `UIWindow` + `CAMetalLayer`-backed `_SamaMetalView`; tracks `drawableSize = bounds × contentScaleFactor`
- [x] `IosTouchInput` — `UITouch` overlay; multitouch slot-id reuse; first-touch-as-mouse mapping
- [x] `IosGyro` — `CMMotionManager` `startDeviceMotionUpdatesUsingReferenceFrame:`; pull model (one sample per frame); zero on simulator (no IMU available)
- [x] App lifecycle — `UIApplicationDelegate` (`_SamaAppDelegate`) with `CADisplayLink`; `Engine::initIos` + `GameRunner::{runIos,tickIos,shutdownIos}` wired through; pause on resignActive, resume on becomeActive
- [x] `engine_rendering` builds for iOS — Metal-only shader headers retained, ESSL/GLSL/SPIRV gated out with `#if !TARGET_OS_IPHONE`; `BGFX_PLATFORM_SUPPORTS_ESSL=0` set on `engine_rendering`
- [x] Sample app `apps/ios_test` mirrors `apps/android_test`: helmet scene, touch trail, multi-touch HUD, gyro hue cycling
- [x] Audio: SoLoud + miniaudio CoreAudio backend wired through `Engine::initIos`; falls back to `NullAudioEngine` if SoLoud init fails. `soloud_miniaudio.cpp` compiled as Obj-C++ on iOS (miniaudio's CoreAudio path uses `AVAudioSession` + `@autoreleasepool`).

#### Hi-DPI verified, no fix needed
- Earlier worry that the simulator screenshot showed the scene tiling vertically turned out to be a misread of the framing: the gray band in the middle is the (small) ground plane's top surface, and the purple band below it is the sky visible *past* the back edge of the ground. Pixel-strip diff of the framebuffer (`/tmp/ios_hidpi_fixed.png`) confirms exactly one HUD, one helmet, one ground. bgfx's swap-chain is at the full retina resolution (`1179×2556` on iPhone 15) per the `IosWindow::updateSize` log + the bgfx Metal backend's `SwapChainMtl::resize` call to `setDrawableSize` — no `BGFX_RESET_HIDPI` is needed (Metal lacks that capability flag entirely). If a future demo wants more visible ground, scale the ground entity up rather than touching the renderer.

### Phase C — Asset Pipeline for iOS
- [x] Shader compilation to Metal shading language via bgfx `shaderc` — Metal-only `*_mtl.bin.h` generated for iOS via host-built `shaderc`; `BGFX_CONFIG_MAX_BONES=128` propagated to shader compile
- [x] App-bundle asset packaging (primitive) — `cmake/SamaIosAssets.cmake` exposes `sama_ios_bundle_assets(TARGET ASSETS_ROOT ASSETS ...)` which takes a list of asset paths relative to `assets/`, validates each at configure time, calls `target_sources()` so Xcode tracks edits, and sets `MACOSX_PACKAGE_LOCATION` per file to preserve subdirectories (e.g. `fonts/JetBrainsMono-msdf.png` lands at `Resources/fonts/JetBrainsMono-msdf.png`).
- [x] JSON-driven asset manifest — same module exposes `sama_ios_bundle_assets_from_manifest(TARGET ASSETS_ROOT MANIFEST [TIERS])` which parses a `project.json` `assets` block (`common` + `low`/`mid`/`high` arrays) at configure time using CMake 3.20's `string(JSON ...)`, computes the union of `common` + selected tiers (default: all three), deduplicates, and forwards the resulting list to the unchanged primitive.  `apps/ios_test/project.json` is the live consumer; all tiers bundled into one `.app` (per-tier IPA splitting deferred to Phase D — see `docs/NOTES.md`).
- [x] `sama-asset-tool --target ios` support — `--target ios` was already wired through `ShaderProcessor` (Metal output, `metal` profile) and `TextureProcessor` (target-agnostic, ASTC encoder).  Smoke-tested at all three tiers: `tools/asset_tool/main.cpp` accepts `ios|android|desktop`; the manifest is tagged `platform: ios`; KTX `glInternalFormat` matches expected ASTC block size.  Catch2 coverage in `tests/tools/TestAssetProcessor.cpp` (tag `[asset_tool][ios]`); on-disk smoke script in `ios/smoke_asset_tool.sh` exercises the real encoder via the built `sama_asset_tool` binary.
- [x] ASTC compression (same as Android — shared code) — `tools/asset_tool/TextureProcessor` + `AstcEncoder` are platform-agnostic; iOS uses the same encoder with the same per-tier block sizes (4x4 high, 6x6 mid, 8x8 low).
- [x] Tier detection — `engine/platform/ios/IosTierDetect.{h,mm}` exposes `detectIosTier()` (sysctl `hw.machine` + `NSProcessInfo.physicalMemory`) returning `IosTier::{Low,Mid,High,Unknown}`; ~50 machine identifiers mapped, RAM-fallback for unknowns, simulator → High. `tests/platform/TestIosTierDetect.cpp` covers identifier table + RAM-heuristic boundaries (9 cases / 66 asserts). [x] Integrated into `Engine::initIos` (logs detected tier to stderr) and `IosApp.mm` (writes `tierToProjectConfigName(detectedTier)` into `ProjectConfig::activeTier` so `toEngineDesc()` picks the correct shadow res / cascade count); `IosTier::Unknown` maps to `"mid"` (safe default — see `docs/NOTES.md`).
- [ ] Asset catalog generation for app icons

### Phase D — IPA Packaging
- [ ] Xcode archive + export workflow
- [ ] Code signing with Apple Developer certificate
- [ ] Entitlements file for capabilities
- [ ] `xcodebuild archive` + `xcodebuild -exportArchive` automation
- [ ] CLI: `sama-build ios --tier mid --signing "Developer ID" --output MyGame.ipa`

### Phase E — App Store Submission
- [ ] App Store Connect metadata generation
- [ ] Screenshot automation for required device sizes
- [ ] Privacy manifest (`PrivacyInfo.xcprivacy`) — required since 2024
- [ ] TestFlight distribution support

---

## Key Design Decisions

- **Metal only, no OpenGL ES.** Apple deprecated OpenGL ES in iOS 12 (2018) and all target devices support Metal. No reason to maintain a GLES path.

- **Xcode for signing and packaging.** Unlike Android (where we bypass Gradle), iOS requires Xcode's toolchain for code signing, provisioning profiles, and IPA creation. The build pipeline generates an Xcode project via CMake and uses `xcodebuild` for the final archive.

- **Shared tier system with Android.** The `TierConfig` struct and `sama-asset-tool` are platform-agnostic. iOS uses the same low/mid/high definitions with identical ASTC block sizes and texture limits. Only the shader output format differs (Metal vs SPIRV).

- **TBDR-first render settings.** All Apple GPUs are tile-based, so the mobile render preset is always the starting point. High tier on iOS enables SSAO and higher shadow resolution but never enables depth prepass.

- **CMMotionManager for gyro.** Unlike Android's `ASensorManager`, iOS uses the higher-level `CMMotionManager` which provides fused device motion (gyro + accelerometer + magnetometer). This gives better orientation data out of the box.

---

## Differences from Android

| Aspect | Android | iOS |
|--------|---------|-----|
| Graphics API | Vulkan | Metal |
| Shader format | SPIRV | Metal Shading Language |
| Build system | CMake + aapt2 + apksigner | CMake + Xcode + codesign |
| Distribution | APK/AAB, sideload possible | IPA, App Store/TestFlight only |
| Signing | Java keystore | Apple Developer certificate |
| GPU architecture | Mixed (IMR + TBDR) | Always TBDR |
| Tier detection | Heuristic (GPU string, RAM) | Precise (device model lookup) |
| Gyro API | ASensorManager | CMMotionManager |
| File system | AAssetManager (APK assets/) | NSBundle (app bundle) |
| Min OS version | Android 7.0 (API 24) | iOS 15.0 |
