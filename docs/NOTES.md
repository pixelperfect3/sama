# Sama Engine — Development Notes

Tracks all decisions and progress made during development.

---

## Project Setup

### C++ Standard
- **Version:** C++20
  - Concepts, `std::span`, `std::ranges`, `consteval`, Coroutines all available
  - **Modules excluded** — build system support is still inconsistent across platforms (MSVC, Clang, Android NDK)
  - Target compilers: Apple Clang (Xcode 12+), Clang/MSVC on Windows, Android NDK r23+

### Code Formatting
- **Style:** Allman braces, 4-space indent, 100 char line limit, `PointerAlignment: Left`
- **Naming:** `UpperCamelCase` classes, `camelCase` functions/variables, `ALL_CAPS` constants, private members suffixed with `_`
- **Namespaces:** `lower_case`, no indentation inside namespaces
- **Tool:** clang-format — run after every C++ file write/edit
  ```
  brew install clang-format   # one-time setup
  clang-format -i <file>      # single file
  clang-format -i engine/ecs/*.h engine/ecs/*.cpp   # directory
  ```
  Config: `.clang-format` at project root

### Threading Model
- **Approach:** System-level parallelism — independent systems run concurrently (e.g. physics + audio in parallel)
- **Intra-system threading:** Deferred for general ECS systems — revisit if a single system becomes a bottleneck on high-end targets.
  **Exception: render systems use intra-system data parallelism from day one.** `FrustumCullSystem` and `DrawCallBuildSystem` partition their entity lists across thread pool workers each frame. This is not a perf-driven future optimisation — it is baked into the render architecture because:
  1. bgfx's encoder model was designed specifically for multi-threaded draw call recording (`bgfx::begin(true)` per worker, `bgfx::end()` after the loop)
  2. The render workload (culling and recording draw calls for up to 50k visible entities) is the dominant CPU hot path, not a speculative concern
  3. bgfx encoders require the pattern regardless — a single encoder on the main thread would serialise all draw call setup work
  The general "deferred" rule still applies to physics, audio, input, and all non-render ECS systems.
- **Platform flexibility:** Thread pool size fixed at startup, configurable per platform (e.g. fewer threads on mobile)
- **Dependency declaration:** Static — each system declares `using Reads = TypeList<...>` and `using Writes = TypeList<...>` as type aliases
- **DAG construction:** Compile-time via `constexpr buildSchedule<Systems...>()` — computes execution phases and bakes them into the binary as a plain array. Zero runtime overhead, no heap allocation in the scheduler
  - Two systems conflict if one writes a component the other reads or writes
  - Conflicting systems are serialized in the DAG (no parallelism) — revisit splitting ownership if this becomes a perf bottleneck
  - All systems must be known at compile time — runtime-registered systems not supported in this layer (future scripting layer would be separate)
  - Binary size impact: minimal (~100–500 bytes for the phase array) — smaller than a runtime hash map approach
- **Execution:** Phase-based — each phase is a set of systems with no conflicts, dispatched in parallel to the thread pool. Executor waits for all systems in a phase to finish before starting the next

#### Future Consideration
- Explicit system ordering without data dependencies (e.g. `dependsOn(InputSystem)` even with no shared components) — not implemented yet, revisit when needed

### Compile Time
- **Goal:** Keep compile times fast — this is a priority, not an afterthought
- **Strategies in use:**
  - `#pragma once` on all headers (faster than include guards on most compilers)
  - Forward declarations preferred over full includes in headers
  - Template-heavy code isolated to headers that are not transitively included everywhere
  - `Registry.cpp` extracts non-template implementations out of the header
  - C++20 Modules excluded partly for this reason — when tooling matures, revisit
- **Future:** Consider unity builds (`UNITY_BUILD` in CMake) or precompiled headers (`target_precompile_headers`) if compile times become painful at scale

### Tooling
- **clang-format:** Run after every C++ file write/edit (see Code Formatting above)
- **clang-tidy:** Optional static analysis, enable with `-DENABLE_CLANG_TIDY=ON` in CMake
- **Build:**
  ```bash
  # Initial CMake configure (once)
  mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug

  # Build everything
  cmake --build build -j$(sysctl -n hw.ncpu)

  # Build specific targets
  cmake --build build --target engine_tests -j$(sysctl -n hw.ncpu)
  cmake --build build --target engine_screenshot_tests -j$(sysctl -n hw.ncpu)
  cmake --build build --target helmet_demo -j$(sysctl -n hw.ncpu)
  cmake --build build --target hierarchy_demo -j$(sysctl -n hw.ncpu)
  cmake --build build --target physics_demo -j$(sysctl -n hw.ncpu)
  cmake --build build --target audio_demo -j$(sysctl -n hw.ncpu)
  cmake --build build --target animation_demo -j$(sysctl -n hw.ncpu)
  ```

- **Run tests:**
  ```bash
  build/engine_tests                          # all unit tests (445 cases)
  build/engine_tests "[scenegraph]"           # tests by tag
  build/engine_tests "[physics]"
  build/engine_tests "[audio]"
  build/engine_tests "[animation]"
  build/engine_tests "[json]"
  build/engine_tests "[serializer]"
  build/engine_tests "[memory]"
  build/engine_tests "[transform]"
  build/engine_screenshot_tests               # all screenshot tests (11 cases)
  build/engine_screenshot_tests --update-goldens  # regenerate reference images
  ```

- **Run demos** (from the build directory for asset loading):
  ```bash
  cd build
  ./helmet_demo         # PBR helmet with rotating light + IBL
  ./hierarchy_demo      # Scene graph with draggable cubes
  ./physics_demo        # Falling cubes on tilting plane (Jolt)
  ./audio_demo          # Spatial 3D audio with volume sliders (SoLoud)
  ./animation_demo      # Skeletal animation with playback controls
  ```

---

## ECS (Entity-Component-System)

### Decisions
- **Storage strategy:** Sparse set (custom implementation, no external library)
  - Note: Revisit archetype-based ECS in the future if multi-component query performance becomes a bottleneck
- **Entity IDs:** uint64, packed as `[ index | generation ]`
- **Memory layout:** SoA (Struct of Arrays)
- **Design target:** ~50k active entities

### Status
- [x] ECS implemented (Registry, SparseSet, Views, System scheduler — all tested)

---

## Rendering

### Decisions
- **Abstraction layer:** bgfx (Metal + Vulkan backends only, all others disabled at compile time)
  - Chosen over IGL (Meta): bgfx has stronger game engine track record, more documentation/examples, includes shader tooling (shaderc)
  - IGL noted as stronger for Android long-tail device fragmentation — revisit if that becomes a pain point
  - Chosen over custom HAL: binary size savings (~1–2MB) do not justify months of low-level Vulkan/Metal boilerplate
  - **App code should never call bgfx directly.** The engine provides abstractions: `Engine::setClearColor()` for view clearing, `DebugHud` for debug text (replaces `bgfx::dbgTextPrintf`), `RenderResources` for default textures. This ensures game code is portable and doesn't depend on rendering backend internals.
- **glTF parser: cgltf**
  - Single C header (~7,500 lines), MIT license — smallest viable option
  - Parses glTF 2.0 JSON and binary GLB; handles meshes, materials, textures, scene node trees
  - Parser only — no image loading, no tangent generation. Those are handled separately by stb_image (already in the project) and mikktspace respectively. This keeps each piece independently replaceable.
  - **Rejected: tinygltf** — bundles nlohmann/json internally, which was already rejected for binary size. The ergonomics advantage doesn't justify the 3× size penalty.
  - **Rejected: fastgltf** — strongest technical option (simdjson, C++20, cleanest API) but is a multi-file CMake dependency that brings in simdjson; size overhead is not justified on mobile at this stage. Revisit if cgltf parse performance becomes a bottleneck.
  - **Rejected: assimp** — ~10 MB, ruled out by the same logic as FMOD. Multi-format support is not needed yet.
  - cgltf is used internally in bgfx's own examples, confirming it is a known quantity in this stack.

- **Ray tracing: tabled as long-term**
  - bgfx does not expose RT APIs (no DXR, no VK_KHR_ray_tracing_pipeline, no Metal RT)
  - Adding RT to bgfx would require forking it: new resource types (BLAS, TLAS, shader binding tables), new shader stages (raygen, miss, hit), shaderc changes, and separate Metal/Vulkan backend implementations — not a clean fit into bgfx's abstraction model
  - RT on mobile is not viable in the near term (requires A17 Pro/M-series for Metal, high-end Android for Vulkan RT)
  - Revisit when RT is ready to implement — evaluate bgfx RT support status at that time, or consider a targeted custom RT layer on top

### Implementation Decisions (resolved during Phase 1–2 implementation)

**View ID layout** — bgfx views are a scarce resource (max 32 by default). Shadows with CSM require one view per cascade, so the shadow range must be reserved upfront:
  - Views 0–7: shadow maps (`kViewShadowBase = 0`, up to 8 cascades/spot lights)
  - Views 8–10: fixed scene passes (depth prepass=8, opaque=9, transparent=10)
  - Views 11–15: reserved for future scene passes (decals, velocity, particles)
  - Views 16–47: post-process sub-passes (bloom needs 10+ sequential passes, each requiring its own view)
  - Views 48–51: UI / HUD (`kViewGameUi=48`, `kViewDebugHud=49`, `kViewImGui=50`, `kViewUi=51`) — all UI is rendered AFTER post-process so tonemap / bloom / FXAA do not touch text and icons
  This replaces the original "6 fixed views" design — the post-process chain is not a single view, and the original UI placement at views 14/15 was clobbered by the post-process FXAA write to the backbuffer (fixed during the unified post-process refactor — see "Phase 7: unified post-process pipeline" below).

**Light data format** — `u_lights` as a uniform array cannot hold 256 lights × 3 vec4s = 768 vec4s: bgfx's `BGFX_CONFIG_MAX_UNIFORMS = 512` by default. **Decision: pack light data into an `RGBA32F` texture (256×4 texels, 16 KB)**:
  - Row 0: `position.xyz, radius`
  - Row 1: `color*intensity.xyz, type`
  - Row 2: `spotDirection.xyz, cosOuterAngle`
  - Row 3: `cosInnerAngle, 0, 0, 0`
  Sampled via `texelFetch` / `texture2D` with point sampler in the fragment shader. This also resolves the 3-vec4 layout gap where `cosInnerAngle` had nowhere to fit.

**Cluster light grid format** — `u_lightGrid` and `u_lightIndices` also cannot be large uniform arrays at scale. **Decision: use textures**:
  - Light grid: 3456×1 `RGBA32F` texture (one texel per cluster, `.xy = offset, count`)
  - Light index list: 8192×1 `R32F` texture (flat list of uint light indices)

**Shadow atlas for CSM** — rather than one framebuffer + sampler per cascade, pack all cascades into a single shadow atlas texture with UV offsetting. This avoids per-cascade sampler uniform slots and simplifies the shader to one `SAMPLER2DSHADOW`. Atlas layout for 4 cascades: 4096×2048 (four 1024×2048 columns), or 4096×4096 for 2048² cascades.

**`WorldTransformComponent` must have `alignas(16)`** — GLM's `mat4` has 4-byte alignment by default. For future `SimdMat4` compatibility (ARM NEON requires 16-byte alignment), add `alignas(16)` now to avoid ABI breakage later.

**`SparseSet<VisibleTag>` empty-type specialisation required** — the doc claims `VisibleTag` on 50k entities costs only the sparse index array (200 KB). Without a `SparseSet` specialisation for empty types, the dense array still allocates (50 KB of 1-byte `VisibleTag`s). The specialisation must be written in Phase 2.

**`AssetHandle<T>` and `AABB` are Phase 2 prerequisites** — both are referenced by component headers and render systems but were absent from the original plan. Must be created before any render component headers compile.

**Post-process is N sequential bgfx views, not one** — bloom requires 1 threshold pass + 5 downsample + 5 upsample + 1 composite = 12 passes. Each reads the previous pass's output and cannot share a bgfx view. The view range 16–47 (32 slots) is allocated for all post-process sub-passes. `PostProcessSystem` manages its own view ID counter within this range.

**DAG system Reads/Writes for render systems** — if `LightClusterBuffer` is managed outside ECS (owned by `Renderer`, passed by reference), `LightCullSystem` has no ECS `Writes`. This allows the DAG to schedule `LightCullSystem` and `FrustumCullSystem` in the same phase (truly parallel) without any false data dependency. Confirmed correct design.

**Tangent bitangent sign encoding** — `a_tangent.w` (snorm8) stores handedness. Never quantise to 0; store +1 as `+127` and -1 as `-1` in the mesh importer. The shader reads `sign(a_tangent.w)` and a zero would eliminate the bitangent.

**Normal matrix for non-uniform scale** — for Phase 3, assume uniform scale and use `mat3(u_model)` for the normal transform. For non-uniform scale, `transpose(inverse(mat3(u_model)))` is required — tracked as technical debt, computed on CPU and uploaded as `u_normalMatrix` when needed.

**bgfx boundary — opaque-alias FrameBufferHandle, not a class split** — `engine::rendering::ViewId` is a `uint16_t` typedef, and `engine::rendering::FrameBufferHandle` is a one-word struct laid out byte-identically to `bgfx::FrameBufferHandle` (guarded by `static_assert(sizeof/alignof)` in `RenderPass.cpp`). `RenderPass.h`, `ViewIds.h`, `HandleTypes.h` and `FrameStats.h` form the public boundary and contain no `<bgfx/bgfx.h>` include — a CMake-driven CTest (`forbid_bgfx_*`) preprocesses each header against the rendering target's include directories and fails if bgfx leaks back in.

  *Reasoning:* the natural alternative was a "wrapper class" split (a `RenderPassImpl` pimpl in the engine, a separate `RenderPass` API in a public header). That gives strong ABI guarantees but costs a heap allocation per pass *and* a cross-TU virtual or function-pointer hop on every `setView*` call — for an API that is expected to be invoked dozens of times per frame. The opaque-alias choice keeps the call graph identical to the pre-abstraction code: every fluent setter is still inlined to a single `bgfx::setView*`, and the boundary conversion (`bgfx::FrameBufferHandle{h.idx}`) compiles to nothing.

  *Tradeoff accepted:* the boundary is *layout-coupled* to bgfx's handle struct. If bgfx ever changes `FrameBufferHandle` (e.g. to a wider integer or to embed a generation counter), the static_assert fires loudly at build time and we have to update the alias. That is an acceptable price for the zero-cost call path; the alternative (pimpl) would have been the wrong default for a hot-path API. Same logic for `ViewId` — a static_assert in `ViewIds.h` guards the underlying `uint16_t` type.

  *Knock-on benefit:* `engine::rendering::FrameStats` (the bgfx-free perf-counter API) and `Renderer::setupDefaultViewNames` (engine self-labels its built-in views) close the only two remaining direct-bgfx paths that game code had — labels and stats — without changing any inner-loop semantics.

### Implementation Progress
- [x] Phase 1 — bgfx init, GLFW window, Renderer lifecycle (committed)
- [x] Phase 1b — RenderSettings quality structs, GpuFeatures, presets, 42 tests (committed)
- [x] Phase 2 — mesh upload, vertex layouts, unlit draw, frustum cull (committed)
- [x] Phase 3 — PBR material + directional light, GGX shaders (committed)
- [x] Phase 4 — shadow maps, ShadowRenderer, ShadowCullSystem (committed)
- [x] Phase 5 — GPU instanced mesh rendering (committed)
- [x] Phase 6 — clustered point + spot lights, LightClusterBuilder (committed)
- [x] Phase 7 — bloom, ACES tonemap, FXAA post-process chain (committed)
- [x] Phase 8 — SSAO screen-space ambient occlusion (committed)
- [x] Phase 9 — CSM 3-cascade shadows, CsmSplitCalculator (committed)
- [x] Phase 10 — 2D sprite batching + UI pass (committed)
- [x] Phase 11 — IBL: irradiance, prefiltered specular, BRDF LUT (committed)

All 437 test cases pass (5373 assertions).

### Status
- [x] All 11 rendering phases complete and committed
- [x] IBL improved: procedural sky model (blue zenith, warm horizon), GGX importance sampling for prefiltered cubemap (128x128, 8 mip levels), cosine-weighted irradiance (64x64), BRDF LUT view vector bug fixed

### Screenshot Tests

**Infrastructure:**
- `engine_screenshot_tests` — separate binary from `engine_tests`. Requires a real GPU (Metal on macOS, Vulkan on Linux). Headless `engine_tests` remains GPU-free.
- `tests/screenshot/ScreenshotFixture` — creates a hidden GLFW window (`GLFW_VISIBLE=false`), initializes bgfx with Metal/Vulkan, owns a 320x240 BGRA8 render target and a separate BGRA8 blit/readback texture (following bgfx 30-picking example pattern). After drawing, `captureFrame()` blits RT to readback, pumps `bgfx::frame()` until async readback completes, returns RGBA pixels.
- `tests/screenshot/GoldenCompare` — loads/saves PNGs via stb_image. Comparison: per-pixel max-channel delta, fails if >1% of pixels differ by more than tolerance=8. On first run or missing golden: saves golden automatically (test passes). Run with `--update-goldens` to overwrite existing goldens.
- Golden images: `tests/golden/<name>.png` (committed to repo). Path baked in via `ENGINE_SOURCE_DIR` CMake define.

**Running:**
```bash
cd build && ./engine_screenshot_tests                     # compare against goldens
cd build && ./engine_screenshot_tests --update-goldens    # regenerate goldens
cd build && ./engine_screenshot_tests -# "[screenshot]"   # all screenshot tests
```

**CI:** Not configured yet (requires GPU runner). Local-only for now.

**Tolerance:** Per-channel max delta <= 8. Loose enough for minor GPU-driver differences across machines.

**Simplification notes:** SSAO (TestSsSsao), CSM (TestSsCsm), and post-process (TestSsPostProcess) tests render simplified scenes without their respective multi-pass setups. Each still produces a meaningful visible output for golden comparison. Full integration is future work once the fixture supports multi-view and multi-framebuffer pass chains.

Shadow (TestSsShadow) is **not simplified** — it runs a real two-pass setup: a depth-only shadow pass into a 512×512 `ShadowRenderer` atlas (kViewShadowBase, orthographic light projection from (-4,8,-4)), followed by a PBR pass (kViewOpaque) that reads `u_shadowMatrix[0]` and `s_shadowMap`. The occluder cube at (0,2,0) casts a visible shadow on the ground plane.

**Metal platform gotchas discovered during screenshot test development:**

- **`CAMetalLayer*` required, not `NSView*`:** GLFW with `GLFW_NO_API` does not attach a `CAMetalLayer` to the NSView. bgfx Metal expects `CAMetalLayer*` as `nwh`. Passing `NSView*` compiles silently but produces no rendered output. Must create and attach `CAMetalLayer` explicitly via ObjC C runtime before `bgfx::init()`.

- **Unbound samplers return `(0,0,0,0)` on Metal:** Unlike desktop GL/Vulkan which may return a default color, Metal returns black-transparent for every unbound sampler. In the PBR shader: unbound `s_albedo` → `albedo *= 0 = black`; unbound `s_orm` → `ao = 0` (kills ambient); unbound `s_shadowMap` with zero shadow matrix → `shadow *= 0` → all geometry black. Always bind a 1×1 white texture to unused sampler slots.

- **`bgfx::renderFrame()` single-thread mode:** Must be called exactly once before `bgfx::init()`, then never again. Calling it after init double-processes command buffers → SIGSEGV in Metal texture creation. In the readback loop, use only `bgfx::frame()`.

- **`u_dirLight[0].xyz` direction convention:** The PBR shader treats this as pointing **from the surface toward the light** (not the direction light travels). Passing the light-travel direction produces NdotL=0 on all visible faces → geometry appears black except for emissive/ambient. Verified in `fs_pbr.sc` comment: "dir points from the surface toward the light source (normalised, away from surface)."

### Scene Demo (`apps/scene_demo`)

End-to-end sample app exercising platform, rendering, input, and ECS abstractions together.

**Design decisions resolved during scene demo development:**

**Sample apps must not call bgfx or GLFW directly** — only the engine abstractions (`IWindow`, `Renderer`, `RenderPass`, ECS systems). Direct bgfx/GLFW calls in app code bypass the abstractions and duplicate what the engine already encapsulates. The only GLFW exception is mouse capture (`glfwSetInputMode`), which requires the raw `GLFWwindow*` from `GlfwWindow::glfwHandle()` — there is no engine-level mouse-capture API yet.

**`RenderPass` fluent builder** — wraps all `bgfx::setView*` calls for one pass into a single chain (`RenderPass(id).framebuffer(...).rect(...).clearColorAndDepth(...).transform(...)`). This prevents the class of bug where a later pass accidentally overrides an earlier pass's view state — bgfx view state is last-write-wins per frame. The shadow viewport bug (shadow atlas rendered at screen resolution instead of 2048×2048) was caused by a stray `bgfx::setViewRect(0, ...)` elsewhere in the frame; `RenderPass` scoping makes this impossible by construction.

**`bgfx::renderFrame()` belongs in `Renderer::init()`** — it must be called exactly once before `bgfx::init()` to prevent bgfx from spawning its own render thread (single-threaded mode). Previously it was called manually in the app. Moving it into `Renderer::init()` means app code can never forget it and cannot call it at the wrong time.

**`GlfwWindow::nativeWindowHandle()` must return `CAMetalLayer*`** — bgfx Metal requires `CAMetalLayer*` as `nwh`, not `NSView*`. `NSView*` passes silently but produces no rendered output. The layer must be created and attached to the view before `bgfx::init()` via the ObjC C runtime (`objc_msgSend`), keeping the `.cpp` file free of ObjC syntax.

**`DrawCallBuildSystem` PBR+shadow overload (`PbrFrameParams`)** — bgfx resets all per-draw state (uniforms, textures) after every `submit()`. The existing PBR overload only uploaded `u_material`; `u_dirLight`, `u_shadowMatrix`, and the three texture slots (`s_albedo`, `s_orm`, `s_shadowMap`) had to be set by the caller before the loop, which means they would only be set once and then silently dropped after the first submit. The `PbrFrameParams` struct (`lightData`, `shadowMatrix`, `shadowAtlas`) lets the new overload re-set all frame-level state per draw inside the system, with no caller pre-setup needed.

**`engine_ecs` static library** — `Registry.cpp` was compiled inline in each test executable rather than into `engine_rendering`. Creating `engine_ecs` as a named static library and linking it to `engine_rendering` as `PUBLIC` means any consumer of `engine_rendering` (including `scene_demo`) gets `Registry` automatically, and the duplicate compilations in test targets are removed.

### Helmet Demo (`apps/helmet_demo`)

Full PBR sample app loading the KhronosGroup DamagedHelmet GLB asset and rendering with directional shadow, PBR shading, and a runtime debug texture panel.

**glTF material completeness audit** — the initial implementation only wired up three of the five DamagedHelmet textures: albedo (slot 0), normal (slot 1), and ORM (slot 2). Emissive and occlusion were decoded and uploaded to the GPU but never bound to the shader. Discovery method: wrote `TestGltfLoader` which decodes the GLB, dumps all five textures to `/tmp/helmet_textures/` as PNGs and verifies dimensions and material index assignments. Both missing textures are 2048×2048 and fully populated — just unused.

**ORM.R ≠ AO per glTF spec** — the glTF 2.0 spec defines the separate `occlusionTexture` (R=AO) as independent from `metallicRoughnessTexture` (G=roughness, B=metallic). `ORM.R` is officially undefined. The DamagedHelmet ships a distinct occlusion texture. The shader was updated to read AO from a dedicated `s_occlusion` sampler (slot 4) rather than `ormSample.x`. White-texture fallback gives `ao=1.0` (no occlusion applied) for models without a separate occlusion map.

**emissiveScale from glTF emissive_factor** — `emissiveScale` is set to `max(emissive_factor[0..2])` when an emissive texture is present, staying 0 otherwise. This preserves existing behaviour for non-emissive models (white fallback × 0 = no contribution) while activating the texture for the helmet's scorch-marked panels.

**Metal LOD=11 bug with `hasMips=true`** — bgfx's Metal backend computes `LOD = floor(log2(max(w,h))) = 11` for 2048² textures when `hasMips=true` but only mip 0 is provided. Metal GPU samples the lowest-resolution mip (black border texel) producing a near-black result. Fix: pass `hasMips=false` to `bgfx::createTexture2D`. This suppresses Metal's auto-LOD selection and forces sampling from mip 0.
**Why:** `hasMips=true` tells Metal the full mip chain exists; when it doesn't, hardware walks off the end of the atlas. `hasMips=false` is the correct value when uploading a single mip level.

---

### Debug Tools (`engine_debug`)

**`engine_debug` static library** — bundles dear-imgui (1.77), the bgfx imgui rendering backend (`examples/common/imgui/imgui.cpp`), and `DebugTexturePanel`. Any sample app links `engine_debug` and calls `imguiCreate/BeginFrame/EndFrame`; the backend and dear-imgui are compiled once and not repeated per app.

**bgfx imgui backend compiled without USE_ENTRY** — the backend in `examples/common/imgui/imgui.cpp` guards keyboard key-map setup behind `#if USE_ENTRY`. Without it, `io.KeyMap[]` is entirely empty: keyboard navigation (arrow keys, Page Up/Down) silently does nothing even with `ImGuiConfigFlags_NavEnableKeyboard` set. Fix: after `imguiCreate()`, manually set `io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP` etc. and update `io.KeysDown[glfwKey]` each frame via `glfwGetKey`.

**bgfx imgui scroll is cumulative, not per-frame delta** — `imguiBeginFrame` takes `int32_t _scroll` and internally computes `io.MouseWheel = _scroll - m_lastScroll`. Passing a per-frame delta and resetting it to 0 each frame means the next frame always computes `0 - delta = -delta`, cancelling every scroll event. The accumulator must be monotonically increasing (never reset). Store as `float` to accumulate fractional trackpad deltas (e.g. 0.3 per GLFW event) that would truncate to 0 with `int32_t` cast.

**ImGui mouse-capture ordering** — `imguiBeginFrame` must be called before the game's mouse-capture check so that `ImGui::GetIO().WantCaptureMouse` reflects the current frame's hover state. Calling it after means every click on an ImGui panel is stolen by the game first. The corrected order: `imguiBeginFrame` → check `WantCaptureMouse` → conditionally activate game mouse capture.

**`ConfigWindowsMoveFromTitleBarOnly`** — by default ImGui windows are draggable from their entire surface, including content area. This makes it impossible to scroll or interact with content without accidentally moving the window. Set `io.ConfigWindowsMoveFromTitleBarOnly = true` after `imguiCreate()` so windows only move when dragged from the title bar.

---

### bgfx threading mode — multi-threaded default (2026-05-24)

**Decision:** `EngineDesc::singleThreaded` defaults to `false` (multi-threaded bgfx).  `Renderer::init` only calls `bgfx::renderFrame()` before `bgfx::init()` when `desc.singleThreaded == true`; without that pre-init call, bgfx spawns its own render thread and `bgfx::frame()` becomes an asynchronous hand-off through a lock-free ring buffer.

**Why we changed it.** The previous "always single-threaded" choice (committed during scene_demo bring-up — see the older "`bgfx::renderFrame()` belongs in `Renderer::init()`" entry above) was the right *default* for the screenshot fixture (it needs deterministic blit-and-readback on the calling thread) but the *wrong* default for shipping games.  On Pixel 9, a reporter's rolling-ball game (1080×2424, ~2.6 MP fragment work) measured `bgfx::frame()` at 20.5 ms of a 21.2 ms total frame — pure swapchain wait charged to the game thread.  perf_smoke had not been measuring `bgfx::frame()` wall time so the regression had been invisible to the budget table.  Flipping the default to multi-threaded moves that wait onto the render thread, where the GPU work runs in parallel with the next frame's CPU work.

**Why a flag and not a hard switch.**
- The screenshot fixture (`tests/screenshot/ScreenshotFixture.cpp`) does its own `bgfx::init` (outside `Renderer`) and continues to use single-threaded mode so the per-test capture flow stays deterministic.  It doesn't go through the new flag at all.
- A future test or tool (e.g. a one-shot frame-dump) might still need synchronous frame submission on the calling thread.  Keeping the flag makes that a one-line opt-in instead of a fork of `Renderer::init`.

**Tradeoffs accepted.**
- *AssetManager:* worker threads only produce CPU bytes (`CpuAssetData`); every `bgfx::*` call lives inside `processUploads()`, which the header has always pinned to the main / bgfx-submission thread.  No marshalling changes were required — but the contract is now load-bearing for correctness, not just convention.  Future asset code that wants to call `bgfx::*` from a worker MUST queue a CPU payload and let `processUploads()` issue the GPU call.  This includes the **`destroyPayload()` path** added in the same change (`engine/assets/AssetManager.cpp`): the `pendingFree_` loop in `processUploads()` and the `~AssetManager()` cleanup both dispatch on the `std::any` payload type to call `Texture::destroy()` before the slot is recycled.  Those are real `bgfx::destroy(handle)` calls and rely on running on the bgfx submission thread.  Any future asset payload that owns bgfx handles **uniquely** must add (a) a `destroy()` method that releases them and (b) a matching branch in `destroyPayload()`.  `GltfAsset` is deliberately excluded from the dispatch (see comment block above the helper) — its meshes are moved into `RenderResources` by `GltfSceneSpawner` while its textures are referenced non-owningly, so a blanket `GltfAsset::destroy()` would double-free the mesh handles.  Fixing that split-ownership story is tracked as deferred work — the standalone-`Texture` leak (the common game-side path) is the immediate win.
- *Latency / determinism:* `bgfx::frame()` no longer represents "GPU done with frame N when this returns."  Game-side code that needed that semantics (none today, but worth flagging) must either flip the flag back to single-threaded or use bgfx's explicit fence APIs.
- *Frame stats:* `EngineFrameStats::bgfxFrameMs` now means two different things depending on the flag — `~0.1 ms` (hand-off) vs `~10-20 ms` (full submit + wait).  The field comment in `engine/core/Engine.h` documents both interpretations so a future reader doesn't mistake the multi-threaded number for "the engine got faster."

**Verification.**
- Unit-level: `tests/rendering/TestThreadingMode.cpp` (4 cases) covers default values + headless init/shutdown in both modes.  The headless path can't exercise the real `bgfx::renderFrame()` gate (Noop renderer skips it regardless), so this is plumbing-only coverage.
- Integration-level: `apps/perf_smoke/run_both.sh` runs perf_smoke twice (one process per mode — `bgfx::init` is one-per-process in this codebase) and prints two budget tables; the `bgfx::frame()` row is the headline cell.  On the Pixel 9 sample game the expected delta is ~20 ms → ~0.1 ms on the game thread.

**Not done (deliberate):** per-thread command recording via `bgfx::Encoder`.  That is a much larger refactor (every system that calls `bgfx::submit` would need to take an encoder), and the multi-threaded-default win alone closes the Pixel 9 frame-budget gap with margin.

### Correction (2026-06-16): "expected ~0.1 ms" was scene-specific, not a general guarantee

A game team integrating Sama reported the multi-threaded flip was working but `bgfx::frame()` still measured 15+ ms on Pixel 9 (45-50 fps), not the predicted ~0.1 ms.  Their flag plumbing was correct (`EngineDesc::singleThreaded = false` reaches `Renderer::init`, the pre-init `bgfx::renderFrame()` is correctly skipped, bgfx is built with `BGFX_CONFIG_MULTITHREADED=1`).  Diagnosis: **the original 0.1 ms number was workload-specific and shouldn't have been written as a general expectation**.

**Mechanism.**  bgfx multi-threaded mode replaces the synchronous "submit + GPU wait + present" on the game thread with an asynchronous hand-off into a lock-free ring buffer.  The render thread drains the ring, makes the Vulkan/Metal/D3D calls, and waits for vsync before presenting.  This works as long as the ring stays drained — i.e. the GPU finishes frame N before frame N+1 arrives.

When `BGFX_RESET_VSYNC` is set (it is, in `Renderer.cpp:69` + `:267`) AND GPU work approaches the vsync period (16.67 ms at 60 Hz), the render thread blocks on `vkAcquireNextImageKHR` (or the platform equivalent) waiting for vblank.  The ring fills up.  The game thread's next `bgfx::frame()` call then blocks waiting for a free ring slot — the wait *migrated* from the GPU-fence wait into the ring-back-pressure wait, but the wall-clock cost is conserved.  Steady-state `bgfx::frame()` ≈ `vsync_period − rest_of_CPU_work − tiny_ring_overhead`, which matches the reporter's 15+ ms exactly.

**The original 0.1 ms was real but in a different regime.**  The 20.5 ms → 0.1 ms delta cited above was extrapolated from perf_smoke (1280×720, simple test scene where GPU work was well under vsync — ring stays drained, hand-off cost dominates).  The reporter's game (1080×2424, full PBR pipeline) is in the GPU-bound regime where vsync gates everything anyway.

**What does multi-threaded actually win for them?**  Not zero, but not what the docs promised either.  Concretely:
- *CPU-side game-thread cost.*  Submit commands → hand-off is ~0.05 ms instead of submit → GPU wait → present being ~16 ms.  The game thread is freed up for the next frame's `Update` / `Render` work.  But — and this is the rub — `bgfx::frame()` *still blocks* on the back-pressure wait, so the apparent saving shows up in the *prior* phases (other systems get more headroom), not in the `bgfx::frame()` row itself.
- *GPU-CPU parallelism.*  Even with the ring full, the render thread is *making the Vulkan calls* while the game thread is *building the next frame's command buffer*.  In single-threaded mode those serialised; in multi-threaded they overlap, so total frame time at the same vsync trims by the smaller of (render-thread time, game-thread time).

**Fix paths** (in order of effort, for the integrating team):

1. **Reduce GPU work below the vsync period.**  The real cure.  If a frame's actual GPU time is 12 ms (vs 16.67 ms vsync at 60 Hz), the render thread drains the ring with margin and `bgfx::frame()` collapses to the hand-off cost.  This is what the engine's perf audit work targets (`docs/PERF_AUDIT_2026-05-25.md`).  Look at fragment shader cost first on a Mali / Adreno phone — TBDR + clustered lighting can absorb a lot but PBR + bloom + SSAO at 1080p×2424 is not free.
2. **Halve the vsync target to 30 fps** on tiers that can't hit 60.  Doubles the budget (33 ms instead of 16.67 ms) for the same workload.  `Surface.setFrameRate(30, CHANGE_IF_SEAMLESS)` on Android — audit item line 138 `#P2`, partially landed via `TierConfig::targetFPS` but the Android-side `Surface.setFrameRate` plumbing is still open.
3. **Disable vsync.**  Pass a different reset flag (`init.resolution.reset &= ~BGFX_RESET_VSYNC`).  Confirms the back-pressure diagnosis instantly — if `bgfx::frame()` drops to ~0.1 ms with vsync off, hypothesis 2 is the smoking gun.  Production cost: screen tearing.  Not a fix, just a diagnostic.
4. **Increase swapchain depth.**  Default Android Vulkan swapchain is typically 3 images; some panels allow 4.  Buys a third "in-flight" frame for the game thread.  Tradeoff: one more frame of input latency.  Not exposed via `EngineDesc` today; would require a new flag.

**Doc fix.**  The original NOTES entry above said "expected delta is ~20 ms → ~0.1 ms on the game thread" without specifying the scene.  That's misleading.  The accurate framing:
- *With GPU work well under vsync period* (perf_smoke-like): `bgfx::frame()` → hand-off cost (~0.1 ms).  Win.
- *With GPU work near or above vsync period* (PBR-heavy game at 1080p+): `bgfx::frame()` → ring back-pressure wait (~vsync_period − CPU work).  The wall-clock per frame doesn't actually change much, but the game-thread CPU time *outside* `bgfx::frame()` becomes more available, and CPU/GPU run in parallel.

**Runtime verification** (also landed 2026-06-16): `Renderer::init` now logs `bgfx::getCaps()->supported & BGFX_CAPS_RENDERER_MULTITHREADED` + the engine-requested mode on Android, so the integrating team can confirm in logcat that multi-threaded is engaged.  This kills hypothesis 1 (silent fallback) on inspection.  See `engine/rendering/Renderer.cpp:108-138`.

**Open item.**  The audit's `#P2` (line 138 of `docs/PERF_AUDIT_2026-05-25.md`) — `Surface.setFrameRate(targetFPS)` plumbing on Android — directly addresses the "GPU work doesn't fit in vsync" case for tiers that target 30 fps.  Currently `TierConfig::targetFPS` exists but the Android-side panel rate change isn't wired up.

### Follow-up (2026-06-16, same day): vsync-off didn't help — back-pressure runs deeper

The integrating team applied fix path 3 (disable vsync as a diagnostic) and reported `bgfx::frameMs` is *still* 15+ ms.  That falsifies the "vsync is the gate" framing of the previous correction in its simplest form.  Three sharper hypotheses replace it:

**Hypothesis 3 — GPU is genuinely the bottleneck.**  PBR + bloom (~9 fullscreen passes at high tier) + SSAO at 1080×2424 on Mali-G715 is ~2.6 MP fragment work that can easily exceed 15 ms regardless of vsync.  The ring fills because the GPU itself is slow, not because vblank is gating.  Diagnostic: compare `gpuTime` (from `bgfx::getStats()`) to the wall-clock; if `gpuTime ≈ bgfx::frameMs`, this is it.

**Hypothesis 4 — bgfx's "vsync off" silently fell back to FIFO on Android Vulkan.**  Without `BGFX_RESET_VSYNC`, bgfx requests `VK_PRESENT_MODE_MAILBOX_KHR` first, then falls back to `FIFO` (which IS vsync).  `VK_PRESENT_MODE_IMMEDIATE_KHR` (true no-vsync) is rarely supported on Android Vulkan — Pixel 9 Tensor G4 / Mali-G715 may not expose it.  Net: the "vsync off" reset flag was a no-op.  Diagnostic: dump the actual swapchain present mode bgfx picked.  bgfx's Stats struct doesn't expose this directly, so the cleanest path is `bgfx::setDebug(BGFX_DEBUG_STATS)` which renders an overlay (or watching the bgfx init log via `bgfx::Callback`).

**Hypothesis 5 — Android compositor (SurfaceFlinger) back-pressure via `vkAcquireNextImageKHR`.**  Even if bgfx picked `MAILBOX`, the Android compositor still controls when swapchain images are released back to the app.  At a 60 Hz panel, SurfaceFlinger refreshes at ~16.67 ms cadence; `vkAcquireNextImageKHR` blocks waiting for a released image.  This is application-invisible vsync enforced at the compositor, independent of the bgfx-side present mode.  Diagnostic: same as 3 (gpuTime should be small, waitSubmit large).

**Disambiguating instrumentation landed today.**  `engine/rendering/Renderer.cpp:236-272` now dumps `bgfx::getStats()` every 120 frames on Android via `__android_log_print`:

```
SamaEngineBgfxStats: frame=120 bgfx::frameMs=15.2 | waitSubmit=14.8 ms | waitRender=0.1 ms | cpu=2.3 ms gpu=14.6 ms | numDraws=287
```

The five numbers triage in under a minute:
- **`waitSubmit ≈ bgfx::frameMs` and `gpuTime ≈ bgfx::frameMs`** → hypothesis 3.  GPU is the bottleneck.  Fix is to reduce GPU work — audit's perf items, drop bloom step count, lower shadow resolution, tier down.
- **`waitSubmit ≈ bgfx::frameMs` and `gpuTime << bgfx::frameMs`** → hypothesis 4 or 5.  GPU finishes fast but submit thread still waits.  Either bgfx fell back to FIFO (hypothesis 4 — check the bgfx init log) or SurfaceFlinger is gating acquire (hypothesis 5 — confirm by running on a 120 Hz panel; if it then drops to ~8 ms, compositor refresh is the gate).
- **`waitRender` large** → render thread is starved.  Game thread isn't keeping it fed.  Unlikely for a PBR-heavy game; would point to a different bug entirely.

Two diagnostics the team can run while waiting for the next APK build:
- *Compare on the same Pixel 9 against the simpler perf_smoke scene* (same panel, same vsync config, same APK harness).  perf_smoke's GPU work should be well under vsync — if its `bgfx::frame()` is ~0.1 ms but the shipping game's is 15 ms, that locates the cost in *their* GPU work, not the threading infrastructure.  This isolates hypothesis 3 even before the new instrumentation lands.
- *Try the build on a 120 Hz panel.*  If `bgfx::frameMs` drops to ~8 ms (= 1/120 s vblank period), the gate is at the compositor.  If it stays at 15 ms, the GPU is the limit.

**No engine fix yet.**  The right move depends on the diagnosis.  Likely outcomes:
- Hypothesis 3 confirmed → audit's perf items + lower the game's tier on Pixel 9 + #P2 (`Surface.setFrameRate(30)`) become the path.
- Hypothesis 4 confirmed → expose a way to set true `VK_PRESENT_MODE_IMMEDIATE_KHR` if the device supports it.  But this only matters for benchmarking; production wants tear-free.
- Hypothesis 5 confirmed → `Surface.setFrameRate(60)` doesn't help (you ARE running at 60 Hz already).  Real fix is to render faster than vblank or accept the cadence.

The integrating team should rebuild against `main` and share one of those logcat lines from a steady-state 5 s sample.  That nails the diagnosis.

### Follow-up (2026-06-16, same day, decisive): GPU stripped to floor, still 15+ ms — hypothesis 1 (silent fallback) is back

The team did the absolute-worst-case GPU test: 0.5× resolution, no IBL, no bloom, no SSAO, no FXAA, no depth prepass, 512² shadows.  `bgfx::frameMs` *still* 15+ ms.  Combined with the earlier vsync-off-also-15+-ms result, the evidence rules out:

- **Hypothesis 3 (GPU bottleneck)** — dead.  No GPU work and still 15 ms.
- **Hypothesis 5 (compositor back-pressure on the render thread)** — dead too, because if the render thread were the one waiting on `vkAcquireNextImageKHR`, the game thread's `bgfx::frame()` hand-off would still return fast UNLESS the ring is exactly 1 slot deep (in which case the wait migrates to the game thread).  But a 1-slot ring is the *single-threaded* configuration in disguise.

The remaining explanation: **bgfx is silently running in single-threaded mode at runtime on Pixel 9 / Android Vulkan.**  Either bgfx is taking the `m_singleThreaded = true` path inside `bgfx::Init` despite our `desc.singleThreaded == false`, OR the bgfx Android Vulkan backend has its own internal fallback that flattens to single-threaded for reasons we can't see from the engine's vantage point.

**Build-time fact (confirmed clean):** bgfx's `BGFX_CONFIG_MULTITHREADED` defaults to `1` on non-Emscripten platforms (`build/_deps/bgfx_cmake-src/bgfx/src/config.h:237-239`).  Android is non-Emscripten, so the build picks 1.  `BGFX_CAPS_RENDERER_MULTITHREADED` (the cap we logged earlier) reports this fact and will say "yes" — but that's the build flag, not the runtime mode.

**The decisive runtime evidence** is `bgfx.cpp:2169`:

```cpp
BX_TRACE("Running in %s-threaded mode", m_singleThreaded ? "single" : "multi");
```

That `BX_TRACE` macro routes through `bgfx::CallbackI::traceVargs` — a virtual that we had never wired up.  So bgfx was telling us the answer all along; we just weren't listening.

**Fix landed today** (`engine/rendering/Renderer.cpp:23-105` + `:133-142`): on Android, we set `init.callback` to a static `BgfxLogcatCallback` that forwards `traceVargs` and `fatal` to `__android_log_print` under tag `SamaEngineBgfx`.  All other CallbackI methods (profiler / cache / screenshot / capture) default to no-op so existing behaviour is unchanged.

After the next APK rebuild, the team's `adb logcat` filtered to `SamaEngineBgfx` will show one of two lines at init time:

```
I SamaEngineBgfx: Running in multi-threaded mode    ← multi-threading IS engaged, the slowness has a different cause
I SamaEngineBgfx: Running in single-threaded mode   ← bgfx fell back; hypothesis 1 confirmed
```

The full bgfx init stream (renderer selection, swapchain probe, capabilities, present mode picked) will also land under the same tag — surrounding context for the threading line.

**If the line says "single-threaded mode" — which is what the evidence now points to** — three things to check next, in order:

1. *Is bgfx's `s_renderFrameCalled` static somehow set to `true` despite our code only calling `bgfx::renderFrame()` when `singleThreaded == true`?*  Could a static initializer or third-party code (screenshot fixture inadvertently linked in?  bgfx debug-text overlay's init path?) be flipping it.  Add a `bgfx_p.h`-style assert log right before our own `bgfx::renderFrame()` call site to confirm whether *we're* the ones calling it.
2. *Does the Android Vulkan backend have a fallback path that flattens to single-threaded* on missing device features (no compute queue, no timeline semaphores, no separate transfer queue)?  Check `renderer_vk.cpp` on the bgfx fork we're on for any `m_singleThreaded = true` branches.
3. *Is `m_thread.init` failing silently* and bgfx then masquerading as multi-threaded?  Unlikely (bgfx asserts on `bx::Thread::init` failure) but worth a `__android_log_print(... "thread init returned")` patch to confirm.

**If the line says "multi-threaded mode" — but `bgfx::frameMs` is still 15+ ms:** then there's a back-pressure shape I haven't accounted for, almost certainly inside bgfx's Android Vulkan command-recording or queue-submit path.  At that point the diagnostic moves to a Vulkan capture (RenderDoc Android or Android GPU Inspector) to see exactly which `vk*` call the render thread is blocked on.

This is the conclusive diagnostic.  No further engine-side guessing; one log line resolves it.

### Follow-up (2026-06-18, conclusive): hypothesis 5 was right all along — Pixel 9 swapchain is 2-deep

The team got the trace callback wired and reports: **multi-threaded mode IS engaged**.  The full forensic picture from the new instrumentation:

- *Game thread (submit):* 0.38 ms of bgfx work, then 13.84 ms waiting for the render thread to drain the ring.
- *Render thread:* ~14 ms per frame wall-clock, but CPU + GPU only sums to ~3.7 ms.
- *The 10+ ms gap:* almost certainly `vkAcquireNextImageKHR` blocking against Android SurfaceFlinger / swapchain image-count starvation.

So hypothesis 5 (compositor back-pressure) was right — I'd ruled it out incorrectly earlier on the grounds that vsync-off and the GPU strip should have shown the same back-pressure if compositor was the gate.  Both diagnostics were misleading: vsync-off doesn't change SurfaceFlinger pacing (it paces independently at panel refresh regardless of bgfx's present-mode request), and dropping GPU work doesn't free swapchain images any faster (the compositor still holds them for one refresh).  Lesson: when the wall-clock-floor is `~vsync_period`, the gate is *probably* SurfaceFlinger, not anything inside bgfx or the GPU.

**Root cause: bgfx defaults `Resolution::numBackBuffers = 2`** (`bgfx.cpp:3764`).  With a 2-image swapchain, the render thread can be at most ONE frame ahead of the compositor; the next `vkAcquireNextImageKHR` blocks until SurfaceFlinger releases an image, which it does at 60 Hz panel refresh.  That ~16 ms wait shows up on the render thread, fills the ring, back-pressures the game thread's `bgfx::frame()` handoff, and produces the exact 13.84 ms `waitSubmit` the team measured.

**Fix landed today** (`engine/rendering/Renderer.cpp:167-192`): on Android, `init.resolution.numBackBuffers = 3`.  bgfx clamps the requested count to `[surfaceCapabilities.minImageCount, surfaceCapabilities.maxImageCount, BGFX_CONFIG_MAX_BACK_BUFFERS]` (`renderer_vk.cpp:7697-7713`) so requesting 3 on a surface that requires fewer / allows more is safe.  Apple / Metal and desktop bgfx backends manage swapchain depth differently and weren't part of the reported problem, so the change is Android-only.

**Verification on the team's side.**  Rebuild against `main`, then `adb logcat -s SamaEngineBgfx`.  At init time bgfx now prints (because we have the trace callback wired):

```
I SamaEngineBgfx: Create swapchain numSwapChainImages 3, minImageCount 3, BX_COUNTOF(m_backBufferColorImage) 8
```

If `numSwapChainImages` comes back as 3, this fix took effect.  If it's still 2, the device's `maxImageCount` clamps it to 2 (unlikely on Pixel 9 but possible on lower-tier devices) and we need hypothesis 2 (semaphore-driven acquire pipeline) instead.

Expected per-frame numbers if this hypothesis is correct:
- `waitSubmit` drops from ~14 ms toward 0 (or whatever fraction of one vsync period the GPU work + 3-frame buffering still leaves).
- `bgfx::frameMs` drops correspondingly — likely to the ~0.1 ms range when GPU work fits comfortably inside vsync.
- Steady-state FPS climbs from 45-50 toward the panel's 60.

**Tradeoff accepted.** Triple-buffering adds one frame of input latency.  At 60 Hz that's ~16.67 ms additional delay between an input event and the pixel it affects.  Acceptable for the engine's current target workload (3D games with PBR, not competitive twitch shooters); for input-sensitive games we'd expose this via `EngineDesc` so games can opt back to 2.

**If hypothesis 1 doesn't fully close the gap.**  Two remaining paths the integrating team identified:
- *Hypothesis 2 — semaphore-driven acquire pipeline.*  Even with 3 images, if bgfx's Vulkan path calls `vkAcquireNextImageKHR` inline on the render thread (synchronously) rather than feeding the acquire semaphore into the next frame's submit, the render thread still blocks on acquire.  This is a bgfx upstream investigation — read `renderer_vk.cpp` around `m_backBuffer.acquire` to see whether bgfx uses the acquire-semaphore pattern or polls.
- *Hypothesis 3 — Choreographer cadence is the floor.*  Android's SurfaceFlinger respects `Choreographer` callbacks; an app can't beat the panel's refresh cadence regardless of swapchain depth.  If the ~16 ms floor is fundamental, the doc's old "~0.1 ms" claim was always wrong for Android and the doc needs an Android-specific acceptance bar (target: `bgfx::frameMs` ≤ vsync_period − (CPU work + GPU work), not ≤ 0.1 ms).

The team will report after they rebuild and try the 3-image fix.

### Follow-up (2026-06-18, hypothesis 1 ruled out — present mode is the real bug)

The team reported back: triple-buffering did not close the `waitSubmit` gap on Pixel 9.  bgfx's swapchain trace confirmed `numSwapChainImages` came back as 3, so the request was honoured — the gap is somewhere else.

Reverted the `numBackBuffers = 3` change (it was harmless but unhelpful; one frame of input latency for no benefit isn't a tradeoff anyone would take).  Investigation moved to hypothesis 2: how bgfx invokes `vkAcquireNextImageKHR`.

**Reading bgfx's Vulkan backend (`build/_deps/bgfx_cmake-src/bgfx/src/renderer_vk.cpp`):**

- `SwapChainVK::acquire` (line 8167) calls `vkAcquireNextImageKHR` with `UINT64_MAX` timeout and a semaphore for GPU-side ordering.  The CPU call blocks if no image is available; the semaphore only gates GPU work that uses the image.
- `FrameBufferVK::acquire` (line 8538) wraps that.
- `RendererContextVK::setFrameBuffer` (line 3042) calls it whenever a view's framebuffer transitions to the backbuffer.  Per frame, that fires once at the first opaque pass.
- Per-frame sequence on the render thread:  acquire (blocks if no image) → build commands → `vkQueueSubmit` (waits on acquire semaphore) → `vkQueuePresentKHR`.

That's all standard — the bug isn't in *how* bgfx invokes acquire; it's in *which present mode it asked for*.  And the present mode picker is where the actual bug lives.

**The real smoking gun.** `s_presentMode` at `renderer_vk.cpp:151`:

```cpp
static const PresentMode s_presentMode[] =
{
    { VK_PRESENT_MODE_FIFO_KHR,         true,  ... },  // [0] picked first when vsync=true
    { VK_PRESENT_MODE_FIFO_RELAXED_KHR, true,  ... },  // [1]
    { VK_PRESENT_MODE_MAILBOX_KHR,      true,  ... },  // [2] NEVER REACHED
    { VK_PRESENT_MODE_IMMEDIATE_KHR,    false, ... },  // [3]
};
```

`findPresentMode` (line 8104) walks this array in order, picking the first whose `vsync` field matches the requested setting.  With `BGFX_RESET_VSYNC` set (the default), `_vsync == true`, so `s_presentMode[0]` = FIFO matches immediately and the loop exits.  MAILBOX is at index `[2]` with `vsync=true` but is *never reached*.

**FIFO** is the present mode where `vkAcquireNextImageKHR` blocks until SurfaceFlinger releases an image back to the swapchain.  On Android that happens at panel refresh — ~16.67 ms at 60 Hz.  The wait charges to the render thread, fills the bgfx command ring, and back-pressures the game thread's `bgfx::frame()` handoff.  Exact match for the team's 13.84 ms `waitSubmit`.

**MAILBOX** would let the render thread keep submitting frames; the compositor takes the most recent at refresh and discards earlier unpresented ones.  Same no-tearing guarantee as FIFO; no blocking on acquire when the swapchain has free images.  Pixel 9 / Mali-G715 supports MAILBOX (it's a standard mode on most Android Vulkan devices, including all of Tensor G2 / G3 / G4 family).

This means even when the team disabled vsync (the earlier diagnostic), bgfx fell through to `findPresentMode(false)` → looked for IMMEDIATE → not supported on Pixel 9 → fell back to `s_presentMode[0]` (FIFO with the "Defaulting to" trace at line 8161 — which we now see in logcat thanks to the wired callback).  So vsync-off was FIFO too.  Both the vsync-off result and the GPU-strip result were "FIFO blocking on acquire" — exactly the same condition.

**Fix landed today.**  New patch `patches/bgfx_android_mailbox_present.patch` reorders `s_presentMode` so MAILBOX is checked first (when `vsync=true`), falling through to FIFO when the device doesn't support MAILBOX.  Also adds a `BX_TRACE` printing the selected mode so the team can verify in logcat:

```
I SamaEngineBgfx: Selected present mode: VK_PRESENT_MODE_MAILBOX_KHR (vsync requested: 1)
```

The patch is wired into the existing `FetchContent_Declare(bgfx_cmake)` `PATCH_COMMAND` chain alongside three other bgfx patches we already carry.  Applied automatically every fresh fetch; idempotent (`git apply` no-ops on already-applied patches).

**Why this isn't just `init.resolution.reset &= ~BGFX_RESET_VSYNC`.**  Without `BGFX_RESET_VSYNC`, bgfx looks for IMMEDIATE (true tearing).  If the device doesn't support IMMEDIATE — which Android Vulkan rarely does — it falls back to FIFO anyway.  IMMEDIATE also gives screen tearing, which we don't want in production.  MAILBOX gives the throughput of vsync-off without tearing.

**Tradeoff.**  MAILBOX renders more frames than the panel can display; the discarded ones are GPU work that produced no pixels.  For a frame that genuinely fits in the vsync period, MAILBOX renders one and the next acquire returns immediately — same effective throughput as FIFO would have.  For a frame that doesn't fit, MAILBOX renders as fast as the GPU allows, the compositor takes the most recent at refresh, and the game thread can keep submitting.  Result: same wall-clock per frame as FIFO when the GPU is fast enough, faster when it isn't.  Power cost: MAILBOX can draw the GPU into rendering frames that are discarded — for a battery-sensitive game this matters, and MAILBOX shouldn't be the default if the engine is targeting "draw exactly one frame per refresh."  The integrating team's game is GPU-bound enough that this isn't the dominant power draw; the audit's `#P2` `Surface.setFrameRate(30)` plumbing is the better long-term answer for power.

**Upstreaming.**  The reorder is a one-line semantic improvement; worth a PR to bgfx upstream.  Open question for the maintainer: why was MAILBOX originally placed after FIFO?  The most charitable reading is "FIFO is universally supported, so prefer it for portability" — but the search-by-vsync-flag pattern means MAILBOX is never selected even on devices that support it, which is the worst of both worlds.  PR draft message + the patch are ready to send.

**What happens next.**  Team rebuilds against `main`, runs once, captures `adb logcat -s SamaEngineBgfx` from init through a steady-state 5 s sample.  Two lines to look for:

```
I SamaEngineBgfx: Selected present mode: VK_PRESENT_MODE_MAILBOX_KHR (vsync requested: 1)
I SamaEngineBgfx: Create swapchain numSwapChainImages 3, ...
```

If both appear, the configuration is correct.  Then the `SamaEngineBgfxStats` line every 120 frames will show whether `waitSubmit` dropped:

- Hypothesis 1+2 confirmed dead, **hypothesis 2 was about how bgfx invokes acquire and the answer is "fine"** — the bug was in the present mode picker.
- Expected new numbers: `waitSubmit` → small fraction of a ms (no longer vsync-bounded), `bgfx::frameMs` → close to hand-off cost, FPS → panel limit, GPU utilisation visibly higher (because MAILBOX may render more frames than the panel displays).

**If MAILBOX still doesn't help.**  Three remaining possibilities:
1. Pixel 9's MAILBOX implementation has a bug where it still acquires synchronously (unlikely but possible on early Tensor drivers).
2. The compositor enforces refresh-rate gating at `vkQueuePresentKHR` regardless of present mode.  In that case the `present()` call blocks and the wait moves there — visible in a frame-pacing trace.
3. The acquire-semaphore-into-next-submit pattern that bgfx uses is correct but something else upstream of acquire (image layout transition?  fence wait?  command buffer alloc?) is the actual blocker — would need a Vulkan frame capture (RenderDoc Android / Android GPU Inspector) to see.

But MAILBOX should be sufficient.  This is the most likely "right answer."

### Follow-up (2026-06-18, vk per-call timing instrumentation)

Team rebuilt with the MAILBOX patch and reported back: it did **not** close B2 on Pixel 9.  10 consecutive `SamaEngineBgfxStats` samples on the figure-8 level (idle ball) post-patch:

```
bgfx::frameMs = 9.21, 24.77, 10.85, 16.86, 21.47, 14.06, 16.20, 19.97, 17.91, 14.85
mean 16.6 ms, median 17.4 ms
```

Still pinned to the vsync floor.

**But MAILBOX did engage.**  The `Selected present mode` BX_TRACE line didn't surface in their logcat (`BX_CONFIG_DEBUG=0` in release builds — bgfx's `BX_TRACE` is a no-op there even when our callback is wired).  So we can't read it directly.  *But* the **9.21 ms outlier** in the sample is the giveaway: under strict FIFO, `vkAcquireNextImageKHR` blocks at panel refresh and no frame can be < ~16.7 ms at 60 Hz.  Some frames sneaking under that floor means MAILBOX is active and the compositor enforces refresh-rate gating *somewhere else* in the present pipeline.

That maps directly onto the team's own fallback hypothesis 2 from the previous follow-up:

> The compositor enforces refresh-rate gating at `vkQueuePresentKHR` regardless of present mode.  In that case the `present()` call blocks and the wait moves there.

The mean stayed at vsync; the wait just *moved*.  Acquire is no longer the gate, but something is.

Also: `dumpsys gfxinfo` shows 4 GraphicBufferAllocator entries.  After we reverted `numBackBuffers = 3`, that count is Android's surface-side BLAST Consumer triple-buffer plus one in-flight image — bgfx's default 2-deep swapchain ask plus the compositor's own buffering.  Nothing else we need to tune at the bgfx level for buffer count.

**Next diagnostic.**  Time each of the four blocking Vulkan calls on the render thread individually and print every 60 frames.  New patch `patches/bgfx_android_vk_call_timing.patch` adds file-static accumulators in the bgfx VK backend at:

- `SwapChainVK::acquire` → `vkAcquireNextImageKHR`
- `CommandQueueVK::kick` → `vkQueueSubmit`
- `CommandQueueVK::kick` (after submit, when `_wait=true`) → `vkWaitForFences`
- `SwapChainVK::present` → `vkQueuePresentKHR`

Captures each call's wall-clock via `bx::getHPCounter` and prints all four at the natural end-of-frame point inside `present()`:

```
I SamaEngineBgfx: Sama vk per-call ms: acquire=0.030 submit=0.080 waitFences=2.10 present=13.85
```

Single-frame snapshot (not a 60-frame average) — for a steady-state diagnostic the math is easier to read.  60-frame gate keeps logcat readable.  `bx::getHPCounter` is a handful of cycles; overhead is negligible against the calls being measured.

**Reading the output.**

| Pattern | Hypothesis | Next move |
|---|---|---|
| `acquire ~0 ms`, `present ~14 ms` | Compositor gates at `vkQueuePresentKHR` (the team's hypothesis 2) | Upstream of bgfx — `Surface.setFrameRate(targetFPS)` plumbing (audit's `#P2`) or `ANativeWindow_setFrameRate` with `compatibility=ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT` |
| `acquire ~0 ms`, `present ~0 ms`, `waitFences ~14 ms` | GPU completion is the gate (not present)—`waitFences` is held until the GPU finishes the prior frame | RenderDoc Android / Android GPU Inspector capture; check `gpuTime` ≠ `gpuLatency` |
| `acquire ~0 ms`, `present ~0 ms`, `waitFences ~0 ms`, `submit ~14 ms` | Command-buffer alloc or driver compile (rare in steady state) | Vulkan validation layer dump on driver hot path |
| Time vanishes between calls (sum << 14 ms) | The gap is *upstream* of the four instrumented calls — bgfx internal scheduling, mutex, or `bgfx::renderFrame` API loop | Bigger instrumentation pass at the bgfx control loop layer (`bgfx_p.h` Context::renderFrame) |

**Crucially: this is one APK rebuild.**  Same `Sama vk per-call ms` log line surfaces regardless of which hypothesis is correct — the team reads the four numbers from one logcat session and we know.

**The Surface.setFrameRate path (audit #P2).**  If the `present ~14 ms` pattern is what we see, the answer is what the audit already flagged.  Currently `engine::rendering::TierConfig::targetFPS` is parsed and stored but no Android-side panel-rate negotiation exists.  Need to call `ANativeWindow_setFrameRate(window, targetFPS, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT)` from the platform init code after the native window is acquired and before bgfx::init.  Android 11+ negotiates a matching display mode (e.g. 60 Hz on a 120 Hz panel); below 11 it's a no-op, which is fine.  That fixes the compositor's refresh-rate gating at the source — the panel runs at the rate we asked for and `vkQueuePresentKHR` returns immediately to the cadence we want.

**Tradeoff.**  Per-call timing instrumentation stays unconditional in the bgfx patch (no `#ifdef`).  Cost on the render thread per frame: 4× `bx::getHPCounter` calls + 4× subtraction + 1× modulo + (every 60 frames) one `BX_TRACE`.  Sub-microsecond.  Negligible against any frame-pacing target.  Leaving it in even after the bug is found gives us a permanent diagnostic for future Android perf regressions.

The patch is appended to the existing `FetchContent_Declare(bgfx_cmake)` `PATCH_COMMAND` chain after `bgfx_android_mailbox_present.patch`.  Applied automatically on fresh fetch; idempotent.  Builds clean on macOS desktop (engine_tests 26981/753 pass) and Android arm64-v8a (bgfx target).

### Follow-up (2026-06-18, root cause found — SUBOPTIMAL_KHR swapchain rebuild loop)

Team rebuilt with `-DBX_CONFIG_DEBUG=ON` so bgfx's BX_TRACE actually fires (it's a no-op in release; the wired callback was correct but had nothing to route).  The first capture surfaced the real bug in two lines of logcat repeating every 15-17 ms for the entire activity lifetime:

```
SamaEngineBgfx: BGFX vkQueuePresentKHR(...): result = VK_SUBOPTIMAL_KHR
SamaEngineBgfx: BGFX Create swapchain numSwapChainImages 5, minImageCount 5, BX_COUNTOF(m_backBufferColorImage) 10
SamaEngineBgfx: BGFX Successfully created swapchain (2251x1080) with 5 images.
```

**bgfx tears down and recreates the entire swapchain on every single frame** because Pixel 9 / Android 16 returns `VK_SUBOPTIMAL_KHR` from every `vkQueuePresentKHR`.  Cost: `vkDeviceWaitIdle` + `vkDestroySwapchainKHR` + `vkCreateSwapchainKHR` + image-view recreation per frame ≈ 15 ms.  **That is what every prior measurement was capturing.**  Backbuffer count, present mode, GPU stripping — all irrelevant because the swapchain dies the instant after the frame ships and the work is thrown away.

Why every prior fix failed:
- Triple-buffer (#6a0cd65) — fresh swapchain still rebuilt next frame
- MAILBOX (#089fe5b) — fresh swapchain still rebuilt next frame
- Per-call timing (#4a024e8) — would have shown the time inside present, but the real cost is the `recreateSwapchain` path AFTER present returns

**Why SUBOPTIMAL is permanent on Pixel 9 / Android 16.**  Almost certainly the display-cutout situation: activity drawable region is 2251×1080 (visible in `dumpsys SurfaceFlinger` as `displayCutoutSafeInsets=Rect(173, 0 - 0, 0)`), but the Vulkan surface's `currentExtent` reports the full panel `2424×1080`.  Swapchain created at the smaller activity-region size perpetually SUBOPTIMALs against the surface's preferred extent.  Rebuilding does NOT clear the flag — the new swapchain is still at the smaller size, the next present returns SUBOPTIMAL again, and we loop forever.

Also: `numSwapChainImages` came back as 5 (not the 3 we'd asked for in the earlier #R7 attempt) because Pixel 9's `minImageCount` is 4.  Our `numBackBuffers=3` setting was overridden by the device requirement.  Independent of the bug — but explains why that earlier diagnostic showed no win.

**The fix.**  New patch `patches/bgfx_android_vk_suboptimal_no_recreate.patch` splits the present-result and acquire-result switches in `renderer_vk.cpp`:

- `VK_ERROR_OUT_OF_DATE_KHR` — keep existing behaviour (must recreate)
- `VK_SUBOPTIMAL_KHR` — treat as `VK_SUCCESS`, log once, don't recreate

One-shot trace gated by a file-static bool so logcat surfaces "we hit SUBOPTIMAL once and chose to ignore it" without flooding the log when the surface reports SUBOPTIMAL every frame.

**Why this is correct, not just a workaround.**  The Vulkan spec explicitly allows applications to keep using a swapchain that returns `VK_SUBOPTIMAL_KHR` (VkSwapchainKHR(3) man page, Vulkan Guide).  It's the *standard* pattern — Unreal, Unity, Godot all ignore SUBOPTIMAL by default.  bgfx's conservative "always rebuild" is the outlier; it predates platforms where SUBOPTIMAL became permanent.  The frame was acquired / presented correctly, just to a swapchain the surface considers non-ideal.  Rendering at 2251×1080 is what we want — that's the cutout-safe area.  We accept "surface technically allows larger" forever; the visible output is identical to a non-suboptimal pass.

**Two-paths analysis.**

| Path | Description | Cost | Verdict |
|---|---|---|---|
| 1 (this patch) | Treat SUBOPTIMAL as success in bgfx | 2-line semantic split in 2 case statements | Right answer today |
| 2 (eventual) | Create swapchain at `currentExtent` (full panel), use viewport+scissor to crop to cutout-safe area | Plumb cutout insets from Android platform layer to renderer; allocate larger swapchain images we never sample | Right answer for future cutout-aware rendering (status bar reveal, edge-to-edge) |

Path 1 lands now and unblocks the team; Path 2 is the proper long-term answer when we add cutout-aware fullscreen.  They don't conflict.

**Expected impact.**  `bgfx::frameMs` drops from ~16 ms (vsync-pinned by per-frame recreation cost) to ~0.1 ms — the original docs claim.  Per-call timing from #4a024e8 should now show all four Vulkan calls in sub-ms territory.  FPS reaches panel limit (60 Hz; or 120 Hz if `Surface.setFrameRate(120)` is plumbed via audit's #P2).  No visible artefact: the 2251×1080 activity-region was the correct render target all along; we just stop discarding our work every frame.

**Honesty about hypothesis tree.**  Looking back at the prior follow-ups, the actual bug was *not* in any of the five enumerated hypotheses (vsync / GPU / silent single-threaded fallback / swapchain depth / present mode).  It was a hidden sixth: "bgfx's response to a soft-hint result code is a hard rebuild."  The per-call timing patch (#4a024e8) was a step in the right direction — it would have revealed the cost as soon as we surfaced "present completes in 0.1 ms but the next frame's acquire takes 15 ms because the swapchain just got destroyed and recreated."  In hindsight the right starting move from day one would have been "look at every BX_TRACE bgfx emits at init and per frame" — which only worked once the team flipped `BX_CONFIG_DEBUG=ON`.  Logging gates that silence themselves in release builds are how production engines hide bugs from themselves.

**Status.**  Patch wired into the `FetchContent_Declare(bgfx_cmake)` `PATCH_COMMAND` chain after the timing patch.  Builds clean on macOS desktop (engine_tests 26981/753 pass) and Android arm64-v8a (bgfx target).  Awaiting team's verification on Pixel 9 — single `adb logcat -s SamaEngineBgfx:V` capture should show:
- One `Sama: vkQueuePresentKHR returned VK_SUBOPTIMAL_KHR — treating as success` line
- `SamaEngineBgfxStats` showing `bgfx::frameMs` < 1 ms, `waitSubmit` ~ 0, FPS at panel rate
- No more `Create swapchain` / `Successfully created swapchain` repeating

**Upstream PR.**  Worth proposing to bgfx alongside the MAILBOX reorder.  Either ignore SUBOPTIMAL entirely or expose a flag (`BGFX_RESET_TOLERATE_SUBOPTIMAL` or similar).  Both patches share a theme: "bgfx's defaults are conservative in ways that misfire on modern Android."

### Follow-up (2026-06-18, second trigger — clamped-vs-requested resolution rebuild loop)

With the SUBOPTIMAL patch in, the team reported back: still 648 `Create swapchain` lines over 12 s (was ~600 before, so right back where we started).  `bgfx::frameMs` 12-19 ms, `waitRender` 11-17 ms.  SUBOPTIMAL closed one trigger; a second one independent of it was perpetuating the rebuild loop.

**The second trigger.**  In `RendererContextVK::updateResolution()` (renderer_vk.cpp:2964), the comparison that decides "did the app ask for a different swapchain" is at lines 3003-3004:

```cpp
||  m_resolution.width  != _resolution.width
||  m_resolution.height != _resolution.height
```

`m_resolution.width/height` is overwritten with the *clamped* swapchain extent at lines 3027-3028 (with an explanatory comment at line 3025: *"Update the resolution again here, as the actual width and height is now final (as it was potentially clamped by the Vulkan driver)"*).  So after a clamp, `m_resolution.width = 2251` (the cutout-safe extent).  But `_resolution.width` next frame is still 2424 (what sama asked for from `ANativeWindow_getWidth()`).  Mismatch → recreate → clamp → mismatch → loop.  Self-sustaining; SUBOPTIMAL doesn't even need to fire.

The bug is using the same field for two purposes: "what should the rest of bgfx see" (correct: clamped) and "did the application ask for something new" (incorrect — should compare against last-requested, not clamped-actual).

**Fix.**  New patch `patches/bgfx_android_vk_clamped_resolution_no_recreate.patch` — three small changes to `RendererContextVK`:

1. Add `uint32_t m_lastRequestedWidth / m_lastRequestedHeight` to the struct (shadow the un-clamped request).
2. Prime them at init time alongside `m_resolution`.
3. In `updateResolution`:
   - Compare `m_lastRequestedWidth/Height` instead of `m_resolution.width/height`.
   - Inside the recreate block, store the *requested* values into the shadow BEFORE the backbuffer-clamping line.

Next-frame comparison sees `m_lastRequestedWidth(2424) == _resolution.width(2424)` → no recreate.  `m_resolution.width(2251)` is still what every viewport / scissor / render-target sizing path in bgfx reads.

**Why the bgfx patch over the engine workaround.**  The team flagged a sama-side alternative: read `bgfx::getStats()->width` after `bgfx::reset()` and feed *that* into our engine's framebuffer-dims check.  It would work for sama specifically (single line), but couples the engine to bgfx's internal clamping behaviour — fragile across bgfx version bumps, and any other game on a cutout device hits the same bug.  The bgfx patch fixes the root cause and joins the upstream PR chain alongside MAILBOX + SUBOPTIMAL.  Three small renderer_vk patches together describe one coherent "make bgfx behave on modern Android" change.

**Expected.**  `Create swapchain` count drops from ~650 to 1 over the activity lifetime.  `bgfx::frameMs` from ~16 ms to ~0.1 ms.  Per-call timing from `bgfx_android_vk_call_timing.patch` should now show all four VK calls sub-ms.  FPS at panel rate.  Render output identical — clamped 2251×1080 was the correct visible area all along.

**Audit of the patch surface.**  Five renderer_vk patches now:

| Patch | What it does | Triggered the bug? |
|---|---|---|
| `bgfx_emulator_compat.patch` | Gate VK_KHR_fragment_shading_rate chaining on extension support | (Unrelated — Android emulator gfxstream crash) |
| `bgfx_mali_shadow_fix.patch` | Mali-specific shadow rendering fix | (Unrelated) |
| `bgfx_android_mailbox_present.patch` | Reorder present mode table so MAILBOX is checked first | Speculative fix; landed before SUBOPTIMAL was found |
| `bgfx_android_vk_call_timing.patch` | Time vkAcquire/Submit/Present/WaitFences per-frame | Diagnostic only; stays in for future regressions |
| `bgfx_android_vk_suboptimal_no_recreate.patch` | Treat VK_SUBOPTIMAL_KHR as success | Closed trigger 1 (every present returned SUBOPTIMAL) |
| `bgfx_android_vk_clamped_resolution_no_recreate.patch` | Compare last-requested resolution, not clamped-actual | Closes trigger 2 (this commit) |

The MAILBOX patch is still wanted — it's an unrelated improvement that lets us run "vsync-on without acquire blocking" when the panel can accept it.  The timing patch stays in.  The two `no_recreate` patches are the actual fix.

**Status.**  Built clean on macOS desktop (engine_tests 26981/753 pass) and Android arm64-v8a (bgfx target).  Awaiting team's Pixel 9 verification — `adb logcat -s SamaEngineBgfx:V` should show:
- `Create swapchain` exactly once (at init) instead of ~650 times
- `Successfully created swapchain` exactly once
- `SamaEngineBgfxStats` with `bgfx::frameMs` < 1 ms, FPS at panel rate
- One `Sama: vkQueuePresentKHR returned VK_SUBOPTIMAL_KHR — treating as success` line (the SUBOPTIMAL is still happening every frame; we just stopped acting on it)

### Resolution (2026-06-18, B2 closed on Pixel 9)

Team verified on Pixel 9 / Android 16:

| Metric | Before | After |
|---|---|---|
| `bgfx::frameMs` | ~16 ms (vsync-pinned) | **0.27 – 6.27 ms** |
| `vkQueuePresentKHR` time | ~14 ms | **0.1 – 2 ms** |
| `Create swapchain` count | ~650 per 12 s | **1 (init only)** |
| Panel refresh | 60 Hz | **120 Hz (auto-promoted)** |
| Uncapped FPS counter | ~50 | **~336** |

Root cause confirmed exactly as diagnosed: SUBOPTIMAL→recreate loop (closed by `bgfx_android_vk_suboptimal_no_recreate.patch`) **plus** a second recreate trigger from the driver's 2424→2251 cutout clamp being compared against the original request next frame (closed by `bgfx_android_vk_clamped_resolution_no_recreate.patch`).  Both patches were necessary; either alone would have left the other loop active.

The 120 Hz auto-promotion is a free bonus: Android's `Choreographer` reads "this app is consistently submitting frames in <8 ms" and switches the panel into 120 Hz mode automatically.  This means audit `#P2` (`Surface.setFrameRate()` plumbing) is now lower priority — the system is doing the right thing without us asking.  We'd still want explicit `setFrameRate()` for cases where we want to *cap* below the panel rate (battery-sensitive scenes, 30 Hz fallback for thermal throttling); but the auto-uncap-to-120 path is good enough for the steady state.

### Operational lesson (2026-06-18, FetchContent_Declare PATCH_COMMAND only runs once)

**The bear trap that turned a multi-hour fix into multi-week debugging.**

CMake's `FetchContent_Declare(... PATCH_COMMAND ...)` runs the patch chain *only on first extraction*.  Subsequent `cmake` reconfigures see the source directory already exists and skip both fetch and patch entirely.  If a new patch is added to the chain (or any existing patch changes), the bgfx tree in an already-built `_deps/` directory is silently left unpatched.

The team's `sample_game` Android build tree was extracted in April.  Every commit since then that added a renderer_vk patch (MAILBOX, call-timing, SUBOPTIMAL, clamped-resolution) appeared correctly in `git pull` but never applied to the Android `_deps/bgfx_cmake-src/`.  Reconfigure was a no-op; rebuild was a no-op for renderer_vk.cpp because the source hadn't changed.  Their builds compiled the unpatched April source for weeks while the upstream patches all looked correct in git.

**Workaround the team used.**  `rm -rf build/android/arm64-v8a/_deps/bgfx_cmake-{src,subbuild}` forces re-extract + re-patch on next configure.  Alternatively, manually `git apply` the four patches in-place against the existing tree and force a rebuild of `bgfx.dir`.

**Two real fixes worth considering:**

1. **Stamp-file approach (recommended):** compute a hash of the patch files in `CMakeLists.txt`, store it in `${CMAKE_BINARY_DIR}/_deps/bgfx_cmake-patches.stamp`, and on every configure check whether the hash changed.  If it did, blow away `_deps/bgfx_cmake-{src,subbuild}` to force re-extract.  ~15 lines of CMake.  Trade-off: any patch chain change triggers a full bgfx rebuild — acceptable because bgfx changes are rare (months apart) and the alternative is silent-wrong-binary.

2. **Migrate off `FetchContent_Declare PATCH_COMMAND`:** vendor bgfx as a git submodule and apply patches via a shell script that's idempotent (`git apply --check && git apply` per patch).  More invasive; trades the FetchContent automation for explicit control.  Probably overkill until we hit this trap a second time.

**Detection-only fallback (cheap):** add a CMake `file(STRINGS ... LIMIT_COUNT 1)` check that greps the on-disk renderer_vk.cpp for a marker string from the most recent patch (e.g. `s_samaSuboptimalLogged`).  If missing, `message(FATAL_ERROR "bgfx patches are stale — rm -rf ${_deps_dir} and reconfigure")`.  ~5 lines.  Doesn't auto-fix but makes the failure mode loud at configure time instead of silent at runtime.

**Lesson for future bgfx patches.**  Any commit that touches `patches/bgfx_*` should mention in the PR description: *"Existing builds need `rm -rf build/<target>/_deps/bgfx_cmake-{src,subbuild}` to pick up this patch.  CI does this implicitly; local devs and downstream teams may not."*  Until/unless we wire up the stamp-file fix.

This trap exists for every `FetchContent_Declare` we have with `PATCH_COMMAND` — not just bgfx.  Audit candidates: any future fetched dependency that we patch.  Currently bgfx is the only one in the chain.

---

## Physics

### Decisions
- **Library:** Jolt Physics
  - Modern C++17, built for multi-threading from day one
  - Covers rigid bodies, soft body/cloth, and vehicle simulation
  - Chosen over Bullet: aging API, basic vehicle support
  - Chosen over PhysX: too heavy (~10MB+), NVIDIA-owned IP; revisit PhysX if racing game vehicle fidelity demands it
- **Integration pattern:** Abstract behind an `IPhysicsEngine` interface (rigid body, soft body, vehicle, step)
  - Allows swapping in PhysX or another backend later without touching game code
- **Soft body:** Jolt's soft body is reasonably mature (edge/bend/volume constraints, pressure, soft↔rigid collision, GPU hair strands)
  - Known gap: hair-environment collision limited to ConvexHull and CompoundShapes only

### Side Projects
- **Jolt soft body contributions:** Planned side project to improve Jolt's soft body support upstream
  - Most actionable gap: expand hair/strand collision to support mesh and heightfield shapes
  - Other candidates: GPU-accelerated soft body simulation (currently CPU only), large soft body count performance
  - Action: open a GitHub discussion with maintainer (jrouwe) before writing any code to align on what's needed
  - Start this after integrating Jolt into the engine — real usage will surface the most painful gaps

### Status
- [x] Physics implemented (IPhysicsEngine, JoltPhysicsEngine, PhysicsSystem, raycasting, collision events — committed in engine/physics/)
- [x] Compound + mesh collider shapes landed (`ColliderShape::Compound`, real `ColliderShape::Mesh`). Shapes are pre-built via `IPhysicsEngine::createCompoundShape()` / `createMeshShape()` and referenced by `uint32_t shapeID` on `ColliderComponent`. Lifetime: shape outlives `destroy*Shape` until the last body referencing it is removed (Jolt's `JPH::ShapeRefC` refcount). **Behavior change:** `ColliderShape::Mesh` is no longer a silent fallback to `Box` — callers using `Mesh` without setting `shapeID` will now get a body-create failure instead of an unintended box. This was a bug fix; the previous fallback was undocumented and silently wrong.

---

## Audio

### Decisions
- **Libraries:** SoLoud + miniaudio
  - miniaudio: handles platform-specific audio output (CoreAudio, AAudio, WASAPI) and decoding (MP3, OGG, WAV, FLAC), ~300–500KB
  - SoLoud: game-facing layer — 3D positioning, distance attenuation, Doppler, mixing, looping, fading, ~500KB–1MB
  - Chosen over FMOD: binary size (~3–6MB) and licensing (royalties above $200k revenue) not worth it for a solo developer at this stage
  - FMOD noted as upgrade path for AAA audio polish (HRTF, geometry-aware reverb, native occlusion, sound designer workflow)
- **Integration pattern:** Abstract behind an `IAudioEngine` interface (play3D, updatePosition, setListener, playMusic, stop)
  - FMOD implementation can be dropped in later without touching game code
- **3D positional audio:** Supported via SoLoud's built-in 3D system (distance attenuation, panning, Doppler)
- **Occlusion:** Implemented in the engine's `SoLoudAudio` layer using Jolt raycasts — no SoLoud modifications needed
  - Raycast between sound source and listener each frame
  - Map occlusion factor to volume reduction + low-pass filter cutoff via SoLoud's `BiquadResonanceFilter`
  - Support partial occlusion (e.g. cracked door) by casting multiple rays and averaging occlusion factor
  - When/if FMOD is adopted, remove this logic — FMOD handles occlusion natively

### Status
- [x] Audio implemented (IAudioEngine, SoLoudAudioEngine, AudioSystem, NullAudioEngine — SoLoud + miniaudio backend, committed)

---

## Scene Graph

### Decisions
- **Approach:** ECS-as-scene-graph — hierarchy encoded directly in ECS, no separate scene graph object
  - Components: `TransformComponent` (local position/rotation/scale), `WorldTransformComponent` (cached world matrix), `ParentComponent`, `ChildrenComponent`
  - `TransformSystem` traverses top-down each frame, recomputes world transforms for dirty nodes only
- **Dirty flagging:** Required — with ~50k nodes, recomputing all world transforms every frame is too costly
- **Scene serialization:** Needed early
  - JSON during development (human-readable, git-diffable)
  - Binary format for shipped builds (fast to parse, compact)
  - Build step bakes JSON → binary at build time
  - JSON parser: **rapidjson** (see JSON section below)
- **GPU instancing:** Separate system from the scene graph, needed early for vegetation/foliage/rocks
  - Millions of instances (grass, trees, pebbles) rendered via single draw calls — not scene graph nodes
- **Scale target:** ~20k–50k actively managed scene graph nodes at runtime, with region-based streaming loading/unloading around the player

### Status
- [x] Scene graph implemented (HierarchyComponent, ChildrenComponent, TransformSystem with dirty flags, SceneGraph mutation API, GltfSceneSpawner integration — committed)

---

## JSON

### Decision: rapidjson

**Library:** rapidjson (~300KB header, MIT + BSD, Tencent)

Chosen over nlohmann/json (~1MB header, MIT) for two reasons that directly match the external library policy priority order:

1. **Binary size** — rapidjson headers are ~300KB vs ~1MB. Headers compile into every translation unit that includes them. The difference is real across a project of this size, and binary size is the primary evaluation criterion.
2. **Performance** — The asset manifest (fetched at launch, potentially thousands of entries) is the only JSON file parsed in the shipped binary that is large enough for parse speed to matter. rapidjson's SAX mode with its `MemoryPoolAllocator` parses it with near-zero heap allocation. nlohmann/json builds a full DOM from `std::map`/`std::unordered_map`, which allocates heavily.

**Rejected: nlohmann/json** — better ergonomics, but the API advantage is negated by wrapping rapidjson (see below). The 3× size penalty is not justified.

### Where JSON is used

| Use site | File size | In shipped binary |
|---|---|---|
| Asset manifest (streaming delta) | Large — thousands of entries | Yes — parsed at launch |
| Player config (RenderSettings, action maps) | Tiny | Yes — parsed at startup |
| Scene files | Medium | No — baked to binary at build time; JSON is dev/editor only |
| Build pipeline (JSON → binary bake) | Any | No — offline tooling |

### Integration pattern: `engine::io` wrapper

rapidjson's API is not exposed directly anywhere in engine or game code. All JSON access goes through a thin wrapper at `engine/io/Json.h`. This keeps rapidjson's verbose type-checking and manual iteration internal, and means the parser can be swapped by changing one file.

```cpp
// engine/io/Json.h — what the rest of the engine sees
namespace engine::io
{
    class JsonDocument { /* owns the parsed document + allocator */ };
    class JsonValue    { /* non-owning view into a value node */ };

    JsonDocument     parseJson(std::string_view text);
    JsonDocument     parseJsonFile(std::string_view path);

    JsonValue        get(JsonValue obj, std::string_view key); // null JsonValue if missing
    bool             getBool(JsonValue, bool fallback = false);
    int32_t          getInt(JsonValue, int32_t fallback = 0);
    float            getFloat(JsonValue, float fallback = 0.0f);
    std::string_view getString(JsonValue, std::string_view fallback = {});
    bool             isNull(JsonValue);
    bool             isArray(JsonValue);
    std::size_t      arraySize(JsonValue);
    JsonValue        arrayElement(JsonValue, std::size_t index);

    class JsonWriter { /* SAX writer wrapping rapidjson::Writer<StringBuffer> */ };
}
```

**Writing** uses rapidjson's SAX writer (streaming, no intermediate DOM) — efficient for serialising large scene files during the build bake step.

**Manifest parsing** uses SAX mode directly inside the asset system's manifest parser — the only site that needs high throughput. It processes entries as a stream rather than building a DOM.

### Status
- [x] rapidjson integrated (FetchContent in CMakeLists.txt)
- [x] `engine/io/Json.h` wrapper implemented (JsonDocument, JsonValue, JsonWriter with pimpl — 22 tests)

---

## Math Library

### Decisions
- **Library:** GLM (OpenGL Mathematics), header-only, ~2MB, MIT licensed
  - Chosen over custom math library: upfront cost not justified early; GLM is battle-tested and covers all needed types (Vec2/3/4, Mat3/4, Quat) and utilities (projections, intersection tests, noise)
  - Zero binary size impact (header-only)
  - GLSL-style syntax maps directly to shader code
- **Type aliases:** All engine code uses aliases — never references GLM types directly
  - Enables swapping GLM for a custom library with a one-file change
  ```cpp
  // engine/math/Types.h
  using Vec3 = glm::vec3;
  using Vec4 = glm::vec4;
  using Mat4 = glm::mat4;
  using Quat = glm::quat;
  ```
- **SIMD strategy:**
  - Enable `GLM_FORCE_INTRINSICS` from day one — free SSE2/AVX SIMD on x86/x64 (Windows desktop)
  - GLM's ARM NEON support (`GLM_FORCE_NEON`) is incomplete — weak coverage, architectural limitations (vec3 padding issues, type system not designed around SIMD registers)
  - Do NOT patch GLM's NEON support — patching is ~1000 lines of intrinsics code constrained by GLM's architecture
  - Instead: add a thin custom SIMD layer (~200–300 lines) for hot paths only if mobile profiling shows it's needed
    - `SimdMat4`, `SimdVec4` types used only inside `TransformSystem` and the renderer
    - Converts to/from GLM types at the boundary
    - Uses ARM NEON intrinsics (vld1q_f32, vmulq_f32, vmlaq_f32, vaddq_f32) directly
  - Hot paths that would benefit most: Mat4 multiply (TransformSystem world matrix updates), Vec4 arithmetic, Quat operations, frustum culling AABB tests
  - Defer custom SIMD until mobile profiling confirms TransformSystem is a real bottleneck

### Status
- [x] Math library implemented (GLM aliases in engine/math/Types.h, used throughout engine — committed)

---

## Input

### Decisions
- **Implementation:** Custom — no external library (SDL2 is the only real option but too heavy for just input)
- **Architecture:** 3 layers
  - **Platform layer:** One `IPlatformInput` impl per platform — polls native events and pushes to a shared `InputEventQueue` in a unified format
  - **Input manager:** Tracks double-buffered per-frame state (pressed/held/released) for keyboard, mouse, gamepad, touch, sensors
  - **Action mapping:** Named actions (`"Jump"`, `"Move"`) bound to one or more inputs, serialized to/from JSON
- **Gamepad:** All platforms via platform-native APIs, mapped to a unified `GamepadState`
  - Windows: XInput (Xbox controllers) + DirectInput fallback
  - Mac/iOS: GCController (GameController framework)
  - Android: `AInputEvent` with `AINPUT_SOURCE_JOYSTICK`
- **Analog sticks:** -1..1 normalized, deadzone (~0.1 default to avoid drift) + optional response curve (linear/quadratic) configurable per binding
- **Rebindable controls:** Yes — action map updated at runtime and serialized to JSON
- **Touch:** First-class, up to 10 fingers, phase tracking (began/moved/ended)
- **Motion sensors:** Accelerometer + gyroscope supported
- **Composite axes:** WASD keys map to a `Vec2` axis the same way a left stick does — game code sees no difference
- **API surface (game code only sees this):**
  ```cpp
  isKeyDown / isKeyPressed / isKeyReleased
  isActionPressed / isActionDown / isActionReleased
  getActionAxis(name) -> Vec2
  getTrigger / getAxis
  isTouchActive / getTouchPosition
  getAccelerometer / getGyroscope
  input.bind("Jump", Key::Space) / input.bind("Jump", GamepadButton::A)
  ```

### Gyro Input API
- `GyroState` struct added to `InputState` (`engine/input/InputState.h`)
  - `pitchRate`, `yawRate`, `rollRate` — angular velocity in radians/sec (X, Y, Z axes)
  - `gravityX`, `gravityY`, `gravityZ` — normalized gravity vector from accelerometer
  - `available` — true if the device has a gyroscope
- Accessed via `state.gyro()` (const ref) — platform backends populate `state.gyro_`
- Platform-agnostic API: Android uses `ASensorManager` + `ASENSOR_TYPE_GYROSCOPE`, iOS can use `CMMotionManager`
- 3 tests in `tests/input/TestGyroInput.cpp` (`[input][gyro]` tag)

### Status
- [x] Input implemented (GLFW backend, InputSystem, InputState, key/mouse/touch abstraction — committed)
- [x] Gyro input API (GyroState struct, 3 tests — committed)

---

## Networking

### Context
The goal is not multiplayer — it's downloading assets and updates post-ship. This is a much simpler problem than game networking, but the decision to support **runtime asset streaming** (like Fortnite) makes it meaningfully more complex than just a batch pre-launch patcher. Assets need to arrive while the game is running without ever stalling the game loop.

### Library: libcurl
- Using `curl_multi` interface for concurrent, non-blocking downloads on a background thread
- Chosen over platform-native APIs (NSURLSession on Apple, WinHTTP on Windows, HttpURLConnection on Android) because four separate HTTP implementations is significant maintenance overhead for something as commodity as file downloading. The ~1MB binary cost of libcurl is worth the single unified implementation
- Chosen over `cpp-httplib` (header-only alternative) because cpp-httplib has no built-in async — you'd need to manage threads yourself, which negates its simplicity advantage for this use case

### Streaming Architecture
Assets can be requested at runtime while the game runs. The game loop must never block waiting for a download.

- **Download Manager:** Dedicated background thread running a `curl_multi` poll loop. Accepts prioritized download requests from any thread and pushes completions to a thread-safe queue consumed by the asset system
- **Priority queue:** Three levels drive scheduling within the download manager:
  - *Critical* — asset needed right now (player is looking at it); jumps to front of queue
  - *High* — prefetch for assets the player is moving toward; loaded ahead of time
  - *Background* — speculative prefetch; yields to all higher priority work
- **Asset Handle:** `AssetHandle<T>` returned immediately when an asset is requested — a lightweight non-owning token the game polls with `isReady()` or attaches a callback to. The game loop never waits; it checks readiness each frame and falls back to a placeholder until the asset arrives
- **Asset Cache:** Downloaded files stored on local disk. LRU eviction keeps cache within a configured size limit. Checksums verified on load to detect corruption or partial downloads
- **Manifest system:** A JSON manifest on the server lists every streamable asset (path, version, checksum, size). Fetched on launch and on manual player trigger. Delta comparison against the local manifest determines exactly what needs downloading — unchanged assets are never re-fetched

### Update Triggers
- On launch: manifest fetched, delta computed, missing/changed assets queued at Background priority
- Manually triggered: player initiates check from settings menu, same flow as launch

### World Streaming Integration
The `WorldStreaming` system (part of the scene graph layer) detects player proximity to unloaded zones and issues `High` priority prefetch requests ahead of need. When the player enters a zone, assets should already be ready or nearly so.

### Placeholder / Fallback Strategy
Low-quality fallbacks shown while full-resolution assets download — this is how modern streaming games avoid hard pop-in:
- Textures: lowest available LOD or a flat-colour placeholder
- Meshes: simplified proxy mesh or bounding box
- Audio: silence or a generic ambient loop

### Deferred
- Paid DLC / in-app purchases — free patches and updates only for now
- Multiplayer / real-time networking — long-term, separate system entirely

### Status
- [ ] Networking not started

---

## Editor

### Context
The goal is a simple, usable editor for a solo developer — explicitly not Unity/Unreal complexity. The editor is not a separate application; it runs as a mode inside the engine itself, sharing the same renderer (bgfx), ECS, and asset system. This eliminates an entire class of sync bugs and means there is only one binary to maintain.

### Implementation: ImGui Inside the Engine
The editor is an engine mode toggled at startup (or via a key). In editor mode, ImGui panels are rendered on top of the game viewport using the same bgfx render pass. In play mode, ImGui is hidden and the game runs normally. This approach is fast to iterate on, fully cross-platform with the same code, and keeps the editor and runtime tightly coupled so what you see in the editor is exactly what runs in the game.

### First Milestone — Minimum Viable Editor
These panels ship first; everything else is deferred:
- **Scene view** — 3D viewport with mouse/keyboard navigation
- **Hierarchy** — entity tree showing parent/child relationships
- **Inspector** — selected entity's components with editable fields
- **Asset browser** — files on disk (textures, meshes, audio)
- **Play / Pause / Stop** — run the game inside the editor
- **Transform gizmos** — move, rotate, scale selected entities in the 3D viewport

### Scripting: C++ Only (Lua Tabled)
Scripting was considered to enable non-programmer game logic authoring and hot-reload without recompilation. For a solo developer shipping one game in C++, the added complexity of a scripting bridge (binding layer, separate language tooling, debugger integration) is not justified at this stage. Lua (~300KB, widely used in games) is the preferred candidate if scripting is added later — note this for future consideration.

### Hot Reload
Hot reload is important for fast iteration — change a system, see it live in the running game without restarting.

**How it works:** Game logic is compiled as a dynamic library (`.dylib` Mac, `.dll` Windows) separate from the engine core. A file watcher detects source changes, triggers a fast incremental recompile of just the game library, then the engine unloads the old library and loads the new one. Because all entity and component data lives in the engine's Registry (not the game library), ECS state is fully preserved across reloads. Only system logic and any static state inside systems is reset.

**Scope:** Game systems only — engine core (ECS, renderer, physics, audio) is never reloaded. This keeps the compile-time DAG scheduler intact for engine systems while adding a simpler runtime-registered layer for game systems that need hot reload.

**Platform support:**
- Mac: `dlopen`/`dlclose`/`dlsym` — straightforward
- Windows: `LoadLibrary`/`FreeLibrary` — requires copying `.dll` before load to avoid file locking
- iOS: Not supported — Apple prohibits dynamic code loading on device (development-only feature anyway)
- Android: `dlopen` works — useful during on-device development

### In-Game Visualiser
Kept separate from the editor — an independent ImGui overlay always available at runtime (in both editor and play mode). Shows FPS, CPU/GPU usage, texture memory, active entity count, and feature toggles. Does not share code with the editor panels beyond both using ImGui.

### Publishing
Per-platform Build action in the editor:
1. Compiles a release build with editor code stripped
2. Packages all assets (including manifest for streaming)
3. Outputs platform-specific bundle: `.app` (Mac), `.exe`/installer (Windows), `.ipa` (iOS), `.apk`/`.aab` (Android)
Signing and notarization handled per platform (Xcode for Apple, certificate signing for Windows).

### Deferred
- Lua scripting — revisit when/if non-programmer authoring or hot-reload-without-recompile becomes a priority
- Material editor, animation timeline, node graph — post-MVP editor features
- In-editor asset import pipeline (currently assets are pre-processed externally)

### Status
- [x] Editor Phases 1-4 complete (native window, hierarchy, properties, gizmos)
- [x] Editor Phases 5-9 complete (undo/redo, shortcuts, asset browser, inspectors, console)
- [x] Viewport click-to-select (ray-AABB picking, slab method, gizmo priority)
- [x] Phase 10: Resource usage inspector — live stats panel (FPS, frame time, draw calls, triangles, texture memory, entity count, arena usage) in a tabbed bottom panel alongside Console. Rolling ring buffer history (120 samples). Native NSTabView with CocoaResourceView labels.
- [x] Phase 11: Play/pause/stop — EditorPlayState enum, Space=play/pause, Escape=stop. Transform snapshot on play, full restore on stop to prevent scene corruption. HUD indicator shows current state.
- [x] Phase 12: Physics in Play mode — editor owns `JoltPhysicsEngine` + `PhysicsSystem`, steps simulation only when `playState == Playing`, destroys all bodies on Editing→Play and Play/Paused→Stop transitions. Gizmo + properties + hierarchy edits gated to Editing mode only. `RigidBodyInspector` and `ColliderInspector` added to PropertiesPanel. "Add Component" menu now offers Rigid Body and Box Collider. (Commits `9f3d952`, `dab14f0`, `ec2a684`, `f4ebcf4`)
- [x] Phase 13: HUD via UiRenderer + JetBrains Mono MSDF — replaces the old `bgfx::dbgTextPrintf` overlay with a real text pass on view 15 (`kViewImGui`). Editor owns its own `engine::ui::MsdfFont`, `UiDrawList`, and `UiRenderer`; init resolves the asset path via the `_NSGetExecutablePath` walker so the binary works from any cwd; shutdown tears down all bgfx resources. Falls back to dbgText if the JBM atlas fails to load. (Commit `755f33e`)
- [x] Phase 14: Per-frame allocation audit + fixes — top-5 items from the editor code review fixed in `0694eef`/`def1ab4`. `syncConsoleView` and `ConsolePanel::render` now stream directly with zero per-frame heap allocations; `AssetBrowserPanel::render` caches per-row icons at scan time; `refreshHierarchyView`/`refreshPropertiesView` pre-reserve their vectors; `TransformGizmo` got a defensive axis-index bounds check.
- [x] Phase 15: Procedural skybox + cached IBL cubemap — editor now renders the engine's existing IBL cubemap as a fullscreen-triangle skybox on view `kViewOpaque` (commit `dfd4162`), and ships a precomputed `assets/env/default.env` blob (2.5 MB) loaded by `engine::assets::loadEnvironmentAsset` instead of regenerating from scratch on every launch (commit `917623b`). Editor startup dropped from **4.96 s → 95 ms** (52× faster). Per-phase profile is dumped to stderr on every launch (commit `b5a8434`) so any future regression is immediately visible.

### Default sky cubemap — re-baking

The procedural sky cubemap that ships at `assets/env/default.env` is a precomputed binary produced by the `bake_default_env` tool from `IblResources::generateDefaultAsset()`. The editor loads it at startup; falls back to runtime regeneration (~5 s) only when the file is missing or fails the version check.

**When to re-bake:** any change to the procedural sky model in `engine/rendering/IblResources.cpp` (the `proceduralSky` function, the cubemap dimensions, the BRDF LUT integration, anything that alters the math) invalidates the cached file. Re-bake whenever such a change lands.

**How to re-bake:**

```sh
# 1. Bump the version constant so old caches in user checkouts are
#    rejected on load and the editor falls back to generateDefault().
$EDITOR engine/assets/EnvironmentAssetSerializer.h
# change `kEnvironmentAssetVersion` from N to N+1

# 2. Build the bake tool and run it. ~5 seconds of CPU work.
cmake --build build --target bake_default_env -j$(sysctl -n hw.ncpu)
./build/bake_default_env

# 3. Commit the new blob plus the version bump.
git add assets/env/default.env engine/assets/EnvironmentAssetSerializer.h
git commit -m "chore(assets): rebake default sky after <reason>"
```

The version bump is critical: without it, users with the old cached file will silently load stale data (wrong colors, mismatched dimensions, possibly worse). The `loadEnvironmentAsset` reader checks the version field in the file header; mismatched version = `nullopt` = editor falls back to `generateDefault()` and prints a warning to stderr.

`bake_default_env` accepts a custom output path as its first argument if you want to bake to a different location for testing. The default `assets/env/default.env` is what gets shipped.

**Why ship the binary in git instead of generating at first run:** the file is small (~2.5 MB), deterministic, and changes only when an engine developer touches the sky model. Generating on first launch would punish every new user with a 5-second blocking startup the first time they ran the editor — and macOS app bundle distributions don't have a "first launch" hook anyway. Shipping the file is simpler and faster for everyone.

### Physics in Play Mode — Design

**Decision:** the editor runs the physics simulation only inside Play mode, and resets the world (destroys all Jolt bodies, clears `RigidBodyComponent::bodyID`) on every Editing→Play and Play/Paused→Stop transition. Bodies are recreated from authored components on the next `PhysicsSystem::update`.

**Why a hard reset instead of snapshotting body state:**
- Jolt body state includes velocities, sleep state, accumulated forces, contact manifolds, and broad-phase tree position. Most of this is not exposed through the `IPhysicsEngine` interface and even if it were, restoring it cleanly across re-creation is fragile (body IDs would need to be remapped).
- The transform snapshot (taken on Editing→Play, restored on Stop) is the *authoring* truth. After restore, the next Play should start the simulation from that exact authored state — which is precisely what re-creating bodies from `RigidBodyComponent` produces. Trying to "resume" a paused-then-stopped simulation would create surprising results: the user would see their cube end up in a different place than where they parked it in the editor.
- Costs are negligible: the typical scene has a few hundred bodies at most; `destroyAllBodies` is O(n) Jolt removes plus a hash-map clear. The Play→first-step latency is dominated by `PhysicsSystem::registerNewBodies`, which already runs every frame.

**Why edits are gated to Editing mode only:**
- During Play, `PhysicsSystem::syncDynamicBodies` writes back into `TransformComponent` every frame from Jolt's authoritative pose. If the gizmo or the properties panel also writes into `TransformComponent`, the two writes race — and Jolt wins on the next step, so the user's edit appears to be silently rejected.
- The simpler fix is to disable user-initiated transform writes outside Editing mode: gizmo `update()` early-returns, and the PropertiesPanel/HierarchyPanel callbacks check play state before committing. The user can still *inspect* values during Play — they just can't change them.
- An alternative would be to detect "user has a `RigidBodyComponent`" and route the edit through `physics.setBodyPosition()`, but that turns the editor into an authoring-during-simulation tool, which is a separate UX question. Deferred until there's a concrete need.

**Caveat — `TransformInspector` keyboard editing not yet gated:** `TransformInspector` has its own in-place keyboard editing path (Tab/Arrow/+/-) that bypasses the PropertiesPanel native callbacks, so it isn't covered by the play-state gate. The practical impact is small (the gizmo and the AppKit-style callbacks are blocked), but if it becomes a real footgun the cleanest fix is to thread `EditorState&` into `IComponentInspector::inspect()`. Tracked in the editor TODO list below.

**Why `destroyAllBodies()` is on the interface, not editor-private:**
- The editor lives outside `engine::physics` and only sees `IPhysicsEngine`. Adding the method as a Jolt-only helper would require a `dynamic_cast` from the editor — fragile and would violate the interface boundary.
- The method is also useful outside the editor: scene unload, level transitions, and any "reset to initial state" feature would want it. It's a small enough surface to belong on the base interface.

### TODO

Consolidated from `EDITOR_ARCHITECTURE.md` § 18.7 (Implementation Guidelines, never enforced post-MVP), the Deferred subsection above, and outstanding bug reports surfaced while dogfooding the editor. Items grouped by trigger / dependency.

**Known bugs / UX gaps (dogfooding backlog)**
- [x] Material editor: changing a field in the material inspector doesn't apply to the selected entity's `MaterialComponent`. The UI binds, but the write-back path is missing — likely needs a `MaterialInspector::commit()` hook in the same place rotation/scale edits got fixed (`b76908d`). *Why:* the inspector exists but is non-functional, so the panel is misleading. *Fix:* MaterialInspector had no keyboard input handling at all — only a static read-only display. Added Tab/Up/Down navigation across 6 fields (albedo R/G/B, roughness, metallic, emissive scale) with Left/Right or `+`/`-` adjusting the active field. Edits write through `RenderResources::getMaterialMut()`, which is the same `Material*` slot `DrawCallBuildSystem` reads each frame (`engine/rendering/systems/DrawCallBuildSystem.cpp:99`) — no caching layer, no invalidation step. Tradeoff: this is a debug-text inspector, not a "proper" Cocoa text-field editor; full color-well/slider UI is tracked under "Material editor (proper)" below.
- [x] Selection outline: viewport-clicked entities now get a vivid yellow stencil-band outline drawn on top of the mesh in the editor viewport. Two-pass stencil approach — see "Editor selection outline — two-pass stencil" architectural-decision entry below.
- [x] Viewport dirty-flagging: implemented in `EditorApp::run()` via a `viewportDirty` flag on `Impl`. The opaque/skinned PBR pass, selection outline, skybox, gizmos, and HUD overlay are all skipped when the flag is false; bgfx views are still touched so the swapchain re-presents the previous frame. The flag is set true on camera orbit/zoom (only when there's actual mouse delta), gizmo hover/mode/drag, picking, every property/material/light/visibility/transform write-back, every hierarchy mutation (rename excluded), undo/redo, scene new/open, asset import, environment loads, window resize, active animator playback, and forced true every frame while in Play mode. A "redraws: N" counter is shown in the HUD; idle frames stop incrementing it within 1-2 frames. *Tradeoff:* the FPS readout in the HUD only refreshes when the viewport redraws — acceptable since the whole point is to stop redrawing when idle. *Why this approach:* a global flag in `EditorApp::Impl` keeps the change confined to the editor (no engine API churn) and slots into the existing `hierarchyDirty` / `propertiesDirty` pattern.
- [x] `TransformInspector` keyboard editing path (Tab/Arrow/+/-) bypasses the play-state gate added in Phase 12 — the gizmo and PropertiesPanel callbacks are blocked during Play, but typing into the inspector still mutates `TransformComponent` and races `PhysicsSystem::syncDynamicBodies`. Cleanest fix: thread `EditorState&` into `IComponentInspector::inspect()`. *Why:* a user inspecting numbers during Play could accidentally fight the simulation and not understand why nothing happens. *Fix:* did exactly that — added `const EditorState&` to `IComponentInspector::inspect()` and updated all six implementations. `TransformInspector`, `ColliderInspector`, and `RigidBodyInspector` now early-out the value-mutating Left/Right/`+`/`-` paths when `playState() != Editing` (field navigation Tab/Up/Down is still allowed so users can browse). `MaterialInspector` adopts the same gate for consistency. A grey hint line `(read-only while playing)` renders below each gated inspector. `NameInspector` and `LightInspector` ignore the new param — they were already read-only.
- [x] Editor HUD swap to UiRenderer + JetBrains Mono MSDF — done in commit `755f33e`. The FPS counter, mode label, gizmo shortcut hint, "PLAYING" indicator, status message, and Add Component overlay all render through `UiRenderer` on view 15 (`kViewImGui`). Falls back to `bgfx::dbgTextPrintf` if the JBM atlas fails to load.

**Editor features still pending (post-MVP, no concrete game blocked yet)**
- [ ] Material editor (proper): node graph or at least a typed multi-channel inspector — texture pickers, sliders, color wells per PBR channel. *Trigger:* first time we want to author a material that isn't a glTF import.
- [x] Animation timeline: scrubber + transport controls for `AnimatorComponent`. Done — clip dropdown, ▶/⏸/⏹ transport, scrubber with time label, speed slider, loop checkbox. `kFlagSampleOnce` for frame-accurate editor scrubbing.
- [ ] Node graph: shader / behavior graph editor. *Trigger:* a designer-authored shader or visual scripting requirement (currently neither exists).
- [ ] Lua / scripting host: deferred until non-programmer authoring or hot-reload-without-recompile is a real ask (NOTES.md → Editor → Deferred).

**Animation editor features (phased roadmap)**

*Phase 1 — Event markers on timeline (DONE)*
- [x] `AnimationViewState` carries `events` (vector of `EventMarker`) and `firedEvents` (names of events that fired this frame, for flash highlighting).
- [x] `AnimationPanel::update()` populates event markers from `AnimationClip::events` and fired events from `AnimationEventQueue`.
- [x] Add/Remove/Edit event callbacks wired in `EditorApp.cpp` (`EventAddedCallback`, `EventRemovedCallback`, `EventEditedCallback`). Add inserts at the current scrubber time; Remove deletes by index; Edit updates name and time.
- [x] All within `CocoaAnimationView` — no new panels needed.

*Phase 2 — State machine parameter panel (DONE)*
- [x] New section in the Animation panel populated when the selected entity has an `AnimStateMachineComponent`.
- [x] `AnimationViewState` carries `hasStateMachine`, `currentStateName`, `stateNames`, `currentStateIndex`, and a `params` vector of `ParamInfo` (name, value, isBool).
- [x] `ParamInfo::isBool` detected by heuristic: if any transition condition on the param uses `BoolTrue`/`BoolFalse`, it is treated as a boolean (checkbox) rather than a float (slider).
- [x] `paramNames` map on `AnimStateMachine` provides human-readable parameter names for editor display (populated automatically by `addTransition()`).
- [x] State dropdown to force-set current state (`StateForceSetCallback`).
- [x] Parameter editing (`ParamChangedCallback`) writes back to `AnimStateMachineComponent::params`.

*Phase 3 — Visual state machine node graph (DONE)*
- [x] `StateMachineGraphView` extracted to separate `editor/platform/cocoa/StateMachineGraphView.h/.mm` with `StateMachineGraphViewDelegate` protocol.
- [x] State nodes as rounded rectangles (120x50px) with name + clip. Green border = active state, blue = selected. Draggable repositioning.
- [x] Bezier curve transition arrows with arrowheads. Self-transitions as loops. Condition summary labels with background pills.
- [x] Click node to select, double-click to force-set current state, click arrow to select transition.
- [x] Right-click context menus: "Delete" on state, "Add State" on empty area.
- [x] Scroll wheel zoom (0.5x-2.0x, toward cursor), Option-drag to pan.
- [x] Auto-layout: grid arrangement on first display (200x100 node spacing), positions preserved across updates.
- [x] Fully synced with list-based editor — same callbacks, same selection state, both update via setState.
- [x] `SMGraphStorage` NSObject workaround for ObjC++ ARC ivar corruption.
  - **Known issue:** ObjC++ ARC does not properly manage Objective-C ivars declared inside `@implementation` blocks of NSView subclasses when the file is compiled as ObjC++. NSMutableArray/NSString ivars can be silently over-released or not retained.
  - **Workaround:** All mutable state (`nodePositions`, `nodeNames`, `nodeClipNames`, `transFromState`, `transToState`, etc.) is stored on a separate `SMGraphStorage` NSObject with `@property` declarations. The NSView holds a single `SMGraphStorage* _s` ivar. ARC correctly manages properties on a standalone NSObject, avoiding the corruption.
  - **GraphViewDelegateAdapter:** Delegate is stored as `void* _graphDelegateRaw` (raw pointer) to avoid ARC retaining the delegate.
- [x] Animation panel wrapped in `NSScrollView` for overflow content — vertical scrollbar auto-hides, container view hosts all sub-panels (transport controls, event markers, state machine, graph view).

*Phase 4 — Serialization (sidecar files) (DONE)*
- [x] `AnimationSerializer` API: `saveEvents()` / `loadEvents()` for `<model>.events.json`, `saveStateMachine()` / `loadStateMachine()` for `<model>.statemachine.json`.
- [x] Sidecar files stored alongside the `.glb` with matching base name (e.g., `BrainStem.events.json`, `BrainStem.statemachine.json`).
- [x] Auto-load on import: `EditorApp.cpp` checks for sidecar files when importing a glTF and calls `loadEvents()` / `loadStateMachine()` if they exist.
- [x] Clip names and parameter names are used as keys for round-tripping (not integer IDs), so sidecar files remain valid across re-exports.

**Implementation hygiene (§ 18.7 checklist — apply incrementally as code is touched)**
- [ ] Header hygiene sweep: confirm no `.h` under `editor/` includes `AppKit`, `windows.h`, or `commctrl.h`; Pimpl all platform types; `IEditorPanel`/`IEditorWindow`/`IComponentInspector` headers contain only `<cstdint>` + forward decls.
- [~] Per-frame heap allocations on the editor hot path — partial. The audit of 2026-04-10 caught all top-priority items: `syncConsoleView` now streams directly to the cView, `ConsolePanel::render` uses a stack-allocated ring buffer, `AssetBrowserPanel` caches per-row icons at scan time, `refreshHierarchyView` and `refreshPropertiesView` pre-reserve their vectors, and `TransformGizmo` got a defensive bounds check on the rotation drag axis index. Commits `0694eef` and `def1ab4`. Remaining: a full sweep for cold-path allocations and converting the remaining `std::vector` scratch buffers to `FrameArena`/`InlinedVector`.
- [ ] Replace small bounded collections with `InlinedVector<T, N>` (panel list, selection set, gizmo axis hit results).
- [ ] Switch undo command storage to `PoolAllocator<T, MaxCount>` sized to `maxDepth_` (default 100) — recycle the oldest slot when full.
- [ ] Audit `std::shared_ptr` usage in editor code; convert to `unique_ptr` unless ownership is documented and proven acyclic.
- [ ] Async file I/O: scene save/load, asset import, auto-save must not call `fread`/`fwrite`/`ifstream::read` on the main thread for files >4KB. Use `mmap` for baked binary assets.
- [ ] Gizmo + grid render time budget: enforce <2ms total via the resource inspector.
- [ ] Profiling: add `SAMA_PROFILE_SCOPE("name")` markers (macro TBD) at the top of every per-frame editor function so future Tracy/Instruments runs are drop-in.

**CI / regression coverage**
- [ ] ASAN/LSAN-enabled editor build in CI.
- [ ] Memory benchmark: load reference scene, assert RSS against § 18.5 targets.
- [ ] Startup-time benchmark: process launch → first frame, asserted against § 18.2 target.
- [ ] Periodic Instruments "Leaks" + "Allocations" runs at each phase milestone (manual, document in NOTES).

*Why this list lives here and not in `EDITOR_ARCHITECTURE.md`:* the architecture doc captures the binding contract (the *should*); this list is the rolling work queue (the *will-do, in this order*). When an item is completed, move it to the Status checklist above and link the commit, then delete it here.

---

## 2D Support

### Context
The question is whether the engine should be 2D-first, 3D-first, or dual from the start. There are no current 2D game ideas in the pipeline — the primary target is 3D (open world / FPS style). However, orthographic rendering and 2D physics are standard asks that real projects will need eventually.

### Decision: Option B — Orthographic Camera + Sprite Rendering Early, 2D Physics Deferred

**What this means:**
- Add orthographic camera projection and sprite rendering (a quad mesh + texture atlas) early, as a lightweight addition to the 3D renderer. This enables UI layers, HUDs, and 2D minigames without any separate 2D pipeline.
- 2D physics (Box2D) is **deferred** — not integrated until a concrete 2D game or physics-driven 2D feature is needed.

**Why not 2D-first:** No current 2D game plans. Building a full 2D pipeline upfront would add complexity (separate physics world, 2D coordinate system, Z-ordering) with no near-term payoff.

**Why not a full dual pipeline:** A fully separate 2D renderer + physics system is maintenance burden. Most "2D games" in a 3D engine are just orthographic 3D — the same renderer, same ECS, same systems, just a camera switch.

**Why Option B over doing nothing:** Orthographic + sprites are low cost to add onto the existing 3D renderer — it's essentially a camera flag and a sprite mesh type. This gives 2D capability for free without a standalone 2D pipeline. Deferring Box2D keeps scope tight until there's a real need.

**2D Physics plan (when needed):**
- Box2D v3 (~300KB) behind an `IPhysics2DEngine` interface, parallel to Jolt's `IPhysicsEngine`
- 2D and 3D physics worlds run independently — no attempt to bridge them
- Trigger when: first game concept that needs rigid body 2D physics

### Status
- [x] 2D orthographic camera + sprite rendering implemented (Phase 10 — sprite batching + UI pass, committed)
- [ ] Box2D integration deferred

---

## Deferred Work

A consolidated index of everything intentionally left for later. Grouped by what would trigger revisiting it. Each item links back to the section of NOTES.md or an architecture doc where the original decision and tradeoffs are recorded.

---

### Trigger: profiling shows a real bottleneck

These are not premature optimisations — they have a clear cost (complexity, code size, fork maintenance) and should only be done when a profiler confirms they are the bottleneck.

| Item | Trigger | Where decided |
|---|---|---|
| Custom ARM NEON SIMD layer (`SimdMat4` / `SimdVec4`) | Mobile profiling shows `TransformSystem` world-matrix updates are the bottleneck | NOTES.md → Math Library; `engine/math/Simd.h` stub ready |
| Intra-system worker parallelism (data-parallel within one system) | A single system's wall time exceeds the frame budget on high-end targets | NOTES.md → Threading Model |
| Splitting conflicting ECS system writes (ownership refactor) | Two systems serialised by the DAG become a combined bottleneck | NOTES.md → Threading Model |
| GPU-driven rendering — compute culling + `drawIndirectCount` | Instanced mesh count exceeds ~100k and CPU culling is confirmed bottleneck | RENDERING_ARCHITECTURE.md → bgfx Limitations; meshlets already generated at import |
| bgfx fork: bindless textures (`VK_EXT_descriptor_indexing` + Metal argument buffers) | Profiling shows material rebinding is a CPU bottleneck at high draw counts | RENDERING_ARCHITECTURE.md → bgfx Limitations |
| bgfx fork: push constants / root constants for per-draw uniforms | Profiling shows uniform binding is a bottleneck at high draw counts on mobile | RENDERING_ARCHITECTURE.md → bgfx Limitations |
| Archetype-based ECS (replace sparse set) | Multi-component query performance becomes measurable bottleneck at scale | NOTES.md → ECS |
| PhysX integration (behind `IPhysicsEngine`) | Racing game vehicle fidelity demands it over Jolt | NOTES.md → Physics |

---

### Trigger: a specific game feature is needed

These have no value until a concrete game requires them. Build when the requirement lands.

| Item | Trigger | Where decided |
|---|---|---|
| Box2D 2D physics (`IPhysics2DEngine`) | First game concept requiring rigid-body 2D physics | NOTES.md → 2D Support |
| Terrain renderer (heightfield + clipmap LOD) | First game with open-world terrain | RENDERING_ARCHITECTURE.md → Open Decisions |
| Decals | First game requiring projected decals (bullet holes, dirt splashes) | RENDERING_ARCHITECTURE.md → Open Decisions |
| Lightmaps + light probe volumes | First game with significant static geometry needing baked GI | RENDERING_ARCHITECTURE.md → Open Decisions |
| TAA (Temporal Anti-Aliasing) | Game requires higher image quality than FXAA provides (requires jitter + reprojection) | RENDERING_ARCHITECTURE.md → Open Decisions |
| Ray tracing | Game explicitly targets high-end desktop/console RT-capable hardware; evaluate bgfx RT support at that time | NOTES.md → Rendering; RENDERING_ARCHITECTURE.md → bgfx Limitations |
| Multiplayer / real-time networking | Game requires multiplayer — entirely separate system from asset streaming | NOTES.md → Networking → Deferred |
| Paid DLC / in-app purchases | Business model requires it | NOTES.md → Networking → Deferred |
| Lua scripting bridge | Non-programmer authoring needed, or hot-reload-without-recompile becomes a priority | NOTES.md → Editor → Scripting |
| Depth prepass for alpha-tested geometry on TBDR | Game has dense foliage and mobile profiling shows masked overdraw is a bottleneck | RENDERING_ARCHITECTURE.md → View 1; `depthPrepassAlphaTestedOnly` flag already in `RenderSettings` |
| ~~Skeletal animation system~~ | ~~First game with animated characters or creatures~~ | **DONE** — Skeleton, AnimationClip, Pose, AnimationSampler, AnimationSystem, GPU skinning, glTF integration (committed) |
| FMOD audio (replaces SoLoud) | Game requires AAA audio polish: HRTF, geometry-aware reverb, sound designer workflow, or revenue exceeds $200k | NOTES.md → Audio |

---

### Trigger: scale or team size grows

These only make sense at a larger codebase or team than currently planned.

| Item | Trigger | Where decided |
|---|---|---|
| C++20 Modules (replace `#pragma once`) | Compiler/toolchain support matures and is consistent across MSVC, Clang, Android NDK | NOTES.md → C++ Standard |
| Unity builds or precompiled headers | Compile times become painful at scale (benchmark first) | NOTES.md → Compile Time |
| Explicit ECS system ordering without data dependencies (`dependsOn()`) | Systems need sequencing for non-data reasons (e.g. frame budget ordering) | NOTES.md → Threading Model → Future Consideration |
| `GltfAsset` / `RenderResources` ownership unification | A game leaks meaningful texture memory by loading and releasing many glTF assets, OR the split-ownership confusion bites a future contributor | NOTES.md → bgfx threading mode; `engine/assets/AssetManager.cpp` `destroyPayload()` excludes `GltfAsset` because `addMesh()` moves mesh handles into `RenderResources` while `addTexture()` references texture handles non-owningly. Fix path: either have `addMesh` take ownership semantically by reading from a non-const `GltfAsset&` and invalidating the source handles, OR have `RenderResources` track per-resource ownership so it can `destroyAll()` only the resources it owns. The standalone-`Texture` leak is fixed; the GltfAsset texture leak (each unused `GltfAsset` texture leaks one bgfx handle) is the open item. |
| Render graph (replace hardcoded linear pipeline) | Pass count grows beyond ~6 or resource dependency management becomes error-prone | RENDERING_ARCHITECTURE.md → Design Principles |
| Runtime-registered ECS systems (scripting layer) | Scripting or hot-reload game systems need a dynamic system registration path | NOTES.md → Threading Model |
| bgfx fork: Vulkan subpasses for TBDR post-effects | Mobile post-effects (SSAO on TBDR) become a shipping priority | RENDERING_ARCHITECTURE.md → bgfx Limitations; SSAO on TBDR currently requires full depth resolve |
| Prebuilt release tarballs (macOS-arm64, windows-x64, etc.) | Sama stabilizes and external users want binary distribution | Publish via GitHub Releases with lib/, include/, and SamaConfig.cmake. Games would use `find_package(Sama)` instead of FetchContent. Avoids ~5-10 min first-configure download for each consumer |

---

### Trigger: upstream / external contributions

Side projects to improve third-party libraries used by the engine.

| Item | Trigger | Notes |
|---|---|---|
| Jolt soft body upstream contributions | After Jolt is integrated and real usage surfaces the most painful gaps | Open GitHub discussion with jrouwe before writing code. Priority gaps: mesh/heightfield hair collision, GPU-accelerated soft body. See NOTES.md → Physics → Side Projects |
| bgfx `NUM_SWAPCHAIN_IMAGE` configurable upstream | Next time we update bgfx | Currently hardcoded to 4 in bgfx's Vulkan backend. Pixel 9 needs 5. We patch to 8 via CMake define. Upstream PR would add a `BGFX_CONFIG_` knob so the patch is no longer needed. Low complexity, high value for all Android/Vulkan users of bgfx. |

---

### Pending decisions (not yet resolved)

| Item | Blocked by | Notes |
|---|---|---|
| Post-MVP editor features: material editor, animation timeline, node graph | MVP editor ships first | See NOTES.md → Editor → Deferred |
| In-editor asset import pipeline | MVP editor ships first; assets currently pre-processed externally | See NOTES.md → Editor → Deferred |

---

## External Library Policy

### Approach: Case-by-Case, Evaluated at Integration Time
No blanket rejection criteria — libraries are evaluated when a concrete need arises, not speculatively. The goal is to avoid over-engineering the policy before the trade-offs are real.

### Primary Evaluation Factors (in order of importance)
1. **Binary size** — the most important axis. Every library adds to the shipped binary, which matters for mobile (app store size limits, download rates, install rates). Prefer header-only or small compiled libraries; document the size cost of every library added.
2. **Performance** — the second primary factor. A library that adds size but delivers measurable performance gains (e.g. Jolt's multi-threaded physics) is worth it. A library that adds size with no meaningful perf advantage over a hand-rolled alternative is not.
3. **License** — MIT/BSD/Apache always acceptable. LGPL is acceptable with care (dynamic linking to avoid GPL infection). GPL is not acceptable in shipped game code. Royalty-bearing licenses (FMOD, PhysX) are case-by-case based on feature value vs. cost.
4. **Platform support** — must cover all four targets (Windows, Mac, iOS, Android) or be explicitly scoped to platforms where it's used.
5. **Maintenance / activity** — actively maintained projects preferred; abandoned libraries avoided unless the code is stable and self-contained.
6. **Build system** — must integrate with CMake (via FetchContent or add_subdirectory). Libraries requiring custom build systems are a red flag.

### Vendoring vs. FetchContent
- FetchContent is the current default (see Catch2 in CMakeLists.txt) — keeps the repo lightweight
- Vendor (copy source into repo) when: upstream is unstable, we need local patches, or the library is small enough that vendoring is trivial
- Decision made per library at integration time

### stb_image vs bimg Overlap (Investigated, No Action)

Both stb_image and bimg can load PNG/JPG images. Investigated replacing stb_image with bimg to reduce duplication.

**Finding:** bimg uses stb_image internally as its fallback decoder. Replacing our direct stb_image calls with bimg's `imageParse()` would call stb_image indirectly through a more complex API (requires `bx::AllocatorI*`, `ImageContainer` lifecycle, `bx::WriterI*` for PNG writing). No actual dependency reduction — stb_image remains compiled into the bimg library regardless.

**Additional factors:**
- stb FetchContent cannot be removed — SoLoud depends on `stb_vorbis.c`
- Current stb_image usage is isolated to 3 files (~15 lines of calls)
- bimg has no file-path loading (would add boilerplate to screenshot golden comparison)

**Decision:** Keep both. The overlap is superficial — no binary size savings, more boilerplate, and the dependency stays either way.

### Status
- Policy defined; applied as each library is integrated

### Umbrella CMake Targets (Game Developer Ergonomics)

Problem: Before umbrella targets, a game's `CMakeLists.txt` had to list 10+ individual `engine_*` libraries with transitive deps in the right order. Easy to miss one (link error) or include too many (binary bloat).

Solution: Four `INTERFACE` targets that aggregate curated dependency sets:

| Target | Aggregates | Use case |
|--------|-----------|----------|
| `sama_minimal` | core + game + rendering + scene + ecs + memory | Simple 3D scenes, UI, prototypes |
| `sama_3d` | minimal + physics + audio + animation + assets | Most 3D games (default) |
| `sama_2d` | minimal + io | 2D games, UI tools |
| `sama` | 3d + input + io + threading | Full engine |

**Why INTERFACE libraries:** No extra compilation — just a named dependency list. Zero build-time cost, zero binary size overhead, standard CMake pattern.

**Why four variants instead of one:** Binary size matters (mobile app stores, download rates). A 2D UI tool shouldn't link Jolt (+20MB) or SoLoud. Games that don't need physics can skip `engine_physics`. The umbrella targets codify common configurations.

**Alternative considered:** Single `libsama.a` bundle. Rejected because:
1. Kills modularity — users can't exclude subsystems
2. Platform-specific bundling tools (libtool/ar/lib.exe)
3. Symbol collision risk when transitively bundling third-party libs
4. Dead code elimination already handles unused symbols at link time — the umbrella `INTERFACE` approach gives us the same benefit without the bundling complexity

---

## Scene Graph

### Architecture Decision: ECS-native hierarchy
- **Approach:** Encode parent-child relationships directly in ECS components, no separate tree object.
- **Why:** Keeps all data in the same Registry that render systems already query. No synchronization between a parallel tree structure and ECS state. Views/queries work naturally.
- **Alternative considered:** Standalone SceneGraph class with its own node objects → rejected because it duplicates entity lifecycle management and requires manual sync with ECS transforms.

### Components
- `HierarchyComponent` (8 bytes): stores `EntityID parent`. Absent = root entity.
- `ChildrenComponent` (~24 bytes + heap): stores `std::vector<EntityID> children`. Removed when last child is detached (no empty vectors).
- `TransformComponent` (44 bytes, existing): local position/rotation/scale + dirty flag.
- `WorldTransformComponent` (64 bytes, existing): cached world matrix, one cache line.

### TransformSystem
- Runs once per frame, single-threaded, before culling/rendering.
- Finds roots (TransformComponent without HierarchyComponent), walks children recursively via ChildrenComponent.
- Composes `world = parentWorld * T * R * S` top-down. Writes WorldTransformComponent.
- **Dirty flag optimization implemented** — TransformSystem uses dirty flags to skip unchanged subtrees.

### Mutation API (free functions in `engine::scene`)
- `setParent(reg, child, newParent)` — cycle detection via ancestor walk, automatic cleanup of old parent's ChildrenComponent when empty.
- `detach(reg, child)` — remove from parent, become root.
- `destroyHierarchy(reg, root)` — depth-first recursive destroy of subtree.
- `isAncestor` — depth-limited walk (max 1024) as defense against corruption.

### GltfSceneSpawner Integration
- Creates an entity for every glTF node (not just mesh nodes) to preserve hierarchy.
- Decomposes `Mat4 localTransform` → TRS via `glm::decompose` for TransformComponent.
- Calls `setParent` to establish parent-child links.
- TransformSystem computes WorldTransformComponent before rendering.
- **Tradeoff:** Slightly more entities than before (hierarchy pivots without meshes), but enables runtime transform manipulation.

### Hierarchy Demo (`apps/hierarchy_demo`)
- 9 cubes in a 3-level tree matching `treehierarchy.png`: Root → Child1/Child2 → 3 leaves each.
- **Mouse picking:** Ray-AABB intersection. Unprojects mouse to world ray, tests against each cube's world-space AABB (8-corner transform of local bounds), selects nearest hit.
- **Drag-to-move:** Projects mouse movement onto a camera-facing plane through the selected cube's world position. Converts world-space delta to parent-local-space delta via inverse parent world matrix, then updates `TransformComponent.position`. TransformSystem propagates the change to children on the next frame.
- **Orbit camera:** Right-drag rotates yaw/pitch around the scene origin; scroll adjusts distance. No mouse capture (cursor stays visible) — different from helmet_demo's free-fly approach.
- **ImGui panel:** TreeNodeEx hierarchy view with click-to-select and live local/world position readout.
- **Why a separate demo:** Validates the scene graph end-to-end with runtime interaction (picking, dragging, hierarchy propagation), which the unit tests cannot cover.

---

## Rendering Bug Fixes

### Shadow Mapping Fixes
1. **Shadow pass face culling (BGFX_STATE_CULL_CCW → CULL_CW):** The shadow pass was culling front faces instead of back faces. Cube mesh uses CCW winding for front faces; `CULL_CCW` removed them, so the shadow map only contained back-face depth. Fixed to `CULL_CW`.
2. **Shadow Y-flip on Metal:** The bias matrix converting NDC→UV didn't account for Metal's top-left texture origin (`originBottomLeft = false`). Added `bgfx::getCaps()->originBottomLeft` check to flip V when needed.
3. **Slope-scaled shadow bias:** Fixed 0.005 bias caused shadow acne on surfaces at oblique angles to the light. Replaced with slope-dependent bias `mix(0.002, 0.02, 1.0 - dot(Ngeom, L))`.

### Neutral Normal Map Fallback
- **Problem:** When no normal map texture is assigned, the white `(255,255,255)` fallback was decoded as tangent-space `(1,1,1)`. After TBN transformation: `N = T*1 + B*1 + Ngeom*1 = T+B+Ngeom`. For a ground plane with `T=(1,0,0), B=(0,0,1), N=(0,1,0)`, this produced `(1,1,1)` normalized — and `dot((1,1,1), lightDir)` was ~0 for many light angles, making surfaces appear unlit.
- **Fix:** Added a `(128,128,255)` neutral normal texture that decodes to tangent-space `(0,0,1)` → `N = Ngeom` (no distortion). `DrawCallBuildSystem` uses this fallback for the normal map slot when `normalMapId == 0`.
- **Lesson:** Any texture slot where the shader decodes values (like `normalSample * 2 - 1`) needs a semantically correct fallback, not just white.

### PBR Specular Visibility
- **DamagedHelmet roughness ~1.0:** The ORM texture has roughness near 1.0 everywhere (battle-worn metal). At roughness 1.0, GGX specular lobe is completely flat — no visible peak. Override material roughness to 0.55 in the helmet demo so smoother areas produce visible specular.
- **Ambient too bright:** Hemisphere ambient (sky 0.90/0.95/1.20) dominated the output through Reinhard tonemapping compression. Reduced to (0.15/0.18/0.25) so directional light specular punches through.
- **Rotating light:** Helmet demo light orbits the model (6s period, 40° elevation) to sweep specular highlights across the surface.

### Skinned Shadow Fix (Metal Non-Contiguous Vertex Streams)
- **Problem:** Skinned entities (BrainStem.glb) rendered correctly with the PBR skinned shader but cast no shadow on the ground. Non-skinned entities (test cube) cast shadows correctly.
- **Root cause:** The skinned shadow draw call bound vertex stream 0 (positions) and stream 2 (bone indices/weights) but skipped stream 1 (surface normals/tangents/UVs). The Metal backend requires contiguous vertex buffer stream bindings — skipping stream 1 caused stream 2 attributes (`a_indices`, `a_weight`) to not be found by the shader, so all bone weights were zero and vertices collapsed to the origin.
- **Fix:** Bind the surface vertex buffer to stream 1 in `submitSkinnedShadowDrawCalls()` even though the shadow shader doesn't use those attributes. This makes the stream indices contiguous (0, 1, 2) which Metal requires.
- **Why PBR skinned worked:** `updateSkinned()` already binds all three streams (position, surface, skinning) because the PBR shader needs normals/tangents/UVs.
- **Lesson:** On Metal (via bgfx), always bind vertex buffer streams contiguously. Gaps in stream indices cause attribute binding failures on the Metal backend, even if the shader doesn't use attributes from the intermediate stream.

### glTF Multi-Primitive Mesh Merging
- **Problem:** BrainStem.glb has 59 primitives in a single mesh. The GltfLoader only processed `primitives[0]`, dropping 98% of the geometry.
- **Fix:** Merge all primitives by concatenating positions, normals, tangents, UVs, bone indices/weights, and indices (with vertex offset applied to index values).
- **Related fix:** BrainStem has no TEXCOORD_0 attribute. Without UVs, the surface vertex buffer wasn't created (requires normals + tangents + UVs). Fixed by generating zero UVs when TEXCOORD_0 is absent, enabling surface buffer creation for meshes with normals but no UVs.

---

## Engine/Application Abstraction (`engine::core::Engine`)

### Problem
~53% of demo code was copy-pasted boilerplate (215-230 identical lines across all 5 demos): window creation, renderer init, default texture creation, shader loading, ImGui setup, input wiring, shadow renderer init, frame arena, DPI scaling, frame loop skeleton, cleanup.

### Solution
`engine::core::Engine` class (474 lines) consolidates all subsystem initialization, frame lifecycle, and shutdown:
- `Engine::init(EngineDesc)` — creates window, renderer, default textures (white, neutral normal, white cube), loads all shader programs (PBR, shadow, skinned variants), initializes shadow renderer, ImGui (with all key mappings), input system, frame arena, DPI scale query.
- `Engine::beginFrame(float& dt)` — polls events, handles resize, computes dt, updates input, feeds ImGui, begins ImGui frame. Returns false when window should close.
- `Engine::endFrame()` — imguiEndFrame, arena reset, renderer.endFrame.
- `Engine::shutdown()` — cleanup in reverse init order.

### What Engine owns
Window, Renderer, RenderResources, ShadowRenderer, ShaderUniforms, default textures, shader programs, InputSystem, InputState, FrameArena, ImGui lifecycle.

### What Engine does NOT own
ECS Registry, game logic, physics, audio, animation, cameras — those remain the application's responsibility.

### Migration
All 5 demos migrated: helmet_demo, hierarchy_demo, physics_demo, audio_demo, animation_demo. Each demo's `main()` now starts with `Engine eng; eng.init(desc);` and uses `eng.beginFrame(dt)` / `eng.endFrame()` instead of 200+ lines of manual setup.

---

## Shared OrbitCamera (`engine::core::OrbitCamera`)

Header-only camera in `engine/core/OrbitCamera.h` replacing 5 duplicated camera structs across demos:
- `position()` / `view()` — spherical coordinate orbit around target
- `orbit(deltaYaw, deltaPitch)` — right-drag rotation with pitch clamping
- `zoom(scrollDelta)` — scroll to adjust distance
- `moveTarget(inputState, dt, speed)` — WASD/QE target movement relative to camera yaw
- All 5 demos migrated to use it

---

## Scene Serialization (`engine::scene::SceneSerializer`)

- Saves/loads ECS scenes as JSON via the `engine::io` wrapper
- Serializes: TransformComponent (position/rotation/scale), CameraComponent, DirectionalLightComponent, PointLightComponent, SpotLightComponent
- Hierarchy preserved via scene-local IDs and two-pass loading (create entities first, rebuild parent-child second)
- 8 tests covering round-trip, hierarchy, deep chains, ordering independence, rotation/scale preservation

---

## Memory (`engine/memory/`)

- FrameArena (per-frame bump allocator, reset each frame)
- InlinedVector (small-buffer-optimized vector)
- PoolAllocator (fixed-size block allocator)
- ankerl::unordered_dense (vendored high-performance hash map)

### Status
- [x] Memory subsystem implemented (FrameArena, InlinedVector, PoolAllocator, ankerl::unordered_dense — committed)

---

## Skeletal Animation

- Skeleton, AnimationClip, Pose, AnimationSampler, AnimationSystem
- GPU skinning via bone matrix palette uploaded as uniforms
- glTF integration: loads skeleton hierarchy, joint weights, animation clips from GLB
- animation_demo app with playback controls

### Decisions

**Rest poses (identity was wrong):**
Joint poses for channels that are not animated defaulted to identity (position=0, rotation=identity, scale=1). For most glTF skeletons this is incorrect -- bones have specific offsets and rotations in their rest pose. The fix: extract each joint's TRS from the glTF node hierarchy during loading and store it in `Skeleton::restPoses` (a `std::vector<JointRestPose>` parallel to `joints`). `sampleClip()` now initializes the output pose from rest poses before applying animated channels. This means partially-animated clips (e.g., a face animation on a full-body skeleton) produce correct results instead of collapsed geometry.

**Non-indexed mesh support and default normals:**
BrainStem.glb exposed two gaps: (1) some meshes have no index buffer -- the loader now generates sequential indices `{0,1,2,...}` automatically; (2) some meshes lack `NORMAL` and/or `TEXCOORD_0` attributes -- the loader now generates default normals `(0,1,0)` and zero UVs so the surface vertex buffer can still be created. Without these defaults, the skinning vertex buffer (stream 2) could not bind because the surface buffer (stream 1) did not exist, and Metal requires contiguous stream indices.

**Animation events design (polling queue vs callback):**
Two consumption patterns for animation events: (1) a per-entity `AnimationEventQueue` component that game code polls each frame, suitable for game logic that processes events once per tick; (2) a global `EventCallback` on `AnimationSystem`, invoked synchronously during `update()`, suitable for immediate reactions like audio footstep triggers. Both coexist because neither alone covers all use cases. The polling queue avoids forcing game systems to register callbacks; the callback avoids forcing audio systems to poll every entity. Events are suppressed when `kFlagSampleOnce` is set (editor scrubbing) to prevent scrubbing from spamming side effects.

**State machine design (shared definition + per-entity component):**
The `AnimStateMachine` struct is shared across all entities of the same archetype (e.g., all soldiers share one state machine definition). Per-entity runtime state (`currentState`, parameters) lives in `AnimStateMachineComponent`. This separation avoids duplicating the state graph for every entity and allows the definition to be built once at setup time. Parameters use `std::unordered_map<uint32_t, float>` keyed by FNV-1a name hash -- flat enough for the expected parameter count (<20 per entity) without the complexity of a custom allocator.

**Joint-only node skipping:**
glTF skeleton joints are scene nodes, and the original `GltfSceneSpawner` created an ECS entity for every node. For a 60-joint skeleton, this produced 60 empty entities with no mesh -- pure hierarchy noise. The fix: tag each `GltfAsset::Node` with `isJoint = true` during loading (by checking if the node appears in any skin's joint list), and skip joint-only nodes (no mesh) during spawn. Joint entities still get created if they have a mesh attached (e.g., a sword bone that also renders geometry).

**Editor animation panel architecture:**
The animation panel is an ImGui window that displays a clip dropdown, transport controls (play/pause/stop), a time scrubber, a speed slider, and a loop checkbox. Scrubbing while paused uses `kFlagSampleOnce` -- the panel writes a new `playbackTime` and sets the flag, causing `AnimationSystem` to re-evaluate the pose exactly once without advancing time or firing events. The panel operates on the first entity with an `AnimatorComponent` found via a registry view, which is sufficient for the current single-character workflow.

**Bone matrix world transform:**
The initial implementation computed bone matrices as `jointWorld * inverseBindMatrix`, which placed all skinned meshes at the world origin regardless of their `TransformComponent`. The fix: read `WorldTransformComponent::matrix` and multiply it in as `entityWorld * jointWorld * inverseBindMatrix`. This requires `TransformSystem` to run before `AnimationSystem` in the frame loop (the original doc said `AnimationSystem` runs before `TransformSystem`, but the dependency is actually the reverse).

**Imported animations start paused:**
`GltfSceneSpawner` sets `kFlagLooping` but not `kFlagPlaying` on imported `AnimatorComponent`. This prevents animations from auto-playing immediately on spawn, which would be surprising behavior -- the game or editor starts playback explicitly when ready.

### Status
- [x] Skeletal animation implemented (Skeleton, AnimationClip, Pose, AnimationSampler, AnimationSystem, GPU skinning, glTF integration -- committed)
- [x] Skeleton rest poses (JointRestPose, rest pose extraction from glTF, sampleClip uses rest poses -- committed)
- [x] Animation events (AnimationEvent, AnimationEventQueue, EventCallback, event firing with loop boundary handling -- committed)
- [x] Animation state machine (AnimStateMachine, AnimStateMachineSystem, condition-based transitions, builder API -- committed)
- [x] Bone matrix world transform (entityWorld multiplied into bone matrices, TransformSystem runs before AnimationSystem -- committed)
- [x] glTF loader improvements (non-indexed meshes, default normals/UVs, joint-only node skipping, node names, paused import -- committed)
- [x] Editor animation panel (clip selector, transport controls, scrubber, speed slider, loop checkbox, kFlagSampleOnce -- committed)
- [x] IK system (Two-Bone, CCD, FABRIK solvers, IkSystem, two-phase AnimationSystem update -- committed)

---

## JSON Integration (`engine::io`)

- rapidjson wrapped with pimpl — no rapidjson headers leak into public API
- `JsonDocument` (parse from string/file), `JsonValue` (typed accessors, math helpers, iteration), `JsonWriter` (SAX builder with math helpers, file output)
- 22 tests covering parsing, typed getters, math round-trips (Vec2/3/4, Quat), object/array iteration, file I/O, error handling

---

## Inverse Kinematics (Architecture Proposal)

- **Design doc:** `docs/IK_ARCHITECTURE.md`
- **Approach:** IK as a post-process on FK poses -- runs after AnimationSystem samples clips, before bone matrix computation. Requires splitting `AnimationSystem::update()` into `updatePoses()` + `computeBoneMatrices()` with an `IkSystem::update()` call in between.
- **Solvers:** Three solvers in priority order: Two-Bone IK (analytical, O(1), for arms/legs), CCD (iterative, for spines/tails), FABRIK (iterative, better convergence for natural motion). All operate on local joint rotations within the existing `Pose` structure.
- **ECS integration:** `IkChainsComponent` (chain definitions with solver type, joints, blend weight) and `IkTargetsComponent` (per-frame world-space targets). Uses `InlinedVector<T, 4>` for up to 4 chains per entity without heap allocation. New `PoseComponent` holds the intermediate arena-allocated pose between FK and bone matrix computation.
- **Memory:** All per-frame temporaries from FrameArena. Estimated ~1.3 KB per entity, ~65 KB for 50 characters -- well within the 1 MB arena budget.
- **Tradeoff -- pose handoff:** Chose PoseComponent over an internal buffer in AnimationSystem. Adds one component per animated entity but makes the data flow explicit in the system DAG and allows future systems (ragdoll, procedural animation) to also modify the pose without special-casing.
- **Tradeoff -- joint constraints:** Starting with per-axis Euler angle limits (simpler) rather than swing-twist decomposition (more physically correct). Euler limits work well for humanoid characters; swing-twist can be added later if gimbal lock becomes a practical issue.
- **Phased implementation:** (1) Two-Bone solver + IkSystem + unit tests, (2) IK demo app with foot placement, (3) CCD + look-at, (4) FABRIK + hand reach, (5) joint constraints + polish.

### Status
- [x] IK architecture proposed (design doc: `docs/IK_ARCHITECTURE.md`)
- [x] IK implemented: Three solvers (Two-Bone analytical, CCD iterative, FABRIK position-based), IkSystem, IkComponents, FootIkHelper, LookAtHelper — committed
- [x] CCD and FABRIK compile-time disableable via `ENGINE_IK_ENABLE_CCD` / `ENGINE_IK_ENABLE_FABRIK`
- [x] 15 IK unit tests (54 assertions) + 1 IK screenshot test — all passing
- [x] `ik_demo` app: foot IK on uneven terrain with ImGui controls
- [x] `ik_hand_demo` app: interactive mouse-driven arm IK on BrainStem.glb T-pose
- [ ] Joint angle constraints (Phase 5) — not yet implemented

---

## UI Widget System (`engine::ui`)

Retained-mode UI tree for game UIs (menus, HUD, inventory). Not ECS entities — UI nodes are plain C++ objects owned by `UiCanvas` via pool allocation. Architecture in `docs/GAME_LAYER_ARCHITECTURE.md` Section 5.

### Components
- `UiNode` — base class with parent/child tree, anchor+offset layout, `InlinedVector<UiNode*, 4>` children
- `UiCanvas` — root container, owns all nodes, runs layout + draw list generation
- `UiDrawList` — per-frame draw command list (rect, textured rect, text)
- 6 widgets: `UiPanel`, `UiImage`, `UiText`, `UiButton`, `UiSlider`, `UiProgressBar`

### Key decisions
- **Retained-mode, not immediate:** UI tree persists across frames — better for animation, state, and styling than ImGui's immediate model
- **Not ECS:** UI nodes form deep variable-depth trees with parent-relative coordinates. SparseSet iteration (flat, unordered) doesn't map well to recursive tree traversal. Dedicated tree with pointer-based links is simpler and faster for layout.
- **Pool allocation:** UiCanvas owns all nodes. No per-node `new`/`delete`. Canvas destructor frees everything.
- **`InlinedVector<UiNode*, 4>` children:** 4 inline pointers, heap only for 5+ children. Zero heap allocs for typical UI trees.
- **Text rendering:** Three swappable backends behind `IFont` — BitmapFont (default, zero-dependency 8×8 ASCII fallback), MsdfFont (sharp at any size, single shader swap from bitmap), SlugFont (vector-perfect via per-pixel Bézier evaluation, patent-free since March 2026). Runtime-selectable per device tier; see Font Backends → Design subsection below.

### Status
- [x] Phase 1: UiNode tree, UiCanvas, 6 widgets, UiDrawList — 26 tests (77 assertions)
- [x] Phase 2: Layout system (anchor+offset) + event dispatch — 35 tests (107 assertions)
- [x] Phase 3: UiRenderer (orthographic quad batching, sprite shader reuse) + screenshot test — committed
- [x] Phase 4: Text rendering with `IFont` interface + three swappable backends (BitmapFont, MsdfFont, SlugFont). Bitmap path is end-to-end (default font, UiRenderer text pass, widget integration, UiTestApp swap). MSDF and Slug compile, load, and have unit tests; final on-screen rendering pending the items in the Font Backends → Follow-ups subsection below. Commits `646b37b` (foundation), `9835668` (bitmap merge), `aa480f6` (MSDF), `69d8452` (slug merge).
- [ ] Phase 5: JSON style sheets
- [ ] Phase 6 (was Slug/MSDF): merged into Phase 4. Removed.

### Font Backends — Design

**Decision:** ship three IFont backends behind a single interface (`engine/ui/IFont.h`) so the rendering backend can be selected at startup based on GPU tier without touching widget or game code. Per `docs/GAME_LAYER_ARCHITECTURE.md` §5 the per-platform recommendation is Slug for desktop / VR / high-end mobile, MSDF for mid-range mobile, Bitmap for low-end / development.

**Why three backends instead of one:**
- **Bitmap** is the only one that ships *zero-dependency* on day one — the default font is a 96-glyph 8×8 ASCII atlas baked into C++ via `font8x8_basic` (public domain). No filesystem assets, no offline tools, no FreeType, no msdf-atlas-gen. This is what the editor and ui_test currently use.
- **MSDF** is the smallest swap from bitmap — same vertex pipeline, same atlas-based glyph metrics, only the fragment shader differs (median-of-3 + smoothstep on a multi-channel SDF atlas). Adds sharp scaling at any UI size for ~10 lines of shader and one new uniform. The default MSDF atlas needs `brew install msdf-atlas-gen` to generate; documented in `assets/fonts/default/README.md`.
- **Slug** (Eric Lengyel's GPU Centered Glyph Rendering) is vector-perfect at any scale, rotation, or perspective angle by evaluating glyph Bézier curves directly in the fragment shader. The patent expired March 2026 (we shipped in April 2026), and the reference shaders are MIT-licensed at https://github.com/EricLengyel/Slug. Critical for VR text and for any content that needs to look right at extreme zooms.

**Why a common `IFont` interface instead of three separate widgets:**
- Widgets (`UiText`, `UiButton`) hold `const IFont*` and never know which backend produced it. Game code and the layout system are renderer-agnostic.
- `UiRenderer` queries the font for its program + atlas texture and submits accordingly. Per-font batching means a single string is one draw call, and switching fonts only adds a draw-call boundary.
- Per-backend resource binding (e.g. Slug's curve buffer texture and dimension uniform) is hidden behind `IFont::bindResources()`, default no-op.

**Why ship Slug as a partial v1:** the realistic estimate (per the doc) was 2-3 weeks even with reference code. One session reasonably shipped: loader, curve extraction via FreeType, shader port, unit test. The remaining pieces (band table, dynamic dilation, proper AA, full UiRenderer integration) are tracked in `docs/SLUG_NEXT_STEPS.md`. This is by design — a self-contained backend that other agents can integrate at their pace is strictly better than a half-working integrated renderer that has to be undone later.

**Why the editor HUD wasn't swapped:** the bitmap agent left the editor's `dbgTextPrintf` HUD calls alone (per the "skip if entangled" clause in the task brief). The bitmap font path is now proven end-to-end in `UiTestApp`, so the swap is a follow-up — see below.

### Font Backends — Follow-ups

- [x] **Slug end-to-end rendering** — landed in commit `8f28d93`. Took the layout-agnostic approach instead of the planned vertex-attribute change: UiRenderer dispatches to a `renderSlugText` helper when the active font is `FontRenderer::Slug`, submitting one draw call per glyph and writing per-vertex font-space corners into TEXCOORD0 (which the slug fragment shader reads as glyph-local coordinates). SlugFont's metrics were also fixed to use the y-down line-relative convention (same bug MsdfFont had before commit `6270b82`). v1 has hard-edged coverage with no AA — that's tracked in `docs/SLUG_NEXT_STEPS.md` §1-3 for a follow-up.
- [x] **Default MSDF atlas** — generated from `ChunkFive-Regular.ttf` using `msdf-atlas-gen v1.3` (built from source; no Homebrew formula). Output: `assets/fonts/ChunkFive-msdf.{png,json}` (94 ASCII glyphs, 208×208 atlas). Verified end-to-end in `apps/ui_test`: cycling F to MSDF renders ChunkFive's slab-serif on screen. Commit `12afcec`.
- [x] **Editor HUD swap** — landed in commit `755f33e`. The FPS counter, mode label, gizmo shortcut hint, "PLAYING" indicator, status message, and Add Component overlay now render through `UiRenderer` + `MsdfFont` + JetBrains Mono on view 15 (`kViewImGui`). Falls back to `bgfx::dbgTextPrintf` if the JBM atlas fails to load, so the editor never silently goes blank.
- [ ] **Slug band table + AA + dynamic dilation.** Listed in `docs/SLUG_NEXT_STEPS.md` §1-3. Required before Slug becomes the default backend on any platform.
- [ ] **Cubic Bézier subdivision in SlugFont.** v1 collapses cubic curves to a single quadratic — fine for TrueType (which is natively quadratic) but produces visible artifacts for PostScript/CFF/OTF fonts. Use de Casteljau subdivision. Tracked in SLUG_NEXT_STEPS §4.
- [ ] **Slug kerning + Unicode.** v1 stubs `getKerning()` to 0 and only loads ASCII 32-126. Wire `FT_Get_Kerning` and add lazy glyph loading or a configurable range table. SLUG_NEXT_STEPS §5-6.
- [ ] **FreeType as FetchContent dep.** Today's `find_package(Freetype)` works on dev machines with Homebrew but will fail in sandboxed CI. Swap to `FetchContent_Declare(VER-2-13-2)` or document the install requirement. SLUG_NEXT_STEPS §10.

---

## Scene Serializer

- Serialize/deserialize all engine component types to JSON
- Registered handlers: Transform, WorldTransform, Camera, DirectionalLight, PointLight, SpotLight, Mesh, Material, Visible, ShadowVisible, RigidBody, Collider, Name

### Status
- [x] Full component serialization — committed

---

## Editor

Architecture is documented in `docs/EDITOR_ARCHITECTURE.md`. The editor uses native platform UI (AppKit on macOS, Win32 on Windows) rather than ImGui, for superior text rendering, accessibility, and system integration. The 3D viewport renders through bgfx Metal.

### Phase 1 — Native macOS Window with 3D Viewport (Complete)

**What was built:**
- `editor/platform/IEditorWindow.h` — platform-agnostic window interface
- `editor/platform/cocoa/CocoaEditorWindow.h/.mm` — native NSWindow + CAMetalLayer implementation (Pimpl, no AppKit headers in .h)
- `editor/EditorApp.h/.cpp` — owns window, bgfx renderer, scene, camera; runs the frame loop
- `editor/main.mm` — minimal entry point
- `sama_editor` CMake target — links engine_rendering, engine_scene, engine_memory, engine_ecs; does NOT link GLFW or ImGui

**Key decisions:**
- **No GLFW:** The editor uses `NSApplication` with poll-based event draining (`[NSApp nextEventMatchingMask:...]`) instead of GLFW's run loop. This gives full control over frame timing and avoids the GLFW dependency entirely.
- **Pimpl everything:** `CocoaEditorWindow::Impl` holds all ObjC pointers. The .h file is pure C++ — no `#import <Cocoa/Cocoa.h>`.
- **Direct bgfx init:** Rather than routing through `engine::core::Engine` (which pulls in GLFW and ImGui), the editor initializes bgfx directly with the CAMetalLayer as `nwh`. This is intentional — the editor will eventually own its own render pipeline with panel-specific framebuffers.
- **CAMetalLayer via NSView subclass:** `EditorMetalView` overrides `makeBackingLayer` to return a `CAMetalLayer`, which is cleaner than the runtime-API approach used by GlfwWindow.cpp.
- **Scroll handling:** The NSView accumulates scroll deltas via `-scrollWheel:`, consumed and reset each frame in `pollEvents()`.

**Test scene:** A red PBR cube on a gray ground plane, lit by a fixed directional light. Right-drag orbits, scroll zooms. Debug text shows FPS.

### Phase 2 — Scene Hierarchy Panel with Entity Selection (Complete)

**What was built:**
- `editor/EditorState.h` — shared editor state with entity selection tracking (`InlinedVector<EntityID, 16>` for multi-select), selection-changed callback
- `editor/panels/IEditorPanel.h` — panel interface with init/shutdown/update/render and visibility toggle
- `editor/panels/HierarchyPanel.h/.cpp` — flat entity list rendered via bgfx debug text; shows entity names (from `NameComponent`) and component tags ([T], [M], [Mat], [DL], etc.); click detection maps mouse Y to entity row for selection; selected entity highlighted with green background
- Selection highlight in 3D viewport: selected entity rendered with a slightly scaled yellow PBR overdraw
- Keyboard event tracking added to `CocoaEditorWindow` (macOS virtual key code mapping to ASCII)

**Key decisions:**
- **bgfx debug text for panels (temporary):** Using `bgfx::dbgTextPrintf` for hierarchy/properties rendering instead of native `NSOutlineView`. This avoids the complexity of native view management while proving out the data flow. Will be replaced with native UI in a later phase.
- **Click detection via character grid:** Mouse position divided by 8x16 debug text cell size to determine which entity row was clicked. Simple but effective for the debug text approach.

### Phase 3 — Properties Inspector with Transform Editing (Complete)

**What was built:**
- `editor/panels/IComponentInspector.h` — interface for per-component inspectors (`canInspect`, `inspect`)
- `editor/inspectors/TransformInspector.h/.cpp` — displays and edits TransformComponent (position/rotation as euler/scale); Tab to navigate fields, +/- or arrow keys to increment/decrement values
- `editor/panels/PropertiesPanel.h/.cpp` — aggregates registered `IComponentInspector` instances, renders all applicable inspectors for the selected entity via bgfx debug text

**Key decisions:**
- **Keyboard-based editing:** Rather than mouse-based text field input (which requires complex text editing state), values are adjusted with arrow keys and +/-. Position/scale increments by 0.1, rotation by 5 degrees. This is fast to implement and surprisingly usable for quick tweaks.
- **Pluggable inspector architecture:** `PropertiesPanel::addInspector()` accepts any `IComponentInspector`, making it trivial to add inspectors for Material, Light, Physics, etc. in future phases.

### Phase 4 — Transform Gizmos (Complete)

**What was built:**
- `editor/gizmo/TransformGizmo.h/.cpp` — gizmo state machine with three modes (Translate/Rotate/Scale, W/E/R keys); mouse raycasting against axis cylinders for hover detection; drag interaction projects mouse movement onto selected axis
- `editor/gizmo/GizmoRenderer.h/.cpp` — renders colored line geometry (arrows for translate, crosses for scale, circles for rotate) using transient vertex buffers on a dedicated overlay bgfx view (view 50, no depth test against scene)
- `engine/shaders/vs_gizmo.sc` / `fs_gizmo.sc` — minimal position+color passthrough shader for vertex-colored gizmo lines
- `engine/rendering::loadGizmoProgram()` — embedded shader loader for gizmo program

**Key decisions:**
- **Dedicated gizmo shader:** The existing unlit shader hardcodes orange output. Rather than modifying it (which could break tests), a new vs_gizmo/fs_gizmo pair does position+vertex-color passthrough. Two tiny shaders (10 lines total) compiled for all four backends.
- **Constant screen-size gizmo:** Gizmo geometry is scaled by `distance_to_camera * 0.15f` so it appears the same size regardless of zoom level.
- **Overlay rendering:** Gizmo uses bgfx view 50 with no depth test, so it always renders on top of scene geometry. Line anti-aliasing enabled (`BGFX_STATE_LINEAA`).
- **Rotate mode deferred:** Circles are drawn but rotation drag interaction is not yet wired (requires angle delta computation around the rotation circle's center).

### Phase 5: Undo/Redo System

**What was built:**
- `editor/undo/ICommand.h` — interface with `execute()`, `undo()`, `description()`
- `editor/undo/CommandStack.h/.cpp` — undo/redo stacks with configurable max depth (100); `execute()` pushes to undo and clears redo; `undo()` moves to redo stack; `redo()` moves back
- `editor/undo/SetTransformCommand.h/.cpp` — stores entity ID + old/new TransformComponent; sets dirty flag on execute/undo
- Modifier key support added to `IEditorWindow` and `CocoaEditorWindow` (Cmd, Shift, Ctrl, Option via `-flagsChanged:` NSEvent handler)
- TransformGizmo captures transform at drag-start; `dragJustEnded()` signals EditorApp to create a SetTransformCommand

**Key decisions:**
- **Stack-based, not command-graph:** Simple linear undo with redo branch invalidation (executing a new command clears the redo stack). Sufficient for a solo-developer editor; command graphs add complexity without proportional benefit.
- **Idempotent execute:** SetTransformCommand.execute() is safe to call even when the transform is already set (e.g., after gizmo drag), since it writes the same values. This simplifies the CommandStack API.
- **Modifier keys via flagsChanged:** macOS delivers modifier state changes through `-flagsChanged:` rather than `-keyDown:`. The EditorMetalView tracks four modifier booleans, exposed through the IEditorWindow interface.

### Phase 6: Keyboard Shortcuts (Save/Load, Create/Delete)

**What was built:**
- `editor/undo/CreateEntityCommand.h/.cpp` — creates entity with TransformComponent + WorldTransformComponent + NameComponent; selects the new entity; undo destroys it
- `editor/undo/DeleteEntityCommand.h/.cpp` — captures all component data at construction (Transform, WorldTransform, Mesh, Material, VisibleTag, Name, lights); execute destroys the entity; undo re-creates with all saved components
- Cmd+S saves scene via SceneSerializer to `editor_scene.json`
- Cmd+N creates a new empty entity (undoable)
- Delete/Backspace deletes the selected entity (undoable)
- Status messages shown in HUD with a timed fade

**Key decisions:**
- **Component snapshot on delete:** DeleteEntityCommand eagerly captures all known component types at construction time. This is explicit rather than generic (no runtime type iteration), which means new component types require manual additions. The tradeoff is simplicity and compile-time safety over full generality.
- **No file dialog yet:** Cmd+O (open) is deferred; Cmd+S hardcodes the save path. Native file dialogs require additional platform API calls that are best done when the full native menu bar is implemented.

### Phase 7: Asset Browser

**What was built:**
- `editor/panels/AssetBrowserPanel.h/.cpp` — scans a directory for asset files (.glb, .obj, .png, .jpg, .wav, etc.) using `std::filesystem`; lists files with type icons ([3D], [Tx], [Au]) in bgfx debug text; Shift+Up/Down scrolls the list
- Toggle with Tab key; refreshes on open; default directory is `assets`

**Key decisions:**
- **std::filesystem for directory scanning:** C++17 filesystem is sufficient and avoids platform-specific directory enumeration code. The scan is done on toggle (not every frame) to avoid I/O stalls.
- **Read-only for now:** The asset browser lists files but does not yet support drag-to-scene or click-to-load. Loading assets requires wiring up AssetManager with a ThreadPool and IFileSystem, which is deferred to a future phase when native panels replace debug text.

### Phase 8: Additional Component Inspectors

**What was built:**
- `editor/inspectors/MaterialInspector.h/.cpp` — displays albedo RGB, roughness/metallic as ASCII bar graphs, emissive scale; reads material data through RenderResources::getMaterialMut()
- `editor/inspectors/LightInspector.h/.cpp` — displays DirectionalLightComponent (direction, color, intensity, shadows) and PointLightComponent (color, intensity, radius)
- `editor/inspectors/NameInspector.h/.cpp` — displays entity name from NameComponent
- Add-component menu: press 'A' to open a numbered menu (1=DirectionalLight, 2=PointLight, 3=Mesh), Esc to cancel

**Key decisions:**
- **Read-only material inspector:** Material editing via debug text would require a field-navigation system similar to TransformInspector. Since the material inspector will be rebuilt with native UI sliders, the current version is display-only. This avoids throwaway complexity.
- **Inspector registration order:** NameInspector renders first (top of properties panel), then Transform, then Material, then Light. This mirrors conventional editor inspector layouts.

### Phase 9: Console Panel

**What was built:**
- `editor/EditorLog.h/.cpp` — thread-safe singleton with mutex-protected ring buffer (100 entries); three log levels (Info, Warning, Error); `log()`, `info()`, `warning()`, `error()` methods; `forEach()` provides read access under lock
- `editor/panels/ConsolePanel.h/.cpp` — renders the last 12 log entries at the bottom of the screen; color-coded (grey=Info, yellow=Warning, red=Error); toggle with ~ key
- All major editor actions (save/load, undo/redo, create/delete) emit log messages

**Key decisions:**
- **Singleton pattern:** EditorLog is a process-wide singleton because log messages can originate from any subsystem (panels, commands, serializers). A passed-reference pattern would require threading the logger through every component, which is excessive for diagnostic output.
- **Mutex over lock-free:** The ring buffer uses `std::mutex` rather than a lock-free queue. Log writes are infrequent (a few per frame at most), so the mutex cost is negligible. Lock-free would add complexity without measurable benefit at this scale.

### Native AppKit Panels (Complete)

Replaced all bgfx debug text panels with native AppKit views. Debug text was unreadable on Retina (8x16 pixels in 2x framebuffer) and all panels overlapped on the 3D viewport.

**What was built:**
- `editor/platform/cocoa/CocoaHierarchyView.h/.mm` — NSTableView with entity name + component tag columns, selection callback
- `editor/platform/cocoa/CocoaPropertiesView.h/.mm` — NSStackView with float fields, sliders, color wells; rebuilds on selection change (dirty flag)
- `editor/platform/cocoa/CocoaConsoleView.h/.mm` — NSTextView with color-coded messages (NSAttributedString), auto-scroll
- NSSplitView layout in CocoaEditorWindow: vertical split (top + console), horizontal split (hierarchy + viewport + properties)
- Titled panel headers ("Scene Hierarchy", "Properties", "Console") with Auto Layout
- bgfx renders only to the center viewport's CAMetalLayer; mouse input gated by `isMouseOverViewport()`

**Key decisions:**
- **NSSplitView over custom layout:** Provides user-resizable panels for free, matches native macOS UX
- **Dirty flag updates:** Hierarchy rebuilds only on entity create/delete, properties only on selection change or gizmo drag end — prevents per-frame NSTableView/NSStackView churn
- **All Pimpl:** Zero AppKit headers in any .h file

---

## Game Layer

### Overview

The game layer bridges the engine infrastructure (ECS, renderer, physics, audio) and game-specific logic. It replaces the hand-rolled `while (eng.beginFrame(dt))` pattern that was copy-pasted across every demo.

### Components

1. **`IGame` interface** (`engine/game/IGame.h`): Five virtual methods with default empty implementations — `onInit`, `onFixedUpdate`, `onUpdate`, `onRender`, `onShutdown`. Games only override what they need. Registry is passed as a parameter (not through Engine) to keep data flow explicit.

2. **`GameRunner`** (`engine/game/GameRunner.h/.cpp`): Owns the frame loop with a fixed-timestep accumulator pattern (default 60Hz). System execution order: Input -> FixedUpdate (0-N) -> Update -> TransformSystem -> Render -> EndFrame. Spiral-of-death capped at 0.25s accumulator max.

3. **`SceneManager`** (`engine/scene/SceneManager.h/.cpp`): Wraps SceneSerializer with scene lifecycle (load/unload/reload). Tracks scene-owned entities vs persistent entities that survive scene transitions. Persistent entities stored in a small vector with linear scan (typically < 10 entries).

4. **`ProjectConfig`** (`engine/game/ProjectConfig.h/.cpp`): Plain struct parsed from JSON at startup. Sections for window, render, physics, audio. All fields have sensible defaults; missing fields keep defaults. `toEngineDesc()` converts to `EngineDesc` for Engine::init(). The file is optional — omitting it uses all defaults.

### Key Decisions

- **IGame methods are not pure virtual.** Default empty implementations mean trivial games only override onInit + onUpdate. This avoids forcing four empty method stubs on simple demos.
- **GameRunner composes Engine, does not replace it.** Engine remains a pure infrastructure layer. Existing demos with raw beginFrame/endFrame loops continue to work unchanged. Migration is opt-in.
- **Fixed timestep is independent of render frame rate.** Physics always steps at fixedTimestep_ regardless of display refresh rate. On a 30fps device with 60Hz fixed rate, the accumulator runs 2 physics steps per frame. On 120Hz display, most frames run 0 or 1 steps.
- **onRender does not receive Registry.** Games that need the registry during render (e.g., for draw calls) store a pointer during onInit. This keeps the interface minimal — most games render through engine systems, not directly.
- **JSON for ProjectConfig, not TOML/YAML.** The engine already has io::JsonDocument. Adding another parser for a 20-line config is not justified.
- **SceneManager in engine_scene, not engine_game.** Avoids circular dependencies — engine_assets already depends on engine_scene.

### Migration Example

The `physics_demo_v2` target demonstrates migration. The original 500-line `main.mm` becomes a ~10-line entry point plus a focused `PhysicsGame` class implementing `IGame`. The original `physics_demo` remains unchanged for reference.

### Test Coverage

ProjectConfig has 8 unit tests covering: defaults, full config parsing, partial config (missing fields keep defaults), fixedRateHz conversion, empty object, invalid JSON, toEngineDesc conversion, and missing file handling. All existing 437 tests continue to pass.

---

## Tier System

### Overview

The tier system allows developers to define device-tier quality presets (e.g. low/mid/high) that bundle both asset quality and render quality into a single configuration. This replaces ad-hoc per-setting tuning with a unified quality profile that maps cleanly to the asset pipeline and render settings.

### Architecture

1. **`TierConfig` struct** (`engine/game/ProjectConfig.h`): Bundles asset quality fields (`maxTextureSize`, `textureCompression`) and render quality fields (`shadowMapSize`, `shadowCascades`, `maxBones`, `enableIBL`, `enableSSAO`, `enableBloom`, `enableFXAA`, `depthPrepass`, `renderScale`, `targetFPS`). All fields have sensible defaults matching the "mid" tier.

2. **`defaultTiers()`** returns three built-in presets: low (weak mobile — 512px textures, no IBL/bloom, 0.75x render scale), mid (mainstream — 1024px, IBL + bloom, 1.0x), high (flagship — 2048px, all effects, 60fps target).

3. **`getActiveTier()`** resolves the active tier: looks up `activeTier` name in user-defined `tiers` map first, then falls back to built-in defaults, and finally to "mid" if the name is unknown.

4. **`tierToRenderSettings()`** converts a `TierConfig` to `RenderSettings` for the renderer — shadow resolution/cascades/filter, IBL, SSAO, bloom, FXAA, depth prepass, render scale.

5. **`TierAssetResolver`** (`engine/assets/TierAssetResolver.h/.cpp`): `resolveAssetPath(base, relative, tier)` checks `<base>/<tier>/<relative>` first, falls back to `<base>/<relative>`. This enables per-tier asset overrides without duplicating the entire asset tree.

### Key Decisions

- **Explicit tiers over runtime auto-detection.** Auto-detecting quality from GPU model strings is unreliable (string formats vary across vendors, available memory depends on background apps). Explicit tiers let developers test each configuration and guarantee performance. Runtime detection can be added later as a hint for the default tier selection.

- **TierConfig is separate from the tool-side TierConfig.** The engine-side `engine::game::TierConfig` (in ProjectConfig.h) bundles both asset and render quality. The tool-side `engine::tools::TierConfig` (in AssetProcessor.h) only has asset-quality fields (`maxTextureSize`, `astcBlockSize`). This avoids the asset tool depending on the full engine, but the asset-quality fields must stay in sync manually.

- **depthPrepass defaults to false.** Mobile GPUs use tile-based deferred rendering (TBDR) where the hardware performs hidden surface removal efficiently. A CPU-side depth prepass would double draw calls for minimal benefit. Desktop GPUs benefit more from depth prepass, but the default is tuned for mobile.

- **Shadow filter is threshold-based.** `tierToRenderSettings` uses `PCF4x4` for shadow maps >= 2048 and `Hard` for smaller maps. This avoids wasting PCF samples on low-resolution shadow maps where the filtering would not improve visual quality.

- **Partial tier JSON supported.** A JSON tier definition can specify only the fields that differ from defaults. This makes it easy to create a custom tier that tweaks one or two settings without repeating the full configuration.

### Android Runtime Tier Auto-Detection (2026-04-30)

`engine/platform/android/AndroidTierDetect.{h,cpp}` plus a one-line wiring change in `Engine::initAndroid` and `GameRunner::runAndroid(configPath)` lets games leave `activeTier` empty (or set the new sentinel `"activeTier": "auto"`) and have the engine pick a tier at process start. Mirrors the iOS `IosTierDetect` API surface (`detect…Tier()`, `…TierLogName()`, `…TierToProjectConfigName()`) so future cross-platform tooling can branch on a single shape.

**Why RAM (`/proc/meminfo` `MemTotal`) + GPU substring rather than alternatives:**

- **Considered:** JNI bridge to `ActivityManager.getMemoryInfo`. Rejected because plumbing a `JNIEnv*` through to the platform layer just to read a number `/proc/meminfo` already prints would add a JNI dependency to a code path that otherwise needs none. Tradeoff: we report total RAM (kernel + reserved) rather than "available to apps", but every public device-tier reference uses total RAM as the bucket boundary so this is the right number anyway.
- **Considered:** A full PCI-id table for Android GPUs (analogous to iOS `hw.machine` -> chip). Rejected because there is no Apple-style chip→model map for Android — the table would be enormous and stale within months. A case-insensitive substring match against the GPU device name catches whole families (Adreno 7xx, Mali-G7xx, Xclipse 9xx, ...) in 12 entries, and the RAM signal saves us when a brand-new generation ships.
- **Considered:** Defer entirely (the original Phase E note suggested deferring runtime detection because GPU strings are inconsistent). Reconsidered because a) the iOS branch already does this and games porting cross-platform expect parity, b) the substring approach plus a RAM fallback is robust enough for the v1 default — when the project pins a tier explicitly we don't override it anyway.

**Tradeoffs accepted:**

- The v1 wiring passes an empty GPU name from `Engine::initAndroid` because bgfx exposes `vendorId` / `deviceId` (PCI IDs) but not the human-readable device name pre-init, and we don't want to spin up a separate Vulkan instance just to query `VkPhysicalDeviceProperties.deviceName` when the RAM signal alone is enough on real devices. The GPU heuristic is tested and ready for when a future caller (e.g. a Vulkan-pre-init query path or an in-game settings UI that reads `bgfx::getCaps()` after init) wants to refine the answer.
- We do not silently override an explicit `activeTier` set in `project.json`. Detection only fills in when the field is empty or set to `"auto"`. This preserves the original Phase E design tenet ("explicit tiers over automatic quality scaling") while still providing a sensible default for games that don't ship per-device tuning.
- Combination logic when the two signals disagree (e.g. flagship GPU paired with little RAM, or weak GPU in a tablet with lots of RAM) lands on Mid rather than picking either extreme. Ties always go to the safer choice.
- `Unknown` (no GPU match + parse failure on `/proc/meminfo`) maps to `"mid"` for the same reason iOS uses Mid as its safe-default — `"high"` risks overheating an unidentified low-end device; `"low"` leaves modern hardware visibly under-utilised.

---

## Asset Pipeline CLI

### Overview

`sama-asset-tool` is a standalone command-line tool that processes source assets for a target platform and quality tier. It discovers textures, shaders, and models in an input directory, processes them, and writes the results plus a JSON manifest to an output directory.

### Architecture

1. **`AssetProcessor`** (`tools/asset_tool/AssetProcessor.h/.cpp`): Orchestrates the pipeline — parses CLI args, discovers assets via `TextureProcessor`/`ShaderProcessor`/model discovery, processes them, and writes `manifest.json`.

2. **`TextureProcessor`** (`tools/asset_tool/TextureProcessor.h/.cpp`): Discovers `.png`, `.jpg`, `.jpeg`, `.ktx`, `.ktx2`, `.dds` files. Output format is `.ktx` with ASTC compression (stubbed — currently copies files as-is).

3. **`ShaderProcessor`** (`tools/asset_tool/ShaderProcessor.h/.cpp`): Discovers `.sc` bgfx shader files, skips `varying.def.sc`. Determines shader type from filename prefix (`vs_`/`fs_`/`cs_`). Output format is SPIRV for Android, Metal for iOS. Compilation via `shaderc` is stubbed — currently copies files.

4. **`manifest.json`**: Lists all processed assets with platform, tier, timestamp, and per-asset type/source/output/format/dimensions.

### Key Decisions

- **Standalone tool, not a library.** The asset tool is a separate executable that does not link against the engine runtime. This keeps the tool lightweight and avoids pulling in bgfx, ECS, or renderer dependencies for offline processing.

- **ASTC encoding deferred.** The `astc-codec` library in third_party is decode-only. Full ASTC encoding requires the `astcenc` CLI tool. Rather than block the pipeline on this dependency, textures are copied as-is with the correct manifest metadata. The compression step can be added later without changing the manifest format or pipeline structure.

- **Path traversal protection.** Model file copying validates that the destination path does not escape the output directory using `std::mismatch` on canonicalized path components. This prevents malicious or malformed relative paths from writing outside the output tree.

- **Dry-run mode.** `--dry-run` skips all file I/O (including output directory creation) and prints what would be processed. This is useful for CI validation and debugging asset discovery without side effects.

---

## APK Packaging (`android/build_apk.sh`)

### Overview

A 7-step Gradle-free APK build pipeline that cross-compiles the engine, processes assets, and produces a signed APK ready for device installation. Implemented in Phase F of the Android roadmap.

### Key Decisions

- **Gradle-free, shell-based pipeline.** Direct use of `aapt2` + `zipalign` + `apksigner` keeps the build fast and avoids Gradle's 30+ second JVM startup. The tradeoff is manual APK assembly (manifest linking, zip insertion, alignment, signing), but games have minimal Android resources (just a manifest — no layouts, no drawables).

- **7-step sequential pipeline.** Each step depends on the previous: (1) NDK build → (2) asset processing → (3) staging directory assembly → (4) aapt2 link base APK → (5) zip in native lib + assets → (6) zipalign → (7) apksigner. Intermediate files are cleaned up after signing. This is simpler than a parallel approach and the total time is dominated by step 1 (NDK build), making parallelism moot.

- **Debug keystore auto-creation.** On first build without `--keystore`, the script creates a debug keystore at `$HOME/.android/debug.keystore` via `keytool -dname` (non-interactive). This avoids blocking first-time users with keystore setup. The debug keystore uses the standard Android debug credentials (`android`/`androiddebugkey`) so `adb install` works without user configuration.

- **Asset tool fallback to raw copy.** If `sama-asset-tool` is not built, assets are copied as-is with a warning. This allows APK testing before the asset pipeline is fully operational. The alternative (hard-fail) would block developers who only want to test the NDK build.

- **Upfront dependency validation.** All required tools (NDK, SDK, aapt2, zipalign, apksigner, android.jar, cmake, keytool) are validated before any work begins. This avoids the frustration of a build failing halfway through after a 5-minute NDK compile because `zipalign` is missing.

- **Build-tools auto-discovery.** The script finds the latest installed build-tools version via `ls -d | sort -V | tail -1`. This avoids hardcoding a build-tools version that the user may not have installed. Falls back to PATH-based lookup for each tool individually.

### Status
- [x] APK packaging implemented (`android/build_apk.sh`, `android/create_debug_keystore.sh` — committed)

---

## Android AAB Generation (`android/build_aab.sh`)

### Overview

AAB (Android App Bundle) generation for Play Store distribution. Builds a base module zip via `bundletool` with multi-ABI support. Implemented in Phase H of the Android roadmap.

### Key Decisions

- **Base module zip via `bundletool`, not APK-to-AAB conversion.** Converting APK-to-AAB with `bundletool` requires an intermediate APK that already has all resources compiled, then decompiling and re-packaging — fragile and wasteful. Instead, we construct the AAB module directory structure directly (manifest, lib, assets), zip it into a base module, and pass it to `bundletool build-bundle`. This approach avoids the full APK pipeline (no `zipalign`, no `apksigner` for the intermediate), uses `aapt2 link --proto-format` for protobuf resources that `bundletool` expects, and keeps the script self-contained.

- **Multi-ABI by default.** Building both `arm64-v8a` and `armeabi-v7a` doubles compile time but is critical for Play Store coverage. The `--skip-armeabi` flag is provided for development iteration where only arm64 matters. Play Store generates per-device APKs from the multi-ABI AAB, so end users only download the ABI they need.

- **`jarsigner` for AAB signing, not `apksigner`.** AABs use `jarsigner` (JDK tool), not `apksigner` (Android SDK tool). This is a Google requirement — Play Store re-signs the per-device APKs with Google's key, so the AAB itself uses the standard JAR signing scheme.

### Status
- [x] AAB generation implemented (`android/build_aab.sh` — committed)

---

## Android Vulkan + UiRenderer (Milestone: 2026-04-11)

### Context

Vulkan rendering with real shader programs (not just `bgfx::dbgTextPrintf`) is now working on Android. The full stack is verified on Pixel 9 at 60fps: Vulkan + SPIRV shaders + UiRenderer + BitmapFont text rendering + gyroscope + touch.

### Decision: bgfx NUM_SWAPCHAIN_IMAGE Patch

**Problem:** bgfx hardcodes `NUM_SWAPCHAIN_IMAGE=4` in `renderer_vk.cpp`. The Pixel 9's Vulkan driver requests 5 swapchain images (minImageCount=3 + driver overhead). When bgfx cannot index all images, `vkAcquireNextImageKHR` returns indices beyond the array bounds, causing silent rendering failure or `VK_ERROR_OUT_OF_DATE_KHR`.

**Fix:** Patch to 8 via CMake compile definition on the bgfx target. The value 8 is generous (no known device needs more than 5) and costs only ~128 bytes of additional stack arrays.

**Why not fork bgfx:** A compile definition is the least invasive approach. It requires no source file changes, survives bgfx version updates, and can be removed if upstream accepts a `BGFX_CONFIG_` knob for this value.

**Tradeoff:** If bgfx changes the internal define name, our patch silently stops working. Mitigated by verifying Vulkan init succeeds at app startup (logcat shows swapchain image count).

### Decision: SPIRV Shader Loading from APK Assets

**Problem:** Desktop uses Metal .bin shaders loaded from the filesystem. Android needs SPIRV .bin shaders loaded from APK assets (no writable filesystem for shaders).

**Approach:** Pre-compile shaders to SPIRV on the host via `compile_shaders.sh` (uses bgfx's `shaderc` tool). The `.bin` outputs go into `shaders/spirv/` and are packaged into the APK's `assets/shaders/spirv/` directory. At runtime, `AndroidFileSystem` loads them via `AAssetManager` — same `IFileSystem::readFile()` interface as desktop.

**Why not embed in .so:** Embedding via `xxd` or binary includes bloats the shared library's text segment, prevents shader-only updates, and breaks the filesystem abstraction that all other asset loading uses. APK asset loading is zero-copy via `AAsset_getBuffer()` so there is no performance penalty.

**Why not runtime compilation:** SPIRV compilation from GLSL at runtime (via `shaderc` or `glslang`) adds ~2MB to the binary and 100-500ms startup latency. Pre-compilation is strictly better for shipping games.

---

## TODO — Performance & Hygiene

Items reviewed during the 2026-04-12 engine audit but deferred due to risk or complexity:

- [x] **AssetManager string copies in load()** — *fixed 2026-04-27.* `pathToSlot_` was changed from `ankerl::unordered_dense::map<std::string, uint32_t>` to `map<std::string, uint32_t, core::TransparentStringHash, std::equal_to<>>`, and the `load()` callsite now does `pathToSlot_.find(path)` directly with a `std::string_view`. ankerl's heterogeneous lookup kicks in when both Hash and KeyEqual expose `is_transparent`, so `.find(string_view)` no longer constructs a temporary `std::string` on the hot path. The two write paths (`allocSlot` `pathToSlot_[std::string(path)] = idx` and `scheduleDestroy` `pathToSlot_.erase(records_[index].path)`) still pass owning `std::string` keys because the map owns its keys — read-only deduplication lookups are the only allocating sites that mattered. Reasoning: every `assets.load("foo.png")` call from games hits the dedup `find` first; on a typical frame with even a handful of load calls this avoids `O(N)` heap allocations for the same hot `string_view`. Tradeoff: one extra forwarding hash struct (`engine::core::TransparentStringHash`) defined in `engine/core/TransparentHash.h`. Both string and string_view route to ankerl's wyhash so equal keys hash equally.
- [x] **AnimationSystem boneBuffer_ lifetime documentation** — *fixed 2026-04-27.* Added a 3-line "why" comment at both `update()` (line 183) and `computeBoneMatrices()` (line 360) explaining that the raw `boneBuffer.data()` pointer survives the local `std::pmr::vector`'s destructor only because the underlying allocator is the per-frame `FrameArena` — its memory is reclaimed only by `reset()` at end-of-frame, not when the vector goes out of scope. With a normal allocator this would be a use-after-free. No code change. Reasoning: this is exactly the kind of subtle aliasing that makes future readers nervous and tempts them to refactor it "safer" (e.g. moving the vector into the class) without realizing the design depends on the arena. The comment makes the contract explicit so the system can be modified safely.
- [x] **Transparent hash for asset path lookups** — *audited 2026-04-27.* Audited all `unordered_map<std::string, …>` and `ankerl::unordered_dense::map<std::string, …>` declarations across `engine/`. Only `AssetManager::pathToSlot_` is queried with `std::string_view`; converted (see entry above). `engine/game/ProjectConfig.h` (`tiers`) and `engine/scene/SceneSerializer.cpp` (`handlerMap`) are also string-keyed but are queried only with `std::string` parameters, so converting them would be churn for no measurable win — left alone. `RenderResources` is integer-keyed (free-list slots) — no string maps at all, contrary to the original TODO's hypothesis. Shared helper lives at `engine/core/TransparentHash.h` (`engine::core::TransparentStringHash`) so future asset-path maps can opt in with one line. Reasoning: scope-discipline — only convert when there's a real string_view callsite, since transparent lookup costs a small template-API surface change.
- [x] **Android Vulkan validation layer infrastructure gated behind `SAMA_ANDROID_DEBUG_LAYERS` (CMake option, OFF by default).** The diagnostic plumbing that found the shadow bug below — `init.debug = true` in `Renderer.cpp`, `BX_CONFIG_DEBUG=1` on `bx`, and the `libVkLayer_khronos_validation.so` copy in `build_apk.sh` — is now opt-in. Reasoning: bundling the validation layer adds ~25 MB to the APK, and `BX_CONFIG_DEBUG=1` enables `BX_TRACE` output in Release builds (perf cost + log noise). Tradeoff: enabling the flag is now a two-step ritual (set the env var when invoking `build_apk.sh`, which forwards `-DSAMA_ANDROID_DEBUG_LAYERS=ON` to cmake), but the flag is mirrored across all three layers (cmake option, C++ macro, shell env var) so the C++ code and APK contents stay in sync. `BX_ANDROID_LOG_TAG="bgfx"` stays unconditional — it only renames the log tag, no perf or size cost. To enable: `SAMA_ANDROID_DEBUG_LAYERS=1 bash android/build_apk.sh` (and place the .so under `android/validation_layers/<abi>/`).
- [x] **Android Vulkan shadow rendering — RESOLVED.** Caught by `VK_LAYER_KHRONOS_validation`: `vkCreateFramebuffer(): pCreateInfo->layers is zero` (VUID-VkFramebufferCreateInfo-layers-00889). Our `ShadowRenderer.cpp` was calling `at.init(atlas_, Access::Write, 0, 0)` — the inline comment claimed the 4th arg was `mip` but it's actually `_numLayers`, with default `1`. Setting it to `0` made the Vulkan framebuffer have `layers=0`, so all shadow-pass rendering went to nowhere on real Android Vulkan drivers (Mali AND Adreno). Emulator gfxstream silently treated 0 as 1, hiding the bug from initial testing on `sama_high`. Fix: explicit named args `at.init(atlas_, Write, /*layer*/0, /*numLayers*/1, /*mip*/0)`. Verified working on Samsung Galaxy S24 (Adreno 750) and Pixel 9 (Mali-G715) — clear helmet-shaped cast shadow on the ground in both. *Original hypothesis was Mali-specific, but verified via Android Studio Device Streaming on Samsung Galaxy S24 (Adreno 750, Snapdragon 8 Gen 3) — same APK shows the same broken-shadow symptom as Pixel 9 (Mali-G715). So it's not a Mali driver bug; it's a fundamental engine/bgfx Vulkan issue exposed by real Android drivers. The emulator's gfxstream→MoltenVK translation must be normalizing or hiding some behavior that breaks on real implementations.* Shadow works on Metal desktop and Android emulator (gfxstream Vulkan via MoltenVK on M3 — confirmed clear helmet-shaped cast shadow), but NOT on Pixel 9 (Mali-G715) NOR Samsung Galaxy S24 (Adreno 750). All CPU-side checks pass (helmet has `ShadowVisibleTag`, valid mesh 14556/46356 verts/idx, correct world transform, shadow matrix non-NaN, atlas handle valid, sensible UV projection). Tried on Mali without success: `D24S8` vs `D32F`, bias-matrix Y-flip toggle, `SampleCmp` vs `SampleCmpLevelZero` (`OpImageSampleDrefImplicitLod` vs `ExplicitLod`). *Tried bgfx#3486 patch (commit 435a2d6) — fixed `colorAttachmentCount = numColorAr` instead of `max(numColorAr, 1)`. Verified the patch made it into the libsama_android.so build. Still no visible shadow on Pixel 9.* So the phantom color attachment was NOT the root cause on this device. *Then tried switching to manual depth comparison* — replaced `SAMPLER2DSHADOW`/`shadow2D` in `fs_pbr.sc` with `SAMPLER2D` + `texture2D(...).x` + `step(refZ, depth)`, and removed `BGFX_SAMPLER_COMPARE_LEQUAL` from the shadow atlas. Verified SPIRV switched from `OpImageSampleDref*` to `OpImageSampleImplicitLod` + `OpExtInst Step`. Emulator still shows correct helmet-shaped shadow; **Mali still shows no shadow**, **and S24/Adreno also shows no shadow with the manual path**. *Then re-tested hardware `shadow2D` on S24 to make sure the manual switch wasn't masking an Adreno-only success — same broken result.* So both the hardware comparison path and the manual `step()` path fail identically on both Mali and Adreno real-device Vulkan implementations. *This narrows the issue significantly:* even sampling the D32F atlas as a regular texture returns 1.0 on Mali. Either (a) the depth pass isn't actually writing into the atlas on Mali (despite the framebuffer attachment being valid), or (b) Mali drivers refuse to provide D32F texture data to fragment-shader regular sampling regardless of view aspect/sampler config. *Next escape hatch:* `bgfx::blit` the D32F atlas to an `R32F` color texture after the shadow pass, then sample the R32F in the PBR shader. This bypasses the entire depth-as-texture issue on Mali. *Other candidates:* SPIR-V `OpTypeImage` Depth flag (1 vs 2 — driver disagreement, see DXC#1107); Mali image-view aspect mask (DEPTH vs DEPTH|STENCIL); function-call wrapping breaking shaderc inlining of `shadow2D` (PPSSPP Mali driver-bugs page). *Path forward:* (a) apply the bgfx#3486 patch locally and test, (b) write a manual-compare diagnostic shader (regular `SAMPLER2D` + `step(refZ, sampledDepth)`) to confirm whether the issue is the Dref path or the depth atlas content itself, (c) capture with ARM Performance Studio / libGPULayers. *Workaround:* test shadow content on the emulator; ship without dynamic shadows on Mali devices, or use a non-Dref technique (variance shadow maps, screen-space shadows).

- [x] **Skinned mesh rendering broken on all desktop demos — RESOLVED 2026-04-28** in `CMakeLists.txt` `compile_shader()` / `compile_shader_pp()` by passing `DEFINES BGFX_CONFIG_MAX_BONES=128` through to `_bgfx_shaderc_parse`. *Symptom:* `animation_demo`, `ik_demo`, `ik_hand_demo` rendered the skinned mesh as a few huge stretched triangles instead of the actual character — vertices were sampling out-of-bounds bone matrices. Static mesh rendering (helmet_demo) was unaffected. *Root cause:* `bgfx/src/bgfx_shader.sh` declares `uniform mat4 u_model[BGFX_CONFIG_MAX_BONES]`. The macro defaults to 1 when undefined, so shaderc compiled `u_model[1]`. `setTransform(matrices, 24)` then uploaded 24 matrices into a 1-slot array, and the skinned vertex shader's `u_model[int(a_indices.x)]` read garbage for every vertex whose bone index was > 0. The `BGFX_CONFIG_MAX_BONES=128` we set on the bgfx C++ target only affected the runtime library, not the separate shaderc tool. *Why we never noticed earlier:* the demos were broken since the bgfx update commit `a1da931` (Apr 22) but every ImGui-using demo also crashed at startup with the stbtt bug fixed in commit `69280a3` — once the demos started actually drawing, the visual regression became visible. Verified: `animation_demo` now shows three skinned foxes with shadows; `ik_demo` shows the IK character; `engine_tests` 6221/6221 and `engine_screenshot_tests` 25/25 still pass. *Why no test caught this:* the IK screenshot test (`TestSsIk.cpp`) draws **cubes at solved joint positions** rather than rendering a skinned mesh, so it never exercised `vs_pbr_skinned.sc`'s `u_model[]` indexing. *Closed by commit `9639361`* — `tests/screenshot/TestSsAnimation.cpp` now loads `Fox.glb`, samples clip 0 at `t=0.5` and clip 2 at `t=0.2`, and golden-compares the rendered skinned mesh; any future regression that re-introduces `u_model[1]` (or any bone-matrix math break) produces a visible diff against `tests/golden/animation_fox_{idle_t05,run_t02}.png`.

- [x] **`stbtt_GetGlyphShape` crash in ImGui-using demos — RESOLVED 2026-04-27** via `patches/bgfx_imgui_default_font.patch`, wired into the bgfx FetchContent PATCH_COMMAND in `CMakeLists.txt`. ImGui 1.92's lazy font baker faulted at `stbtt_GetGlyphShape + 2680` on the first text-bearing call (`ImGui::Text` → `CalcTextSize` → `ImFontBaked_BuildLoadGlyph` → `stbtt_MakeGlyphBitmapSubpixel`). The fault site is the compound-glyph `STBTT_malloc`: codegen loads the allocator pointer (`s_ctx.m_allocator`) from the wrong global on arm64 macOS — the allocator itself is intact, but x26 ends up holding garbage so the indirected `ldr x8, [x26]` faults. Heap/link-order sensitive: at the time of cdd55b9 it crashed five demos (physics_demo, physics_demo_v2, animation_demo, ik_demo, ik_hand_demo, audio_demo) but not hierarchy_demo / helmet_demo, with no clean theory for the difference. *Patch:* the bgfx example imgui glue (`bgfx/examples/common/imgui/imgui.cpp`) loads `Roboto`/`RobotoMono`/`Kenney`/`FontAwesome` via `AddFontFromMemoryTTF` — TrueType fonts with compound glyphs that exercise the broken stb_truetype path. The patch deletes those four `AddFontFromMemoryTTF` calls and the merge loop, replacing them with a single `io.Fonts->AddFontDefault()` (ProggyClean — bitmap font, no compound glyphs, never enters `stbtt_GetGlyphShape`). Both `m_font[Regular]` and `m_font[Mono]` are pointed at the default font. *Why this fix vs. alternatives:* (a) we can't easily bisect/fix the codegen-vs-globals interaction without forking bgfx or upgrading ImGui, both of which we explicitly rule out; (b) the embedded fonts are a cosmetic upgrade over ProggyClean — the engine's first-class text rendering goes through `MsdfFont`, so ImGui's font is only used inside debug overlays. Bypassing the entire stb_truetype path eliminates the crash class regardless of which demo's heap layout would otherwise trip it. *Tradeoff:* ImGui text in `hierarchy_demo` and `helmet_demo` (which were not crashing) now uses the chunky ProggyClean bitmap font instead of Roboto. Acceptable cosmetic regression — the previous workaround (cdd55b9) had already given up ImGui entirely in the crashing demos and routed their HUD content through `DebugHud`, so this fix is strictly an improvement: ImGui works again everywhere, just with a less polished font. The cdd55b9 workaround was reverted on the same commit, restoring the original ImGui-based panels in `physics_demo`, `physics_demo_v2`, and `animation_demo`. Verified via the demo run-suite (all eight demos run ≥6 s with no segfault) and `engine_tests` (6221/6221 assertions still passing).

---

## Cross-Platform Engine & Android Game Runner

### Overview

The `Engine` class and `GameRunner` now work on both desktop and Android. `IGame` implementations are 100% platform-agnostic -- the same class runs on both platforms with zero `#ifdef` in game code. This was the key missing piece that tied the Android platform layer (Phases A-C) into a usable game development workflow.

### Design Decisions

**Single Engine class with `#ifdef __ANDROID__` vs EngineAndroid subclass**

Considered: (1) inheritance hierarchy with `EngineDesktop` and `EngineAndroid` subclasses, (2) strategy pattern with injected platform backends, (3) `#ifdef` sections within one class.

Chose option 3. The public API is identical on both platforms -- `beginFrame()`, `endFrame()`, `resources()`, `inputState()`, shader accessors, framebuffer dimensions all have the same signatures and semantics. Only the init path (GLFW vs ANativeWindow), window management, and input backend differ, and these are all private implementation details. An inheritance hierarchy would split the frame lifecycle logic across two files with inevitable duplication. A strategy pattern would add runtime indirection for decisions that are fully determined at compile time. The `#ifdef` approach keeps all code in one place, compiles out the unused platform entirely, and costs zero at runtime.

**`samaCreateGame()` extern linkage vs registration macro**

Considered: (1) `extern` factory function, (2) `REGISTER_GAME(MyGame)` macro with static initializer, (3) explicit `setGame()` call in `android_main`.

Chose option 1. There is exactly one game per APK, so a registry is overkill. Static initialization ordering (option 2) is a well-known source of subtle bugs in C++, especially across translation units. An explicit call in `android_main` (option 3) would require games to provide their own `android_main`, duplicating the bootstrap boilerplate. The `extern` function is explicit, simple, and familiar -- it is essentially the `main()` equivalent for Android.

**Shared `runLoop()` in GameRunner**

The frame loop (fixed-timestep accumulator, IGame callback dispatch, beginFrame/endFrame) is factored into a private `runLoop(Engine&)` method. Both `run()` (desktop) and `runAndroid()` (Android) call it after platform-specific Engine initialization. This guarantees identical frame timing on both platforms and means bug fixes apply everywhere. The alternative -- duplicating the loop in each entry point -- would inevitably drift as features are added.

---

## First Android Hardware Render (Pixel 9, Vulkan, 2251x1080)

### Overview

The Sama engine successfully renders on a Pixel 9 running Vulkan at 2251x1080. The `android_test` app demonstrates touch input, multi-touch trails, gyroscope tilt, and bgfx debug text at full frame rate. This validates the entire Android pipeline: NDK cross-compile, APK packaging, NativeActivity lifecycle, bgfx Vulkan initialization, and the cross-platform IGame/GameRunner architecture.

### Design Decisions

**Event loop timeout must be recomputed each iteration**

The Android event loop in `Engine::beginFrame()` uses `ALooper_pollAll(timeout, ...)` in a loop. The timeout must be recomputed on every iteration, not cached before the loop. Reasoning: the first iteration may process `APP_CMD_INIT_WINDOW` which makes the window ready, or `APP_CMD_GAINED_FOCUS` which sets the focused flag. If the timeout was computed once before the loop (as `-1` because the window was not yet ready), the loop would block indefinitely on the second iteration even though the window is now ready. The fix is `int timeout = (androidWindow_->isReady() && focused_) ? 0 : -1;` at the top of each iteration.

**`beginFrameDirect` on Android (no post-processing)**

On desktop, `beginFrame()` sets up the post-process framebuffer (bloom, FXAA, tone mapping) and begins an ImGui frame. On Android, shaders are stubbed so the post-process pipeline cannot function. Rather than conditionally initializing an incomplete post-process chain, Android calls `renderer_.beginFrameDirect()` which targets the swapchain directly. This means games on Android render to view 0 without any intermediate framebuffer. The tradeoff is no post-effects, but the benefit is simplicity and guaranteed correctness -- there is no half-broken pipeline producing artifacts. When shader loading is implemented, the Android path can switch to the full `beginFrame` pipeline.

**SPIRV shader pipeline for Android**

Android shaders are cross-compiled to SPIRV using the desktop-built `shaderc` binary (`android/compile_shaders.sh`). The compiled `.bin` files are packaged into the APK's `assets/shaders/spirv/` directory and loaded at runtime via `AAssetManager`. The minimum shader set (sprite, rounded_rect, msdf) is compiled by default; `--all` compiles the full engine shader suite. The `fs_msdf` shader is included in the minimum set to support `MsdfFont` text rendering on Android.

**bgfx.cmake dependency update (bkaradzic/bgfx.cmake)**

Switched from the stale `widberg/bgfx.cmake` fork to the official `bkaradzic/bgfx.cmake` repo. This resolved the Vulkan swapchain image count issue natively (`kMaxBackBuffers = max(BGFX_CONFIG_MAX_BACK_BUFFERS, 10)`) and removed the need for our CMake patch. The update required several API adaptations:

- `shaderc_parse` → `_bgfx_shaderc_parse`, `shaderc` target → `bgfx::shaderc`
- `mtxFromCols3` → `mtxFromCols` (shader function renamed)
- `instMul` → `mul` (removed from bgfx_shader.sh)
- ImGui `KeyMap`/`KeysDown` → `AddKeyEvent()` (newer dear-imgui bundled)
- Disabled WGSL shader support (`BGFX_PLATFORM_SUPPORTS_WGSL=0`) since we don't compile WGSL shaders
- Added `bimg` link to `engine_debug` (bgfx imgui wrapper now requires it)

Two Vulkan-specific fixes for Android:

1. **Surface format:** bgfx defaults `formatColor` to BGRA8 (`VK_FORMAT_B8G8R8A8_UNORM`), which is optional on mobile Vulkan. Android Mali/Adreno GPUs typically only expose RGBA8 (`VK_FORMAT_R8G8B8A8_UNORM`). We set `init.resolution.formatColor = RGBA8` on Android. Without this, swapchain creation fails silently and bgfx falls back to OpenGL ES.

2. **Fragment shading rate pNext crash (emulator):** bgfx unconditionally chains `VK_KHR_fragment_shading_rate` properties into `vkGetPhysicalDeviceProperties2` pNext. While valid per the Vulkan spec, the Android emulator's gfxstream layer aborts on unknown struct types. Patched via `patches/bgfx_emulator_compat.patch` to only chain when the extension is supported. Verified working on emulator (sama_low/mid/high AVDs) and real hardware (Pixel 9).

**MsdfFont::loadFromMemory for Android**

Added `MsdfFont::loadFromMemory(jsonData, jsonSize, pngData, pngSize)` to support loading MSDF fonts from APK assets via `AAssetManager`. The existing `loadFromFile` was refactored to delegate to `loadFromMemory`. The raw JSON + PNG font files are copied into the APK alongside the asset-tool-processed KTX files, since `MsdfFont` needs the original format. Verified ChunkFive MSDF rendering on Pixel 9 and emulator.

**`libc++_shared.so` must be packaged in APK with `c++_shared` STL**

The NDK build uses `ANDROID_STL=c++_shared` because: (1) `c++_static` can cause ODR violations when multiple shared libraries link the static STL, (2) exception handling across shared library boundaries requires a shared runtime, (3) `libc++_shared.so` is small (~800KB). The consequence is that `build_apk.sh` must copy `libc++_shared.so` from the NDK sysroot into the APK's `lib/<abi>/` directory. The NDK provides it at `$ANDROID_NDK/toolchains/llvm/prebuilt/<host>/sysroot/usr/lib/<triple>/libc++_shared.so`. Without it, the app crashes immediately on launch with `java.lang.UnsatisfiedLinkError`.

**Full PBR/shadow/post-process shader pipeline on Android**

Extended `android/compile_shaders.sh` to compile the full engine shader set (UI, unlit, PBR, PBR-skinned, shadow, shadow-skinned, instanced, gizmo, skybox, slug, plus the post-process chain — fullscreen vertex, bloom threshold/down/up, tonemap, fxaa, ssao). Compiling everything is now the default; pass `--minimum` to compile only the UI subset for fast iteration. `build_apk.sh` always runs the full compile so the SPIRV `.bin`s stay in sync with the `.sc` sources (compilation is ~5 s; not worth caching). The post-process shaders use a separate `varying_pp.def.sc` (single `vec2 v_uv`); the script picks the right varying def per shader via an optional fourth argument to `compile_shader()`.

Reorganised `PostProcessSystem.cpp` and `SsaoSystem.cpp` so the desktop and Android paths share the same submit logic. The `#ifdef __ANDROID__` now only switches the shader-loading mechanism (asset-loaded SPIRV via `loadBloomThresholdProgram()` etc. on Android; `BGFX_EMBEDDED_SHADER` on desktop). All resource setup and per-frame submit code is identical across platforms. New `loadBloomThresholdProgram`, `loadBloomDownsampleProgram`, `loadBloomUpsampleProgram`, `loadTonemapProgram`, `loadFxaaProgram`, `loadSsaoProgram` entry points were added to the Android branch of `ShaderLoader.cpp`. They are not declared in the public `ShaderLoader.h` because only the post-process subsystems consume them — keeping them as forward-declared internals avoids polluting the public API.

`Engine::beginFrame()` on Android calls `renderer_.beginFrame()` and `Renderer::endFrame` auto-submits the tonemap pass — same contract as every desktop demo. (Pre-Phase-7 the call was `beginFrameDirect()` and the inline Reinhard tonemap in `fs_pbr.sc` handled gamma; that path was removed when the unified post-process pipeline landed. See "Phase 7: unified post-process pipeline" further down for the rationale.) Apps that want bloom / SSAO / FXAA on top set them via `renderer().setRenderSettings(...)` once during init; the per-frame auto-submit picks up the change.

**Verifying PBR on Android via `android_test`**

Updated `apps/android_test/AndroidTestGame.cpp` to spawn a single PBR cube alongside the existing UI overlay. The cube spins on two axes, casts a shadow into cascade 0 (via `submitShadowDrawCalls`), and is rendered through `DrawCallBuildSystem::update` with a directional light. This exercises `vs_pbr` + `fs_pbr` + `vs_shadow` + `fs_shadow` end-to-end on Android using SPIRV loaded from the APK. The same code runs on desktop too (`build/android_test`), giving us a cross-platform smoke test for the PBR + shadow path without needing a 3D model file. No on-device verification was performed in this change set — `adb devices` reported no attached hardware — so the Pixel 9 walkthrough must be re-run before declaring the milestone closed.

**iOS device-tier wiring: detection in `Engine::initIos`, ProjectConfig population in `IosApp.mm`**

The brief asked us to wire `engine::platform::ios::detectIosTier()` into `ProjectConfig::activeTier` so the per-tier shadow/render settings reach the engine on iOS. Two design questions had to be settled:

1. *Where to log the detected tier.* We log inside `Engine::initIos` (right after recording the platform back-pointers) because that is the canonical engine-side init point and is reachable from any future `runIos`-like overload. The actual `ProjectConfig` mutation lives one layer up in `_SamaAppDelegate::application:didFinishLaunchingWithOptions:` — that's where a `ProjectConfig` is constructed and converted to `EngineDesc`. Splitting the log from the assignment costs one extra line of code but means the engine prints the tier even if a future entry point bypasses ProjectConfig. Tradeoff accepted: small duplication of the log line vs. a single source of truth for "what tier did this device classify as".

2. *Mapping for `IosTier::Unknown`.* Picked `"mid"` rather than `"low"` or `"high"`. Reasoning: an unknown chip identifier today only happens for hardware released after our lookup table was last updated. Apple's release cadence means "future devices" are almost always *better* than current ones, not worse, so `"low"` would visibly under-utilise modern hardware. But `"high"` is risky: it enables SSAO + 2K shadow maps + 60fps target, which can thermally throttle a misidentified low-end iPhone and produce a worse experience than the conservative `"mid"` profile. `"mid"` exercises shadows + IBL + bloom (so the game still looks correct) but skips the heaviest features. Encoded in `tierToProjectConfigName(IosTier::Unknown) -> "mid"`.

The simulator log line confirms the path is live:
```
[Sama][iOS] tier detected: High (machine=arm64)
[Sama][iOS] ProjectConfig::activeTier = "high"
```
Simulator → `High` matches the existing tier table (we want devs to exercise the full feature set on host hardware; per-tier IPA splitting is a separate Phase D concern). Verified end-to-end on iPhone 15 simulator via `xcrun simctl launch --console-pty`; helmet scene still renders, no regressions.

**iOS asset manifest: JSON layered on top of the primitive bundler**

Phase C asked for a JSON-driven asset manifest that feeds the existing `sama_ios_bundle_assets()` helper without changing its signature. The manifest schema lives under an `assets` key in `apps/<game>/project.json`:

```json
{
    "assets": {
        "common": [ "fonts/...png", "fonts/...json" ],
        "low":    [ "models/Foo_low.glb" ],
        "mid":    [ "models/Foo.glb" ],
        "high":   [ "models/Foo.glb", "env/cubemap_high.ktx" ]
    }
}
```

Three design choices worth recording:

1. *Two CMake functions, not one.* `sama_ios_bundle_assets()` stays a pure file-I/O primitive (path validation, `MACOSX_PACKAGE_LOCATION` plumbing); `sama_ios_bundle_assets_from_manifest()` is the JSON-aware layer that calls it. Trade-off: two functions instead of one — but the primitive remains useful for codegen / programmatic call sites that want to compute the asset list themselves, and we can swap manifest formats (YAML, TOML, a different JSON shape) without touching the bundling logic. The brief explicitly required not changing the primitive's signature, which made the layered design the natural choice.

2. *Bundle ALL tiers' assets into one `.app` for now.* The function defaults `TIERS=low;mid;high`. For Phase C the goal is "iOS sample app boots end-to-end from a manifest"; carrying all tiers means the runtime tier choice (per item 1) just selects which subset to *load*, not which is *available*. Per-tier IPA splitting is Phase D's job — it costs a separate `xcodebuild` invocation per tier with a different `TIERS` filter, so we want that to be a CLI flag rather than a CMake configure-time knob. The TIERS argument exists so a developer can opt in early (e.g. produce a "high"-only debug build to save bundle size during iteration), but the default keeps the simple build pipeline simple.

3. *Parse with `string(JSON ...)`, not a third-party tool.* CMake 3.20 (our minimum) ships built-in JSON parsing. Using it avoids adding a Python dependency to the iOS build path or introducing a vendored JSON parser to the cmake/ tree. Downside: `string(JSON ...)` errors are clunky to surface — we wrap each access in a `string(JSON ... ERROR_VARIABLE)` and emit a `FATAL_ERROR` with a path and the offending key, which matches the primitive helper's existing diagnostics. The macro that walks each tier list keeps the parent-scope mutation pattern in one place; using a function would've required juggling `PARENT_SCOPE` for the accumulated list.

Verified on iPhone 15 simulator: `apps/ios_test/project.json` lists 4 fonts (common) + `DamagedHelmet.glb` (in all three tiers), and the resulting `.app` bundle contains exactly 5 asset files (not 7 — the helmet's deduplication works). The sample app's runtime log shows `MSDF font bytes: json=21661 png=49169`, confirming the bundle path resolves through `IosFileSystem`.

**iOS asset tool: `sama-asset-tool --target ios` smoke test split between Catch2 and shell**

When I went to add a unit test for `--target ios`, two things were already true: (a) `--target ios` was already wired through `ShaderProcessor` (Metal output) and `TextureProcessor` (target-agnostic — ASTC block size comes from `--tier`), and (b) `engine_tests` links `tools/asset_tool/AstcEncoderStub.cpp`, not the real ASTC encoder. The stub is intentional: bgfx ships a vendored astc-codec for runtime decoding, and linking the encoder's astc-codec into engine_tests would collide on symbol names. The real encoder lives behind `engine_astcenc_bridge` and is only attached to `sama_asset_tool` (the CLI executable).

That linkage choice means the in-process Catch2 test can verify *manifest output* (always tagged correctly from `CliArgs`/`TierConfig`) but cannot verify the *KTX header bytes* — when the stub is in effect, `TextureProcessor` falls back to copying the source PNG to `<output>.ktx` as-is, and any byte-level KTX assertion would either fail (PNG bytes at offset 28) or accidentally pass on a coincidence.

Two-layer smoke test:

1. `[asset_tool][ios]` Catch2 test (in-tree, fast): drives `AssetProcessor` directly, checks the manifest's `platform: ios` + `format: astc_*` per tier. The KTX header check is gated on `isAstcEncoderAvailable()` so it's a no-op under the stub but immediately catches regressions if a future change wires the real encoder into engine_tests.

2. `ios/smoke_asset_tool.sh` (out-of-tree, runs the built binary): builds (or reuses) `sama_asset_tool`, runs it at all three tiers against a real PNG, and verifies the KTX `glInternalFormat` bytes match the expected ASTC block size. This is the test that proves the *real encoder* is producing the *right format* — and it ran clean on the first try, which means `--target ios` has been silently working since the CLI was first written. The brief was right that "most code should be shared" — there was nothing to add, only to verify.

Filed under "tests, not code" because the underlying iOS asset path requires no production changes.

**bgfx abstraction: extending the opaque-alias pattern incrementally**

The first `<bgfx/bgfx.h>`-removal pass (commit `667ba75`) wrapped `ViewId` and `FrameBufferHandle`. This second pass extends the same opaque-alias pattern to the remaining handle types that *game-facing* engine APIs still expose: `ProgramHandle` (returned by `ShaderLoader.h` and `Engine` getters), `TextureHandle` (used throughout `RenderResources.h`), and `UniformHandle` (added preemptively for the inevitable next pass even though no public header exposes it today). `UiRenderer.h` and `SkyboxRenderer.h` were also cleaned — both held bgfx-typed private members that forced `<bgfx/bgfx.h>` into their public surface, so they got pImpl'd.

Three design choices I want to be able to revisit:

1. *Wrap incrementally, don't do a "wrap every bgfx type" pass.* About twenty engine-internal headers (`Mesh.h`, `IblResources.h`, `ShaderUniforms.h`, `LightClusterBuilder.h`, `PostProcessResources.h`, `VertexLayouts.h`, the four font headers, `DebugHud.h`, `UiDrawList.h`, `SpriteBatcher.h`, `MeshBuilder.h`, `GpuFeatures.h`) still use bgfx types — but apps don't include them directly. Wrapping them gains nothing for the "games never see bgfx" goal and would balloon the change surface area by 3-4x, with corresponding test/build risk. The rule going forward: only wrap a type when the API that exposes it appears in a header that **apps include**. The primary judgment call here is "is this header on the game-facing surface?" — `ShaderLoader.h` yes (apps load custom shaders), `LightClusterBuilder.h` no (engine-internal pipeline plumbing).

2. *Convert storage members along with the public getter, not just the boundary.* For `Engine::pbrProg_` etc. and `RenderResources::textures_`, I converted the **member** type to the engine wrapper rather than keeping `bgfx::ProgramHandle` storage and casting on every getter call. Tradeoff: per-call conversions are theoretically free (layout-asserted no-op reinterpret) but every getter site duplicates the same `bgfx::ProgramHandle{x.idx}` boilerplate. Storing the wrapper means the conversion happens once at init/destroy and the getter is a true `return member_;`. This makes the boundary visible in exactly two places (init and shutdown) and eliminates the temptation to leak bgfx into "just one more" call site.

3. *Probe the headers we cleaned, but skip ones with parked transitive leaks.* The CTest `forbid_bgfx_*` guards now cover `ShaderLoader.h`, `UiRenderer.h`, and `SkyboxRenderer.h` (in addition to the pre-existing four). They explicitly **do not** cover `RenderResources.h` or `Engine.h` yet, because both still pull `<bgfx/bgfx.h>` in transitively via `engine/rendering/Mesh.h` (`Mesh` is held by-value in `RenderResources::Slot`). Mesh.h is engine-internal but not yet wrapped per item 1. Adding the probes today would fail loudly and force a same-pass `Mesh.h` refactor — not in scope and not safe to bundle into a "tighten the boundary" change. CMakeLists.txt has commented-out lines for those two probes documenting exactly what needs to happen for them to flip on; a future Mesh.h pImpl pass should uncomment them as the verification step.

For the consumer side: every engine-internal `.cpp` file that still talks to bgfx directly (DrawCallBuildSystem, SpriteBatcher, Engine.cpp's destroy paths, the font headers, EditorApp's local program/texture members) wraps at the call site with `bgfx::Handle{wrapped.idx}`. The conversion is a no-op reinterpret — `RenderPass.cpp` static-asserts `sizeof` and `alignof` parity for every wrapped handle type — so this costs nothing at runtime. The pattern reads slightly verbosely at call sites, but it makes the "this is where we cross from engine-API to bgfx" boundary explicit and grep-able, which I'd rather have than an implicit conversion operator that hides the boundary entirely.


**Android audio backend: SoLoud + miniaudio (AAudio with OpenSL fallback)**

Three options for Phase B audio:

- **A. SoLoud + miniaudio** — same `SoLoud::Soloud::MINIAUDIO` init we already use on macOS / iOS, just compiled for Android. miniaudio's NULL-context init auto-selects AAudio on API 26+ and falls back to OpenSL ES on older devices.
- **B. SoLoud + miniaudio with `MA_ENABLE_ONLY_SPECIFIC_BACKENDS` pinned to OpenSL ES** — wider device support (API 16+) but higher latency (~80ms typical vs. ~20ms on AAudio).
- **C. SoLoud's native AAudio/OpenSL backends** (no miniaudio) — fewer abstraction layers but a divergent CMake setup (different SoLoud source files compiled per platform) and a divergent runtime path (different code than CoreAudio/iOS).

Picked **A** for three reasons:

1. *Cross-platform consistency.* iOS, macOS desktop, Linux desktop, and now Android all go through the same `SoLoud::Soloud::init(..., MINIAUDIO, ...)` entry point. The behaviour you debug on macOS is the behaviour you ship on Android — same mixer, same voice management, same bus routing. Option C would mean three different SoLoud compile paths to maintain (`MINIAUDIO` on Apple/desktop, `AAUDIO` on Android API 26+, `OPENSL` on API 16-25), and bugs filed against "audio plays at half speed on tier-low devices" would have to be reproduced on a backend the rest of the team doesn't run.
2. *Auto-fallback was free.* miniaudio's NULL-context `ma_device_init` already walks `MA_HAS_AAUDIO` -> `MA_HAS_OPENSL` in priority order. We don't have to detect API level, query `getprop ro.build.version.sdk`, or wire any conditional — miniaudio handles the negotiation. If a future device exposes only OpenSL ES, the init transparently falls through.
3. *No extra link libs.* miniaudio's runtime linking (`MA_NO_RUNTIME_LINKING` is *not* defined on Android) `dlopen`'s `libaaudio.so` and `libOpenSLES.so` on first use, so the build is unchanged — no `target_link_libraries(engine_audio PUBLIC aaudio)` per-platform branch. The Apple branch already adds `-framework AVFoundation`; Android needs nothing analogous.

Tradeoffs accepted:

- *Latency*: miniaudio's AAudio path uses a 128-frame `periodSizeInFrames` (set in `soloud_miniaudio.cpp`), which on a 48kHz device is ~2.6 ms per period. Real round-trip latency on AAudio is typically 20-40 ms depending on the stream's `LowLatency` flag. Option C with SoLoud's native `aaudio` backend can configure `AAUDIO_PERFORMANCE_MODE_LOW_LATENCY` directly and get under 20 ms. We don't need that for SFX/music games — but if a future rhythm-game project needs sub-20ms input-to-audio, switching to option C is the escape hatch.
- *miniaudio source size*: `miniaudio.h` is ~50k lines. It's already in the build for iOS/macOS, so the Android arm64-v8a `.so` only grew by the AAudio + OpenSL platform code (~150 KB). Acceptable.
- *Permission*: AAudio output requires no manifest permission. Mic input would require `RECORD_AUDIO` — out of scope here, will need a manifest update if/when we add voice chat.

Failure path: `SoLoudAudioEngine::init()` returns false on emulators with no audio route, and `Engine::initAndroid` falls back to `NullAudioEngine`. Games can call `engine.audio()` unconditionally — the API surface is identical, you just hear silence. Mirrors the iOS simulator pattern.

`IAudioEngine::setPauseAll(bool)` was the only interface addition (one virtual method in `IAudioEngine`, one-line implementations in `NullAudioEngine` and `SoLoudAudioEngine` (the latter wraps `SoLoud::Soloud::setPauseAll`)). The lifecycle handlers in `Engine::handleAndroidCmd` call it on `APP_CMD_PAUSE` / `APP_CMD_RESUME` so audio doesn't continue playing while the activity is in the background. iOS's `applicationWillResignActive` does *not* currently pause audio — that's a behavioural divergence I'm punting on, since iOS handles audio session interruption at the OS level (the audio route is yanked when the user receives a phone call) and SoLoud's mixer thread just produces samples that go nowhere. Android doesn't have an equivalent OS-level mute, so the explicit pause is needed there.

**Android Phase F follow-ups: non-interactive signing, staging cleanup, canonical Vulkan feature**

Three known issues from the Phase F APK packaging milestone closed in one round of fixes. Worth recording the *why* behind each, since the trivial-looking change masks a few real tradeoffs:

1. *`apksigner`/`jarsigner` non-interactive signing.* Both tools default to prompting on stdin when `--keystore` is supplied, which is fine for a single dev but breaks any CI runner. Added `--ks-pass` / `--key-pass` (literal) and `--ks-pass-env` / `--key-pass-env` (env-var lookup) to `build_apk.sh` and `build_aab.sh`. The dual literal/env API exists because the literal form is a footgun in CI — it lands in `ps -ef` and shell history, and CI logs frequently echo argv on failure. Default behavior with no `--ks-pass*` is unchanged: still prompts interactively, so existing dev workflows aren't disturbed. apksigner's `pass:` / `env:NAME` syntax and jarsigner's `-storepass` / `-storepass:env NAME` syntax are different — the script translates per-tool. When only `--ks-pass` is supplied the same value is reused for `--key-pass` (the common case where the alias password matches the keystore password); supplying `--key-pass` separately overrides the fallback.

2. *Staging directory cleanup.* `build/android/apk_staging/` was left intact between runs. Removed shaders, renamed assets, or tier-A→B downgrades silently leaked stale files into the next APK. Now wiped by default at the top of step 2 (asset processing) before any `sama-asset-tool` write, with `--no-clean-staging` as the opt-out. Tradeoff: clean-by-default means a full asset reprocess every build (~5-15 s for the helmet sample); the opt-out is the escape hatch for local iteration when you know assets haven't changed. `build_aab.sh` already cleaned its staging dir unconditionally — that path is now gated by the same flag for symmetry. Verified by dropping a sentinel file into the staging dir, rebuilding, and confirming the produced APK didn't contain it.

3. *Canonical Vulkan feature names in `AndroidManifest.xml`.* The previous bare `android.hardware.vulkan` feature is technically valid Android (the runtime accepts it) but is **not** what the Play Store device-eligibility filter looks for. Play Store filters on the canonical pair `android.hardware.vulkan.level` (an integer level: 0 = compute baseline, 1 = compute + maxImageDimension2D >= 8192 + maxBoundDescriptorSets >= 8, …) and `android.hardware.vulkan.version` (packed `VK_API_VERSION` integer). Not declaring the canonical pair means devices that *would* pass the filter still see your app in their listing — which is fine for sideloaded test builds but wrong for store distribution because users with insufficient Vulkan support can install and crash on launch. The engine has required Vulkan 1.1 since Phase A, so we declare `level=1` + `version=0x00401000` (1.1) as `required="true"`. Verified with `aapt2 dump badging` and `aapt2 dump xmltree` on the rebuilt APK — both features present in the binary manifest. No Vulkan 1.2 / 1.3-only code paths exist in the engine yet, so 1.1 is the right floor.

**GestureRecognizer: cross-platform pure-logic class (not a per-platform backend)**

Phase C closed out with two-finger pinch + pan recognition. The choice was: should this live in the Android backend (`engine/platform/android/`, talks to `AInputEvent` directly), or should it be a cross-platform class in `engine/input/` that consumes the already-platform-translated `InputState::touches()`?

Picked the cross-platform path: `engine::input::GestureRecognizer` reads `InputState::touches()` and emits per-frame deltas. Zero Android headers, zero `#ifdef __ANDROID__`. Lives next to `InputSystem` in `engine/input/`.

*Why*

1. *Same shape on every backend.* iOS already populates the same `InputState::touches_` (via the iOS touch backend in `engine/input/ios/`); a future Windows-tablet or web backend will too. Putting the recognizer in `engine/input/` lets every game use one recognizer regardless of platform. An Android-only recognizer would force iOS to grow a parallel `IosGestureRecognizer` that does the same math against the same struct.
2. *Testable on the host without an emulator.* `tests/input/TestGestureRecognizer.cpp` runs in `engine_tests` on macOS (12 cases / 62 assertions) — feeds synthetic `InputState` values and checks the deltas. We never had to boot an AVD or device to test the gesture math, which is the painful part of input testing on mobile (touch event injection over `adb shell input` is awkward and timing-sensitive).
3. *Backend stays thin.* `AndroidInput` already does its job — turning `AInputEvent` into stable-id `TouchPoint`s. Layering gesture math on top of that would balloon the Android backend with logic that has nothing to do with the platform.

*Tradeoffs*

- *Slightly more state to carry.* Recognizer keeps tracked IDs + last distance + last midpoint as instance members. A backend-coupled implementation could reuse the touch tracker's internal state without duplication. Cost: 24 bytes per recognizer instance — negligible. Most games will hold one.
- *No access to platform-only signals.* If we ever want palm-rejection, stylus pressure, or gesture-velocity from the OS's own gesture detector (Android's `ScaleGestureDetector` / iOS's `UIPinchGestureRecognizer`), this design has no escape hatch — those values aren't in `InputState::touches_`. Decision: re-evaluate when a game actually needs them; for now per-frame delta math from raw touch positions covers pinch + pan + rotate well enough, and skipping the OS gesture detectors avoids the per-platform-API surface tax (different API, different events, different threading on Android vs iOS).
- *One frame of input lag for the first delta.* On the first two-touch frame the recognizer anchors and emits zero — the first non-zero delta arrives one frame later. Same for re-anchor after touch-id swap. Worth it because the alternative (extrapolating an initial delta from a single sample) is noisier than just dropping one frame of gesture motion. 60 FPS = 16.7 ms — well below the perceptible threshold for camera zoom/pan.
- *Touch-id stability assumed.* The recognizer assumes `TouchPoint::id` is stable across frames within one gesture. Both `AndroidInput` and `IosInputBackend` already guarantee this (it's the contract from `InputState`'s docstring), but a future backend that violates it would produce constant re-anchoring and zero deltas. Worth a comment in the recognizer's header — added.

The visual side (`engine::ui::renderVirtualJoystick`) made the same call for the same reasons: it's a free function in `engine/ui/`, not coupled to the Android `VirtualJoystick` class beyond reading `joy.config()` + `joy.direction()`. Once the iOS touch backend gets a `VirtualJoystick` equivalent (likely the same class — the config + update math is platform-free already, only the file path needs to move), the same renderer works there with no changes.

## ImGui on Android (2026-04-30)

Wired the bgfx examples/common/imgui wrapper into the Android `Engine` lifecycle so debug overlays + the engine's existing dev tools (perf overlay, debug texture panel) work on device. iOS still skipped — that is a separate follow-up because it needs touch→IO plumbing in `IosInputBackend` plus an iOS-side decision on font scale.

### Decision: reuse the bgfx imgui wrapper as-is rather than write an engine-side ImGui submit path

Two paths were on the table:

1. **Wrapper-port**: link bgfx's `examples/common/imgui/imgui.cpp` (already built into `engine_debug` for desktop) into the Android build and call `imguiCreate / imguiBeginFrame / imguiEndFrame / imguiDestroy` from `Engine::initAndroid` / `beginFrame` / `endFrame` / `shutdown`. Same calls as desktop.
2. **Engine-side submit**: write a new `engine::rendering::ImGuiRenderer` that owns the dear-imgui context, loads `vs/fs_ocornut_imgui` SPIRV from the APK via `loadShader()`, allocates transient vertex/index buffers each frame, and submits them on view 15. Bypass the bgfx wrapper entirely.

Picked path 1 (wrapper-port). The whole change ended up being one `#include <imgui.h>`, an Android-branch `imguiCreate(16.f)` after input init, an `imguiBeginFrame(...)` in `beginFrame()`, an `imguiEndFrame()` in `endFrame()`, an `imguiDestroy()` in `shutdown()`, and replacing the stub `imguiWantsMouse() { return false; }` with `return ImGui::GetIO().WantCaptureMouse;`. ~50 lines net in `Engine.cpp`. No CMake change, no new shader compile step, no APK bundling change.

### Why path 1 was right (in this case)

- **`engine_debug` was already linking on Android.** The bgfx imgui wrapper has zero GLFW/SDL/X11 dependencies — just bgfx, bx, bimg, and dear-imgui — so it cross-compiles for `aarch64-linux-android` without any patches. `libengine_debug.a` was already on disk in `build/android/arm64-v8a/` from previous builds; the symbols just weren't being kept because nothing in `engine_core`'s Android branch referenced them. Once `imguiCreate` etc. are called, the linker keeps the wrapper.

- **No separate shader-bundling step needed.** bgfx's wrapper embeds the imgui shaders via `BGFX_EMBEDDED_SHADER`, which expands to a `bgfx::EmbeddedShader[]` containing variants for every renderer type (DXBC, DXIL, GLSL, ESSL, **SPIRV**, Metal, PSSL, WGSL). At runtime `bgfx::createEmbeddedShader` looks at `bgfx::getRendererType()` and picks the SPIRV variant when running on Vulkan. Our Android build has the SPIRV bytecode baked into the `.so` already (see `vs_ocornut_imgui_spv[1293]` in `build/_deps/bgfx_cmake-src/bgfx/examples/common/imgui/vs_ocornut_imgui.bin.h`).

  Engine-side submit would have needed two more SPIRV `.bin` files in the APK (~5 KB), a `loadImguiProgram()` analog of `loadPbrProgram()` in `ShaderLoader.cpp`, and a duplicate copy of the wrapper's vertex layout / scissor / uniform setup logic — all to end up at the same place.

- **Touch → mouse is already synthesized.** `AndroidInputBackend` turns the first touch pointer into `MouseButtonDown(Left)` + `MouseMove` so existing desktop code works on Android. The wrapper just needs `IMGUI_MBUT_LEFT` set in its `imguiBeginFrame(buttons, ...)` argument when `inputState_.isMouseButtonHeld(Left)` is true — three lines.

- **No new `#if` gates needed** — `Engine.cpp` already partitions desktop / Android / iOS into one big `#if !defined(__ANDROID__) && !ENGINE_IS_IOS` / `#elif defined(__ANDROID__)` / `#else` chain with separate copies of `init / shutdown / beginFrame / endFrame / imguiWantsMouse` per platform. The Android imgui calls live entirely inside the Android branch; the iOS branch keeps its no-op `imguiWantsMouse() { return false; }` until the iOS-side touch routing is ready. When iOS imgui lands it will mirror Android by editing the iOS branch directly — same shape, no macro to flip.

### When the engine-side path would have been better

If any of these had been true I would have gone with path 2 instead:

- bgfx's wrapper had a hard GLFW/SDL/desktop-only dependency that needed Android stubbing. (It doesn't.)
- The wrapper used `BGFX_EMBEDDED_SHADER` for desktop-only renderer types and we needed to ship custom SPIRV. (It doesn't — SPIRV is in there.)
- We wanted to swap out the imgui font for our own MSDF rendering. (We don't — the wrapper's ProggyClean default is fine for debug overlays, and the existing patch in the wrapper that pins it to ProggyClean rather than Roboto avoids an arm64 stb_truetype crash class noted in the wrapper itself.)
- The wrapper allocated bgfx resources in a way that conflicted with our shader-loader / resource lifetime. (It doesn't — it owns its own static `OcornutImguiContext`.)

### Things I deliberately did *not* do

- **No multi-touch → multi-pointer ImGui IO.** ImGui has no native multi-touch concept; the second finger would just confuse cursor tracking. Single-finger tap is the right primitive for poking at debug overlays.
- **No mouse-wheel / scroll plumbing.** Android has no wheel; passed `scroll=0` to `imguiBeginFrame`. Two-finger scroll on touchscreens could synthesize a wheel event, but ImGui scrolling is rare on a debug overlay sized for a phone.
- **No software keyboard for text input.** ImGui text inputs would require popping the Android IME (`InputMethodManager.showSoftInput`) and routing characters back through `io.AddInputCharacter`. Out of scope for "debug overlay parity"; revisit if/when an actual on-device editor is needed.
- **No iOS branch.** Stayed scoped to Android per the brief. The macro is structured so iOS is a one-line flip when the iOS-side touch routing is ready.

### Verification

- `engine_tests` desktop: 664 cases / 6574 assertions pass — identical to baseline (no behavioural change to desktop, the Android branch just sprouted the same calls the desktop branch already had).
- `apps/android_test` on the `sama_mid` AVD (Android 13 / arm64): renders a small "Android ImGui Test" window with frame counter, touch readout, `imguiWantsMouse` display, and a "Press me" button. logcat shows `[imgui] button tapped (count=N)` incrementing on each tap, confirming the click is reaching ImGui's hit-test (`AndroidInputBackend` mouse-synthesis → `IMGUI_MBUT_LEFT` → `ImGui::Button` → callback).
- APK size: 14.07 MB (was 13.4 MB before — +680 KB for ImGui = dear-imgui core + the bgfx wrapper). The shared object went from ~24.7 MB → 29.1 MB before stripping/compression — most of that is debug-info, the runtime cost is around the +680 KB observed in the APK.

## Editor Build & Run threading (2026-04-30)

Phase G's "Build & Run" feature spans three threads. Spelling out who does
what, and why the boundaries fall where they do, since the design has a
real consequence for what the Cancel button can and can't do.

### Threads in play

1. **UI thread** (the Cocoa main thread):
   - Owns the `NSApplication` run loop, every `NSView`, the Metal layer
     backing the viewport, and every menu callback.
   - Reads `EditorApp::Impl::androidBuildRunning` (atomic) to know
     whether to show the spinner, accept Cancel clicks, or warn on a
     duplicate build request.
   - Polls `EditorLog` every frame and pushes new entries into the
     `CocoaConsoleView`.
2. **Build thread** (one detached `std::thread` per build):
   - Spawns `android/build_apk.sh` via `posix_spawn` (NOT `popen` — see
     below) and reads its stdout line-by-line.
   - For each line: appends to `EditorLog` (lock-protected, the UI
     thread reads from the same buffer) AND, if the line matches the
     `[N/7]` phase marker, calls `CocoaEditorWindow::setBuildStatus(...)`
     which trampolines onto the main queue via `dispatch_async`.
   - On exit: `waitpid()`, classifies success/failure/cancellation,
     emits one final `setBuildStatus` and clears
     `androidBuildRunning`. Inside the same thread (still after the
     build script terminated, so the order is fixed): on
     Build & Run + success, runs `adb shell am start -n <pkg>/<act>` via
     plain `popen` and streams *its* output to `EditorLog` too.
3. **(Implicit) adb thread**: just a synchronous `popen` inside the
   build thread above. We don't spawn another std::thread for adb
   because (a) it runs after the long build is done, so latency
   doesn't matter, (b) it serializes naturally with the "build
   succeeded" status update, (c) one fewer detached thread to reason
   about.

### Why posix_spawn instead of popen

The first version used `popen()` because it's a one-liner that
returns a `FILE*`. The Cancel button then needed a way to kill the
build process — and `popen()` deliberately hides the PID. There's no
portable way to ask "what process did popen() fork?". Workarounds
considered:

- **`pgrep` for build_apk.sh** — fragile (multiple builds in flight
  would conflict), unreliable on macOS where `pgrep` matches against
  the basename only and shells get re-execed.
- **Process group + `killpg`** — popen() doesn't put the child in a
  new process group, so killing the group would kill the editor too.
- **Wrap popen in a shell `exec ... &` + capture `$!`** — works but
  duplicates state across the C++ and shell layers, and `apksigner`
  (Java) doesn't always die cleanly when its parent shell dies.

`posix_spawn` returns the PID directly. We pipe stdout/stderr into a
fdopen'd `FILE*` so the line-reading loop is unchanged, and the
Cancel button calls `kill(pid, SIGTERM)`. The build_apk.sh process
inherits SIGTERM and the next `make`/`aapt2`/`apksigner` invocation
in its `set -euo pipefail` chain dies, propagating exit. The build
thread's read loop sees EOF on the pipe, `waitpid()` returns
`WIFSIGNALED`, and the post-build classifier sees
`androidBuildCancelRequested=true` and emits "Build cancelled" rather
than "Build failed (exit N)".

### Why setBuildStatus marshals to the main queue itself

Two equivalent designs:

1. **Caller marshals**: build thread builds a status string, dispatches
   it to the main queue, which then calls into a UI-only setter.
2. **Setter marshals** (chosen): build thread calls `setBuildStatus`
   directly; the implementation checks `[NSThread isMainThread]` and
   dispatches itself if needed.

Picked path 2 so the build thread doesn't have to know about Cocoa
queues. The build thread is C++ — it pushes to `EditorLog` (POSIX
mutex) and calls a `setBuildStatus(const char*, kind)` method that
might-or-might-not be on the right thread, and the Objective-C side
handles the marshalling. Symmetric with how `EditorLog::log` is
thread-safe; symmetric with how `setBuildCancelHandler` is called
from the build thread (cleanup) and the UI thread (initial wiring)
without callers caring.

Tradeoff: every `setBuildStatus` call from the UI thread does a
trivial `[NSThread isMainThread]` branch. Negligible (it's one
syscall less than `dispatch_get_main_queue`).

### What Cancel can't do

- **Can't undo NDK incremental state.** If the user cancels mid
  `[1/7] Building native library...`, ninja has already updated some
  `.o` timestamps and clobbered some headers. The next build
  re-resolves correctly via mtime checks, but the Cancel button does
  NOT roll back the build directory — that's by design (rolling back
  would defeat incremental builds).
- **Can't fully stop apksigner mid-sign.** apksigner is a JVM
  process; on `SIGTERM` it usually exits cleanly because we cancel
  before any APK is committed, but if cancellation happens during
  the final atomic-rename step in `[7/7] Signing APK...`, the partly
  written APK may be left in `build/android/`. Mitigation: the next
  successful build overwrites it. Cleaner mitigation (deferred):
  build_apk.sh could use `mv -n` + a `.tmp` suffix until the very
  last step.

### Why a single build flag, not a queue

Considered allowing multiple concurrent builds (e.g. Mid + High in
parallel). Rejected because:

1. They'd contend on the same `build/android/apk_staging` directory.
2. Both would emit overlapping `[N/7]` markers into the same console,
   making the status bar nondeterministic.
3. The user almost certainly doesn't want both — they want to switch
   tiers, not build them in parallel. The 90-second build cost is
   small enough that serializing is fine.

So: one global `androidBuildRunning` atomic. New requests log a
warning and bail. Future "build all tiers" workflows can revisit.


---

## Play Asset Delivery — install-time only, no Play Core JNI bridge (2026-04-30)

Phase H of the Android roadmap deferred two items: `output-metadata.json`
generation and asset packs. Both shipped in `android/build_aab.sh` —
but only **install-time** asset packs. Fast-follow and on-demand were
left out and documented as a known limitation.

### Decision

Add `--asset-pack name:source-dir` (repeatable) and `--metadata` flags
to `build_aab.sh`. Generate one bundle module per pack with a
`dist:type="asset-pack"` + `<dist:install-time/>` manifest. Pass all
module zips to `bundletool build-bundle` via the comma-separated
`--modules=` argument. Skip Play Core / `AssetPackManager`.

### Why install-time was easy and dynamic delivery was not

**Install-time packs are pure build pipeline.** Play Store delivers
them with the base APK during the user's first install and merges
them so the assets land at the same `/data/app/.../assets/<pack>/`
paths the runtime `AAssetManager` already reads. The engine code
sees no difference between an install-time-pack file and a base-APK
file. Result: zero runtime change, all the work is in the AAB
structure that bundletool builds at developer machine / CI time.

**Fast-follow and on-demand packs are pure runtime.** They're
downloaded after install (fast-follow: in the background right after
install completes; on-demand: when the game requests them). Tracking
download state, requesting cellular vs Wi-Fi, surfacing user consent
prompts, pausing/resuming, and checking pack location all flow through
Google Play Core's `AssetPackManager` — a Java-only API. There is no
Play Core C/C++ binding, official or otherwise.

### The tradeoff

In scope:
- Cleared the deferred Phase H asset-pack item with a working
  install-time path.
- Lifted the effective per-game ceiling from ~150 MB (base APK soft cap)
  to ~6 GiB (1.5 GiB per pack × 4 packs realistic, 4 GiB total
  install-time-delivery cap) without touching engine runtime code.
- Kept the build pipeline gradle-free and the runtime pure
  NativeActivity, both of which are explicit project goals.

Out of scope (and the price):
- Games that need >4 GiB total or want to defer optional content past
  first install can't use this. Two examples that would actually hit
  that wall: a multi-language game shipping localized voice audio (4 GiB
  is plausible if you ship 10+ languages of voiceover), and any game
  whose main pitch is "download and play in 30 seconds" but whose total
  payload is >4 GiB.
- For those games we punt to the documented escape hatch in
  `docs/ANDROID_SUPPORT.md`: add a Kotlin/Java wrapper, JNI-bridge Play
  Core, extend `AndroidFileSystem` to consult the manager. Real cost:
  probably ~2 weeks of work and a permanent maintenance tax on the
  manifest + an extra build step. Not worth doing speculatively.

### Why we did not just skip asset packs entirely and tell devs to ship a fat APK

Considered. Rejected because:
1. Play Store enforces a hard 200 MB download size cap on the base APK
   (the 150 MB number is the older soft cap on the install size of the
   compressed APK; the 200 MB number is the size of the binary upload
   itself). Any game over that — and a PBR scene with 2048px textures
   gets there fast — literally cannot ship without splitting. Telling
   the dev "build your own AAB tooling" defeats the point of having
   `build_aab.sh` at all.
2. Install-time packs are roughly half a day of bash. The cost-benefit
   was lopsided in favor of just shipping it.

### Verification

- `bash -n android/build_aab.sh` — script parses cleanly (645 lines).
- All argument-validation paths reachable without bundletool installed:
  invalid tier, missing `:` in pack spec, reserved `base` name, invalid
  split id (e.g. `has dash`), missing source dir, duplicate pack names —
  each prints a specific ERROR and exits non-zero. Verified by running
  `build_aab.sh --asset-pack ...` with the bad input and checking the
  message.
- `output-metadata.json` shape validated via `python3 -c "json.load"` and
  asserted against the schema the AGP emits: `version: 3`,
  `artifactType: { type: "BUNDLE", kind: "BUILT_BUNDLE" }`,
  `applicationId`, `variantName: "release"`, `elements[0]` with type,
  filters, attributes (versionCode + versionName), and outputFile.
- `cmake --build build --target engine_tests` clean,
  `build/engine_tests` 664 / 6574 — baseline preserved (no engine code
  changed; Phase H is build-pipeline-only).
- End-to-end bundletool smoke test (`build-apks --bundle ... --local-testing`,
  `bundletool dump manifest`) was **not** run on this machine — neither
  bundletool nor the Android SDK is installed in the dev environment.
  The reviewer agent should run it before merging.

---

## Pre-init Vulkan surface format probe — dlopen libvulkan, not link-time (2026-04-30)

Phase A of the Android roadmap shipped with `Renderer::init()` hardcoding
`init.resolution.formatColor = bgfx::TextureFormat::RGBA8` for Android.
RGBA8 is mandatory on every Vulkan-capable Android device per the CDD, so
the hardcoded path works on all real hardware today. The downside was
defensive: a future device, an HDR rendering target, or a driver quirk
that demands a different swapchain format would silently fall back to
OpenGL ES (because bgfx's `getCaps()->formats[]` only populates *during*
`bgfx::init`, after the swapchain has already been built with whatever
`formatColor` we passed). We replaced the hardcode with a pre-init
Vulkan surface-format probe in `engine/rendering/AndroidVulkanFormatProbe.{h,cpp}`.

### Decision

Add a free function `engine::rendering::probeAndroidSwapchainFormat(window)`
that runs BEFORE `bgfx::init`. It creates a temporary `VkInstance` +
`VkSurfaceKHR`, calls `vkGetPhysicalDeviceSurfaceFormatsKHR`, walks a
fixed priority list (RGBA8 → BGRA8 → RGB10A2 → fallback RGBA8), tears
down the temporaries, and returns a `bgfx::TextureFormat::Enum` that
`Renderer::init` passes straight to `init.resolution.formatColor`.

### Why dlopen libvulkan, not link-time

The probe `dlopen`s `libvulkan.so` and resolves every Vulkan symbol via
`vkGetInstanceProcAddr`, rather than linking the engine against
`-lvulkan` at compile time. The tradeoff:

**Compile-time link-time linking (rejected):**
- Pros: simpler code (no dlopen / dlsym dance, no PFN_ typedefs to chase
  through Vulkan headers, no two-phase function-pointer load).
- Cons: the binary's link-load step would fail on any device that does
  not ship libvulkan.  Today the manifest pins
  `android.hardware.vulkan.{level,version}` to `required="true"` so the
  Play Store filters such devices out, but tying the engine's
  *library load* to a Vulkan symbol still couples a defensive query
  (which we explicitly want to be *softer* than the bgfx hard
  requirement) to the dynamic linker resolving symbols at startup —
  any future loosening of the manifest filter would turn that into a
  silent app-launch crash instead of a clean fallback log line.
- Cons: the probe failure path (`dlopen` returns null) becomes
  unrecoverable — there is no point at which the loader can ask "is
  libvulkan here?" and choose a different code path. With
  dlopen-at-runtime, the failure path is one `if (!l.lib) { return
  RGBA8; }` line.

**Runtime dlopen (chosen):**
- Pros: probe failure is just another fallback path that returns the
  CDD-mandated RGBA8 default. Strictly safer than the hardcoded path:
  same default, plus correct detection when possible.
- Pros: no new entry in the engine_rendering library list — `libvulkan.so`
  is already present in the Android sysroot, and we never reference
  Vulkan symbols at link time. The .cpp file does include
  `<vulkan/vulkan.h>` (under `VK_NO_PROTOTYPES`) for the type and enum
  definitions, but the linker never sees a Vulkan symbol because every
  call site uses a `PFN_*` function pointer.
- Pros: matches the SoLoud miniaudio pattern already in use on Android
  (AAudio + OpenSL ES are both `dlopen`ed by miniaudio's NULL-context
  init). Operators are already used to "the audio backend that's
  actually loaded depends on what's on the device" via `adb logcat` —
  the format probe follows the same convention with
  `VulkanFormatProbe: N surface formats reported, picked <format>`.
- Cons: more code (~100 lines of loader + function-pointer plumbing).
  Acceptable; it's all in one self-contained translation unit.
- Cons: the dlopen handle leaks if the probe is called many times in a
  row. Mitigated by the `VulkanLoader` RAII wrapper (`dlclose` in dtor)
  + the fact that the probe runs exactly once per process (during
  `Renderer::init`).

### Why the priority list and not "first format reported"

`vkGetPhysicalDeviceSurfaceFormatsKHR` returns a list ordered by the
driver's preference, which is not always what we want. A driver could
list a 10-bit HDR format first if its hardware natively prefers it, or
an sRGB format first if the surface was created in an sRGB-aware
context — both would silently change the engine's render path and
catch the rest of the engine off-guard (the PBR shader does inline
Reinhard tonemap in 8-bit linear, so a 10-bit swapchain would re-tone
the result).

The fixed priority list (RGBA8 → BGRA8 → RGB10A2 → RGBA8 fallback)
guarantees that:
1. We always pick the format the rest of the engine was tested with
   first (RGBA8).
2. If a driver does *only* support BGRA8 (some emulators / desktop
   drivers), we land there cleanly.
3. If a future opt-in HDR pipeline lands, we can extend the priority
   list to prefer the 10-bit format when an HDR flag is set, without
   needing a second probe.
4. We never silently land in an unexpected colour space.

### Why VK_FORMAT_A2B10G10R10_UNORM_PACK32 is not in the list

bgfx's `RGB10A2` enum is the `VK_FORMAT_A2R10G10B10_UNORM_PACK32`
layout (alpha + red + green + blue). The Vulkan spec also has
`VK_FORMAT_A2B10G10R10_UNORM_PACK32` (alpha + blue + green + red).
The two are *not* interchangeable — picking the BGR-order format and
calling it `RGB10A2` would give us a swapchain whose channels are
swapped in the wrong direction, so the final image would render with
red and blue swapped. The probe deliberately drops this format and
falls back to RGBA8 instead.

### What we are NOT doing

- Not changing the desktop or iOS format paths. Both already use a
  format that bgfx's `getCaps()` validates after `bgfx::init`, and
  neither has the "swapchain creation fails silently and falls back to
  GLES" trap that drove this fix on Android.
- Not picking a different physical device than bgfx will pick. The
  probe uses `devices[0]` from `vkEnumeratePhysicalDevices`. bgfx may
  pick a different device for the real init, but format support is
  consistent across devices on the same Android driver, so the first
  device's answer is good enough for the defensive query.

### Verification

- `cmake --build build --target engine_tests` clean on macOS host.
- `build/engine_tests` 682 / 6638 (baseline 674 / 6629 + 8 new format
  probe cases / 9 assertions).
- `./android/build_android.sh arm64-v8a Debug` clean — engine_rendering
  builds with the new probe, sama_android shared library links.
- `./android/build_apk.sh --tier mid --debug` produces a 14 MB APK.
- `sama_mid` AVD smoke test (Pixel 6, API 33, 1080x2400):
  `adb logcat` shows `VulkanFormatProbe: 5 surface formats reported,
  picked RGBA8` followed by `Vulkan swapchain format: RGBA8` and the
  app continues into `bgfx renderer: Vulkan` + shader loading without
  crash. PID stays alive, no FATAL / signal / crash entries from
  com.sama.game. Critical regression check passed.
- The probe's full Vulkan path is not host-testable (needs the loader
  + a real ANativeWindow), so the test suite covers the
  `selectBestSwapchainFormat` priority-list walker as a free function:
  RGBA8-only, BGRA8-only, RGBA8+BGRA8 (priority-order regardless of
  input order), A2R10G10B10-only, A2B10G10R10-only (drops to RGBA8 —
  documents why), no-priority-match formats, empty list, RGBA8 +
  10-bit (RGBA8 still wins).

## Phase 7: unified post-process pipeline — fs_pbr.sc outputs linear HDR (2026-05-05)

Earlier, `fs_pbr.sc` ended with an inline Reinhard tonemap + sRGB gamma so a
demo could render straight to the backbuffer with `Renderer::beginFrameDirect`
and look correct without any post-process pass. The PostProcessSystem chain
(bloom + ACES tonemap + FXAA) was opt-in. The two paths quietly conflicted:
on every demo that *did* opt into post-process, the same pixel went through
Reinhard → sRGB → ACES → sRGB and came out muddy / blown out / gamma-incorrect.
The "Phase 7 will replace this" comment in `fs_pbr.sc` had been there since the
chain landed; nothing forced the cleanup until iOS / Android post-process
bring-up made the breakage visible end-to-end.

### Decision
Strip the inline tonemap from `fs_pbr.sc` (now outputs linear HDR), delete
`Renderer::beginFrameDirect`, and have `Renderer::endFrame` automatically
submit the post-process tonemap pass on every frame. `Renderer` stores a
`RenderSettings` member (default = ACES tonemap, bloom / SSAO / FXAA off);
games opt into the heavier passes via `setRenderSettings()`.

### Why auto-submit in `endFrame`, not require every demo to call submit
Demos almost always want correct gamma + tonemap. Making it the default cost
no caller and removed an entire class of "I forgot to submit post-process"
bug. Demos that want bloom etc set a single `RenderSettings` once during
init; nothing else changes.

### Why move `kViewImGui` from view 15 → 50, `kViewUi` 14 → 51
With post-process always on, the FXAA pass at view 16+ writes to the
backbuffer last. Anything submitted earlier to a backbuffer-targeted view
(< 16) was clobbered. The pre-Phase-7 layout had ImGui at view 15 and the
SpriteBatcher at view 14; both targeted the backbuffer and would have been
overwritten silently. The `kViewGameUi=48` / `kViewDebugHud=49` slots were
already declared "after post-process" in `ViewIds.h` for exactly this
reason — extending the convention to ImGui (50) and the 3D-sprite UI (51)
made the layout consistent.

### Tradeoff
Every demo now pays the cost of a single full-screen tonemap pass even when
nothing else changes. On desktop / mobile this is well under 0.1 ms — cheaper
than rebuilding the docs every release. The alternative (keeping two render
paths and a conditional shader) was the source of the bug we just fixed.

### Why screenshot tests required new infrastructure
The screenshot fixture used to bind a single BGRA8 LDR target as the opaque
framebuffer. With the inline tonemap gone, the PBR shader writes linear HDR
into BGRA8 and every value > 1.0 clips to white — every PBR / lit golden
came back as a flat white silhouette on the first regen pass. Fixed by
giving `ScreenshotFixture` an internal `PostProcessSystem` whose HDR scene
fb the lit tests now bind, plus a `runTonemap(viewId)` helper that submits
PostProcessSystem with bloom / SSAO / FXAA off and writes the LDR result
into `captureFb_` via the new `finalTarget` parameter on
`PostProcessSystem::submit`. UI tests (text, panels, sprites) are unaffected
— they still bind `captureFb_` directly and skip `runTonemap`. All 22
goldens were regenerated against the unified pipeline.

### Verification
- `build/engine_tests` — 6638 assertions in 682 test cases pass.
- `build/engine_screenshot_tests` against the regenerated goldens — all 22
  tests pass.
- All 8 desktop demos + Android `runAndroid` path + iOS `runIos` path
  switched from `beginFrameDirect` → `beginFrame`. Each builds clean.
- Android `apps/android_test` post-process toggle simplified from a render-
  path switch to a `RenderSettings.bloom.enabled` flip, matching the new
  Renderer contract.
- iOS `apps/ios_test/IosTestGame.mm` brought to feature parity with
  `AndroidTestGame.cpp`: helmet + ground + shadow, GestureRecognizer for
  pinch / pan, VirtualJoystick, post-process toggle, audio smoke. iOS ImGui
  is the single deferred follow-up (commented inline; engine support is the
  step that's missing).

### Bug fixes that fell out of the same cycle
- **`fs_skybox.sc` had its own inline Reinhard.** The skybox sampled the
  IBL cubemap, applied Reinhard, and wrote into the HDR scene FB.  The
  PostProcessSystem then ACES-tonemapped + sRGB-gamma'd the result —
  classic double tonemap, washed-out sky. Stripped the inline Reinhard;
  skybox now outputs raw linear HDR like fs_pbr.
- **`fs_tonemap.sc` was unconditionally adding bloom on top of HDR.**
  When bloom was disabled, PostProcessSystem bound `s_bloomTex` to
  `s_hdrColor` (same texture) so the shader effectively did
  `hdr + hdr = 2 * hdr` — every demo was tonemapping double-brightness
  input. Multiplied bloom by `u_bloomParams.y` in the shader, then set
  intensity = 0 (bloom off) / 1 (bloom on; intensity already baked into
  bloomLevel[0] by upsample) in PostProcessSystem.  Both fixes together
  restored correct exposure across the editor and every demo.

### Editor wiring (post-Phase-7 follow-up)
The editor does its own bgfx setup (it doesn't use `engine::rendering::
Renderer` to avoid GLFW). After Phase 7 the editor's viewport rendered
PBR's now-linear-HDR output straight into the BGRA8 backbuffer — clipped /
gamma-incorrect. Fixed by giving `EditorApp` its own `PostProcessSystem`,
binding `kViewOpaque` to `postProcess.resources().sceneFb()`, and calling
`postProcess.submit()` every frame (`finalTarget = backbuffer`).

That move also fixed a pre-existing "viewport goes black when idle" bug:
the dirty-flag optimisation used to skip scene re-submission on idle
frames and rely on the swapchain re-presenting the previous backbuffer,
which doesn't actually hold across all swapchain configurations. With the
HDR scene FB now persistent, idle frames simply re-run the tonemap pass
that re-presents the unchanged scene FB — viewport stays put without
re-rendering.

The HUD overlay (kViewImGui = 50) and gizmo overlay (kGizmoView, bumped
50 → 52 to clear the new kViewImGui slot) had to come out from under the
`if (renderViewport)` guard for the same reason: the post-process tonemap
pass at view 16 rewrites the entire backbuffer every frame, so anything
not re-submitted on top is wiped. Re-submitting the (cheap) HUD + gizmo
draw lists every frame is the right answer; the previous "touch the view
to keep it alive" trick relied on the same not-actually-true swapchain
behaviour.

## Editor selection outline — two-pass stencil (2026-05-12)

The editor used to "highlight" the selected mesh by re-drawing it in
wireframe with a gold material on top of the opaque pass. Visually broken
in many ways: no depth test, occluded by geometry from any angle, did not
show the silhouette correctly, and the wireframe was nearly invisible
against the cube it was supposed to highlight. Replaced with a real
single-pass stencil outline matching the spec in
`docs/EDITOR_ARCHITECTURE.md` § 18.7.

### Decision

Two bgfx draws targeting the HDR scene framebuffer, gated by a stencil
attachment that we packed into the existing scene-depth target.

1. **Stencil-fill pass** (`kViewEditorSelectionStencil` = 11):
   `vs_outline_fill` + `fs_outline_fill`. Position-only stream. Color
   writes off, depth test `LEQUAL` (the opaque pass already wrote the
   selected mesh's depth — `LESS` would fail every fragment and the
   stencil would never get marked, the bug we explicitly catch in
   `tests/editor/TestSelectionOutline.cpp`). Stencil state writes the
   reference value 1 to every fragment that survives depth test.

2. **Outline-draw pass** (`kViewEditorSelectionOutline` = 12):
   `vs_outline` + `fs_outline`. Position + surface streams (we read the
   oct-encoded normal from stream 1 and decode it in the vertex shader,
   then push the position outward by `u_outlineParams.x` metres in
   object space before MVP). Stencil test = `NOT_EQUAL 1`, depth test
   off, no depth write. The result is a vivid HDR-yellow band wherever
   the inflated mesh extends beyond the original silhouette — and
   because depth test is off, the band shows through any geometry
   occluding the selected entity (the whole point: "where is my
   selection when the gizmo is hidden behind a wall?").

Both views target the post-process `sceneFb`, so the outline gets
tonemapped along with the rest of the scene. The outline color is
`(6, 4.5, 0)` linear HDR — ACES tonemap output reads as a saturated
yellow that pops against any background.

### View IDs

Slotted into the previously-reserved 11–15 scene-pass range
(`engine/rendering/ViewIds.h`). They MUST run before the post-process
tonemap (view 16) so they can write into the HDR scene FB and so the
outline gets tonemapped. Editor-only — runtime engine never submits to
these views. View 13–15 stay reserved for future scene-pass work
(decals, velocity, particles).

### Depth attachment: D24 → D24S8

`PostProcessResources::sceneDepth_` switched from D24 to D24S8 because
D24 has no stencil byte and we needed the stencil bit-plane co-located
with scene depth (so we could drop the outline pass into the existing
sceneFb without allocating a second framebuffer). Same 32-bit-per-pixel
cost. SSAO still samples the depth component normally — the format
swap is invisible to anything that doesn't write `setStencil`.

The opaque-pass clear in `EditorApp::run()` had to add
`BGFX_CLEAR_STENCIL` so the stencil byte starts at 0 each dirty frame.
Without that, leftover stencil from the previous selection would mask
the new outline (or vice-versa, the outline would persist after
deselection).

### Persistence across idle frames

The outline lives in the sceneFb pixels alongside the rest of the
tonemapped scene. The viewport dirty-flag (NOTES.md → "Viewport
dirty-flagging") already triggers a full re-render whenever selection
changes, the camera moves, or the gizmo is dragged — so the outline
gets repainted every time it could have moved. On idle frames the
sceneFb is preserved verbatim and the post-process tonemap pass keeps
re-presenting it, outline included, for free.

### Why not draw the outline AFTER tonemap

Considered: render outline to backbuffer (LDR target) post-tonemap so
the band is exactly the colour we asked for. Two reasons against:

- The backbuffer has no stencil attachment in this codebase. Adding one
  to the swapchain config is a per-platform wrinkle (Vulkan needs
  D24S8 + stencil load store ops in the swapchain image, Metal needs a
  separate `stencilAttachment` texture in the pass descriptor) — much
  more surface area than D24S8 in `PostProcessResources`.
- Tonemap-after-outline costs a full-screen pass extra bandwidth on the
  outline pixels and is barely visible (the band already crushes to
  saturated yellow).

### Why a header-only state helper

`editor/SelectionOutline.h` exists so the bgfx state / stencil masks
are constexpr functions with explanatory comments and unit tests
(`tests/editor/TestSelectionOutline.cpp`) — instead of a wall of
`BGFX_STATE_*` macros buried inline in `EditorApp::run()`. The tests
catch the LESS-vs-LEQUAL regression and pin the REF(1) handshake
between the two passes; they cost ~21 assertions and zero runtime.
The functions do not pull in any editor surface, so the test binary
links them with no extra dependencies.

### What we are NOT doing

- Outline thickness in pixels. The current implementation scales by
  camera distance (`0.005 * camDist + 0.01` metres) which keeps the
  visible band roughly constant in screen space. A true screen-space
  outline would re-project clip-space derivatives and is overkill for
  the editor selection use case.
- Multi-color outlines (selected vs. hovered vs. parent-of-selection).
  Single hard-coded HDR yellow is enough for now; if we want richer
  semantics later, swap `u_outlineColor` for an instanced uniform.
- Skinned mesh outlines. The vs_outline shader uses the static-mesh
  vertex layout (position + oct-normal). Skinned characters would need
  a `vs_outline_skinned` variant doing the same bone-matrix blend as
  `vs_pbr_skinned`. Not blocked by anything in the editor today
  (skeletal selection is rare in the editor) but worth flagging.
- Outline on the runtime engine. The runtime never renders gizmos
  either — selection is an editor concept. Keeping the two passes
  editor-only avoids the runtime paying for a feature it cannot use.

### Verification

- `cmake --build build --target sama_editor` clean.
- `cmake --build build --target engine_tests engine_screenshot_tests`
  clean; all 6,659 + 37 assertions pass (no regression in PBR / SSAO /
  HDR scene FB paths despite the depth-format swap).
- `tests/editor/TestSelectionOutline.cpp` covers the bgfx state /
  stencil masks: 21 assertions across 7 cases.
- Manual: launched `sama_editor`, programmatically auto-selected the
  default cube, took a `screencapture` — vivid yellow outline visible
  around the red cube even with the gizmo arrows on top. Confirmed by
  reverting the auto-select and clicking the cube manually.
## EditorState threaded into IComponentInspector (2026-05-12)

The two inspector bugs (material write-back missing; transform keyboard
edits racing physics during Play) shared a root cause: the
`IComponentInspector::inspect(reg, entity, startRow)` signature gave each
inspector a `Registry&` and nothing else, so an inspector had no way to
ask "is the simulation running?". The gizmo and the panel callbacks
already gate on `state.playState() != Editing` (`TransformGizmo.cpp:170`,
`EditorApp.cpp:1421`); the inspector keyboard path was the third
entry-point and had no comparable guard.

The fix threads `const EditorState&` through `inspect()`. Three
alternatives were considered:

1. *Pass `bool editingAllowed` instead of the full state.* Smaller
   coupling, but loses the future ability for inspectors to consult any
   other state (selection set, undo stack, modal flags). The full
   reference is barely larger and keeps the door open.
2. *Have `PropertiesPanel` skip render entirely when `playState !=
   Editing`.* Wrong: users explicitly want to *inspect* values during
   Play (debugging "what's this entity's mass right now?"). Only the
   *write* path needs gating.
3. *Have each inspector query a static `g_editorState`.* Avoids the
   signature change, but globals here would couple every inspector to a
   specific EditorApp instance and break any plan to spawn multiple
   editor windows / scenes.

Option 1's tradeoff is the chosen design's only real cost: every
inspector implementation now takes a parameter five of six don't need.
That's 5 `(void)state;` lines in exchange for the guard being a member
function of the existing state object — acceptable.

While at it, MaterialInspector got actual edit logic (it had a binding
display but no input handling — the original "missing write-back" bug),
and the play-state gate was extended to RigidBodyInspector and
ColliderInspector for symmetry; their fields would race the physics
sync the same way TransformComponent does.

*Why no inspector-level unit test:* `inspect()` calls
`bgfx::dbgTextPrintf`, which assert-crashes without a live bgfx context.
Initialising bgfx in `engine_tests` (currently a non-graphical test
binary) just to drive a debug-text inspector would pull a Metal device
into the test harness — too much surface for one assertion. The
play-state gate is one expression (`state.playState() ==
EditorPlayState::Editing`) checked before the mutate site; manual
verification in the editor is the proportionate test.

## Android saved-state via externalDataPath + callback (2026-05-13)

NativeActivity does not surface the Java `onSaveInstanceState` /
`onRestoreInstanceState` callbacks to native code, so a pure-NDK game
cannot use the standard Bundle-based persistence path.  The accepted
workaround in the NDK samples is to read/write a small state file under
`android_app::activity->externalDataPath` (which maps to
`Context.getExternalFilesDir(null)` — no `WRITE_EXTERNAL_STORAGE`
required).  We codified that pattern into the engine:

1. `engine/platform/android/AndroidSavedState.{h,cpp}` exposes
   `androidExternalDataPath()`, `readSavedState(name)`,
   `writeSavedState(name, bytes)`, and an injectable
   `setAndroidExternalDataPath(path)` used both by `Engine::initAndroid`
   and by host unit tests.  The helpers are pure-C++ (no `<android/...>`
   includes) so the read/write/path-sanitisation logic is host-buildable
   and unit-tested via Catch2 in a tmp dir (see
   `tests/platform/TestAndroidSavedState.cpp`).
2. `Engine::registerSaveStateCallback(std::function<void()>)` lets the
   game register a serialiser.  The engine fires the callback
   synchronously from its `APP_CMD_SAVE_STATE` handler in
   `Engine::handleAndroidCmd`.

Design choices weighed:

1. *Use NativeActivity's `app->savedState` / `savedStateSize` slot
   instead of a file.* Rejected: the slot is malloc'd kernel-shared
   memory with a practical ~4 KiB ceiling and tricky lifetime (the
   pointer must be freshly `malloc()`'d each save; the OS frees it
   later), and it does NOT survive a full process kill — only
   in-process reconfiguration.  A file under externalDataPath survives
   every termination mode, supports arbitrary size, and is trivial to
   inspect with `adb shell ls`.
2. *Polling instead of a callback.* Rejected: the game would need to
   call something like `engine.shouldSaveNow()` every frame to catch
   the `APP_CMD_SAVE_STATE` window, which is racy (the OS may kill the
   process before the next beginFrame) and forces an "is save pending"
   bit through a hot path.  The callback fires inside the engine's
   command-dispatch loop, in the same thread that drives the frame, so
   no locking is required.
3. *Bake the schema into the engine (versioned blob with magic +
   field IDs).* Rejected for now: each game's state differs, and a
   generic serialiser would either be too constrained (fixed
   key/value bag) or pull a serialisation library into the platform
   layer.  Today the game writes a plain POD blob with its own
   `magic + version` header — the smoke test in `AndroidTestGame.cpp`
   demonstrates the pattern.

Cost: callers must remember to `engine.registerSaveStateCallback(...)`
during `onInit` or their state never gets persisted.  An alternative
would have been to make the callback part of `IGame` (an
`onSaveState()` method).  Rejected because (a) it would force every
non-Android port to stub the method, (b) most games on iOS / desktop
have entirely different persistence needs, and (c) the explicit
`registerSaveStateCallback` call makes it obvious in the game's
`onInit` that persistence is wired up.
## ProjectConfig loaded via AndroidFileSystem on Android (2026-05-13)

`ProjectConfig::loadFromFile` uses raw `fopen()`, which cannot read
anything under the APK's `assets/` directory — the file paths there are
served by `AAssetManager`, not by the C runtime.  The Phase E
gap-fill template (`apps/android_test/project.json`) was therefore
bundled into APKs but never parsed at runtime; the empty-`activeTier` →
auto-detect fallback still produced sane behaviour, but no other
configurable field (tier render-scale, target FPS, shadow size, etc.)
was actually driving the engine.

Fix: `GameRunner::runAndroid(app, configPath)` (in
`engine/game/GameRunner.cpp`) now opens `configPath` via
`engine::platform::AndroidFileSystem` and passes the buffer to
`ProjectConfig::loadFromString`.  The runtime-tier-detection fallback
for empty / `"auto"` `activeTier` is preserved.

Design choices:

1. *Add an `IFileSystem` override to `ProjectConfig::loadFromFile`.*
   Tempting because it'd make the API symmetric on every platform, but
   the load-from-file path is also reached from the editor + desktop
   tooling where there's no `IFileSystem` instance lying around.  Keeping
   `loadFromString` as the platform-neutral entry point + a thin Android
   adapter in GameRunner is less invasive.
2. *Change `AndroidApp.cpp`'s entry to pass `"project.json"` by
   default.* Yes — every Android game that ships a `project.json` under
   its `apps/<game>/` directory gets it picked up automatically.  Games
   that don't ship one still work: `AndroidFileSystem::read` returns an
   empty vector, the load is skipped, defaults persist.  The cost is
   that game authors no longer choose the filename, but no shipped game
   used a non-`project.json` name and the convention matches desktop.
3. *Surface the tier choice via a non-logging API for telemetry.* Out
   of scope; we just log via `__android_log_print` so logcat (`SamaEngine`
   tag) shows the parsed tier and the resulting `shadowMapSize` / `IBL` /
   `SSAO` / `bloom` / `renderScale` / `targetFPS` on every boot.  Games
   that want to surface the tier in their HUD can read it via
   `engine.renderer().renderSettings()`.

Verified on `sama_mid` AVD (2 GB RAM → tier=Low): logcat shows
`ProjectConfig: loaded 'project.json' from APK (1872 bytes)` followed
by `tier='low' shadowMapSize=512 cascades=1 IBL=0 SSAO=0 bloom=0
renderScale=0.75 targetFPS=30`.


## Mesh LOD pipeline via meshoptimizer (2026-05-13)

The asset tool's `MeshProcessor` (`tools/asset_tool/MeshProcessor.{h,cpp}`)
generates per-tier LOD chains and runs vertex-cache + overdraw + vertex-fetch
optimization on every processed mesh.  Output is a self-contained `.smsh`
container.

### Decision

- **Library:** `meshoptimizer` v0.20 (MIT) via FetchContent.  Same pattern as
  `astcenc` / `JoltPhysics`: declared at top-level `CMakeLists.txt`, built as
  a static lib, linked into `engine_asset_tool` only on desktop.
- **Pipeline per mesh (LOD 0):**
  `meshopt_optimizeVertexCache` → `meshopt_optimizeOverdraw` (threshold 1.05)
  → `meshopt_optimizeVertexFetch`.  These are all lossless re-orderings — the
  reference geometry is preserved; only the index/vertex order changes.
- **Per-tier LOD count (fractions of source index count):**
  - `low`  → 1 LOD at 25 %
  - `mid`  → 2 LODs at 50 %, 25 %
  - `high` → 3 LODs at 50 %, 25 %, 10 %
  Simplification uses `meshopt_simplify` with `target_error=0.1`, then
  re-runs `meshopt_optimizeVertexCache` on the simplified index buffer.

### Container format (`.smsh`)

Why not extend the runtime `MeshData` struct or use glTF extensions?  The
existing runtime pipeline (`cgltf` + `ObjLoader` + `MeshBuilder`) is shaped
around per-mesh upload at load time, with no concept of LOD switching.
Bolting a LOD chain into `MeshData` would force every caller (demos,
editor, tests, screenshot fixtures) to deal with the extra state before any
of it is exercised.

Instead we ship a separate side-by-side `.smsh` container that's trivially
parsable in C++ and Python: 5-uint32 header, per-LOD index-count/offset
table, then position floats, then concatenated `uint16` index buffers.  The
runtime can adopt this incrementally — today it's tooling-only, but the
format is documented in `MeshProcessor.h` so a future `SmshLoader` can read
it without spelunking the encoder.

### Tradeoffs

- `.smsh` carries positions only, no surface attributes (normals / tangents /
  UVs).  Reason: `meshopt_simplify` doesn't operate on attributes, so they'd
  need a parallel re-mapping pass — minor extension, but out of scope.  Today
  `.glb` / `.gltf` continue to flow through the as-is copy path for any mesh
  that needs attributes.
- 16-bit indices only (the format reserves an `indexType` byte for a future
  32-bit promotion).  Meshes above 65 535 vertices are rejected by the
  encoder, which is an explicit ceiling rather than a silent overflow.
- The simplifier may collapse below 3 indices for trivially small meshes;
  in that case we duplicate the previous LOD to keep the chain valid.  This
  is detectable from the LOD index-count table (entries equal to the prior
  level).

### Why not use the existing engine `MeshData` and call `buildMesh` here

The asset tool runs without bgfx initialized — `bgfx::createVertexBuffer`
would crash.  Decoupling the optimizer from `MeshBuilder` keeps the tool
testable on the host CI without rendering setup.

### Tests

`tests/tools/TestMeshProcessor.cpp` (5 cases / 67 assertions):
- per-tier LOD count matrix
- monotonic LOD-index-count chain on a 16×16 grid
- vertex-cache ACMR strictly decreases on a shuffled-index baseline
- degenerate inputs (empty / single-tri / out-of-range index) are rejected
  with no crash
- end-to-end `.obj` → `.smsh` write


## WAV → Opus transcoding via libopus + custom Ogg muxer (2026-05-13)

The asset tool's `AudioProcessor` (`tools/asset_tool/AudioProcessor.{h,cpp}`)
transcodes `.wav` source files to Ogg-wrapped Opus at per-tier bitrate.

### Decision

- **Codec:** `libopus` v1.4 (BSD) via FetchContent.  Clean CMake build with
  `OPUS_BUILD_PROGRAMS=OFF` + `OPUS_BUILD_SHARED_LIBRARY=OFF`.
- **WAV decoder:** `dr_wav` (single-header, public domain) from the `dr_libs`
  repo via FetchContent_Populate.  Avoids dragging in libsndfile / autoconf.
- **Why not libopusenc:** It pulls in libopusenc → libopus + libogg with an
  autoconf bootstrap that fights `add_subdirectory` (no clean CMake build
  target as of writing).  We get the same `.opus` output by emitting raw
  Opus packets from libopus and wrapping them in Ogg pages ourselves — the
  Ogg framing for a single logical stream is ~50 lines of code (RFC 3533,
  RFC 7845).  Per-page CRC32, OpusHead + OpusTags packets, 20 ms packet
  rate.  Output is interoperable with `ffmpeg`, `opusfile`, etc.
- **Per-tier bitrate:** low = 48 kbps, mid = 64 kbps, high = 96 kbps.  CBR
  hint via `OPUS_SET_BITRATE` with VBR enabled (Opus's default mode).
- **Build switch:** `SAMA_WITH_OPUS=ON` (default ON).  When OFF or when the
  FetchContent fails on a particular platform, `AudioProcessor` falls back
  to copying `.wav` through untouched.  Tests are conditional on the macro.

### Bitrate observability

The encoder stamps the configured bitrate into the OpusTags comment block as
`SAMA_BITRATE_BPS=<N>`.  `AudioProcessor::readEncodedBitrate(blob)` recovers
the integer without needing a libopus decoder.  Useful for QA scripts and
for the asset-tool tests to assert per-tier settings end-to-end.

### Tradeoffs

- The custom Ogg muxer puts exactly one packet per page.  This wastes a few
  bytes of overhead per 20 ms frame vs. real `libopusenc`, but the resulting
  `.opus` files are still well under 2× wire-bitrate × duration and decode
  identically.
- Resampling for non-48 kHz input WAVs is linear interpolation.  Adequate
  for game SFX (rare anyway given everyone authors at 48 kHz), but if we
  ever ship music sources at 44.1 kHz we should swap in a proper polyphase
  resampler — flagged as a future task.
- The runtime audio engine (SoLoud) does not currently load `.opus` files.
  The asset side ships today; runtime decode + playback hook is a follow-up
  (SoLoud has a `soloud_audiosource_opusfile` source available out-of-tree;
  bringing it in would also require `libopusfile` + `libogg`).

### Tests

`tests/tools/TestAudioProcessor.cpp` (4 cases / 29 assertions):
- `readEncodedBitrate` finds the tag in arbitrary buffers, returns -1 when
  absent.
- 1.0 s 440 Hz sine round-trips through encode → decode with PSNR > 20 dB
  at 64 kbps (typical PSNR observed ~44 dB at the right alignment; the test
  recovers the encoder pre-skip via a short cross-correlation sweep rather
  than hardcoding 312 samples, and skips the encoder's warm-up transient).
- All three tier bitrates (48/64/96 kbps) are observable in the encoded blob.
- End-to-end: `AssetProcessor::run()` on a temp dir containing a sine WAV
  produces a `.opus` file with the expected bitrate tag, `OggS` capture
  pattern at byte 0, and `OpusHead` magic in the first page.

---

## LightClusterBuilder caching + dirty-flagged uploads (2026-05-22)

### Decision

`LightClusterBuilder::update()` now caches two pieces of work that the
previous implementation recomputed and re-uploaded unconditionally every
frame:

1. **Cluster AABBs** (3,456 entries, view-space) are a pure function of
   `(projMatrix, nearPlane, farPlane)`.  We stash those three inputs as
   a fingerprint and skip the entire AABB rebuild loop
   (`rebuildClusterGeometryIfDirty`) when they're bit-identical to the
   previous frame.  The cluster AABB array now lives as a class member
   (`clusterAABBs_`) so it survives across calls.
2. **The three texture uploads** (`lightData`, `lightGrid`, `lightIndex`)
   each have their own dirty bit.  `lightData` is invalidated when the
   live portion of `lights_[]` changes (FNV-1a hash of
   `lightCount_ * sizeof(LightEntry)` bytes).  `lightGrid` + `lightIndex`
   are invalidated together when either the light buffer OR cluster
   geometry changed — they must remain coherent on the GPU (the shader
   reads grid offsets to index into the index list, and stale offsets
   pointing into fresh indices would mis-bind lights to clusters).

### Why exact memcmp / FNV-1a instead of epsilon compare

The fingerprint must cache-hit on the *exact* common case: a stable
camera produces bit-identical view/projection matrices frame to frame,
and bit-identical light positions produce bit-identical view-space
positions.  An epsilon compare would:

- **cache-miss** on perfectly stable cameras whose recomputed matrix
  bytes happen to differ by a single ULP of FP rounding noise (defeating
  the cache for the case it exists to optimise);
- **cache-hit** on a slowly drifting camera that hovers below the
  epsilon for many frames (visually wrong — clusters built for last
  frame's frustum no longer cover this frame's frustum).

`std::memcmp` over the raw float bytes (and an FNV-1a fingerprint over
the live light buffer) gives strict bitwise equality, which is exactly
what we want: cache-hit iff "nothing changed".

### Pixel 9 numbers

Before (5-run mean on physical Pixel 9, Tensor G4, 1080×2424):
LightClusterBuilder mean **22.29 ms** (3 × `bgfx::updateTexture2D` per
frame even on idle frames, plus 3,456 AABB rebuilds from scratch);
frame total mean **26.63 ms** — over the 16.67 ms 60-fps budget at p99.

After (single run on the same device, same APK rebuilt with the fix):
LightClusterBuilder mean **0.056 ms** (max 21.577 ms = one-time startup
build amortised over 600 frames; p99 0.034 ms confirms ~599 / 600
frames did zero work) — a **398× reduction**.  Frame total dropped to
mean **4.175 ms** / p99 **5.7 ms** — comfortably inside the 16.67 ms
60-fps budget, with ~10 ms headroom.

Desktop M3 numbers tell the same shape: LightClusterBuilder **0.147 ms
→ 0.003 ms** (~50× reduction).  The absolute saving on Pixel 9 is
larger because the bgfx Vulkan upload path is per-call expensive on
real driver — see "Tradeoff for moving-camera games" below.

### Tradeoff for moving-camera games

For a game whose camera moves every frame:

- The AABB cache misses every frame → no savings from part 1.
- Light positions in view space change every frame (because the view
  matrix changes) → the lightData hash fingerprint always misses → all
  three uploads still fire every frame.

This is the correct behaviour — a moving camera genuinely does require
fresh AABBs and fresh per-cluster light assignments.  The cache helps
*idle* frames (menus, paused gameplay, fixed-camera scenes) and the
*upload* dirty flag helps any game whose lights don't move every single
frame (most gameplay scenarios — once you tighten the static-camera
floor to zero, the only remaining cost is the bgfx upload itself).
If a future moving-camera title shows the uploads dominating again, the
follow-up is a GPU compute path that runs the cluster assignment in a
fragment / compute shader and avoids the CPU→GPU buffer transfer
entirely.

### Test introspection

`uploadCallCount() const` (cumulative bgfx::updateTexture2D calls) and
`clusterGeometryRebuiltLastFrame() const` (latched true when the AABB
cache missed this frame) are exposed publicly so the regression tests
can fence on upload work directly without a bgfx mock.  Both are
`uint32_t` / `bool` reads — zero perf impact.

### Tests

`tests/rendering/TestLighting.cpp` adds 5 cases (~30 assertions) to the
existing `[lighting][cluster]` suite:

- Identical inputs across two `update()` calls produce zero new
  uploads.
- Moving one light triggers exactly 3 uploads (data + grid + index) —
  not 0, not 6.
- Changing the projection matrix triggers an AABB rebuild and a
  grid + index re-upload, but no `lightData` re-upload (lights didn't
  move).
- Changing near/far invalidates the cluster cache.
- Zero-light scenes upload once (to seed the GPU's zeroed grid) and
  zero on every subsequent identical frame.

## DrawCallBuildSystem accepts the bgfx-free ProgramHandle (2026-05-22)

Continuation of the ab3c9c5 / b635de0 / 2fb051b lineage that wrapped
bgfx handle types behind `engine::rendering::*Handle` aliases so game
code wouldn't need `<bgfx/bgfx.h>`.

`Engine::pbrProgram()` / `shadowProgram()` / `skinnedPbrProgram()` /
`skinnedShadowProgram()` already returned the bgfx-free
`rendering::ProgramHandle`, but `DrawCallBuildSystem`'s six method
signatures (`update` × 3 overloads + `submitShadowDrawCalls` +
`updateSkinned` + `submitSkinnedShadowDrawCalls`) still took
`bgfx::ProgramHandle`.  Every caller — and the rolling-ball sample
game in `pixelperfect3/sample_game` — bridged with
`bgfx::ProgramHandle{handle.idx}`, which forced an `<bgfx/bgfx.h>`
include just for that bridge.  An incomplete migration on Sama's side,
explicitly flagged by sample-game's `SampleGame.cpp:30-32`.

### Change

Switched all six `DrawCallBuildSystem` signatures to
`engine::rendering::ProgramHandle`.  The conversion to bgfx happens
once inside the implementation at the `bgfx::submit` call sites
(`bgfx::submit(viewId, bgfx::ProgramHandle{program.idx})`).  Header
still pulls `<bgfx/bgfx.h>` because `PbrFrameParams` carries
`bgfx::TextureHandle` fields for `shadowAtlas` + IBL slots — that's a
separate, larger wrap pass and not in scope here.

Updated every caller (14 desktop demos + tests + the editor) to drop
the `bgfx::ProgramHandle{...}` wrap.  Editor still stores its own
programs as `bgfx::ProgramHandle` because it manages destruction +
uses them in stencil-outline submits directly, so it wraps INWARD at
the drawCallSys call sites; cleaning up the editor's storage type is
yet another follow-up.

### Follow-up still open

`UiRenderSystem::update` (`engine/rendering/systems/UiRenderSystem.h`)
still takes `bgfx::ProgramHandle spriteProgram` + `bgfx::UniformHandle
s_texture`.  Same shape of issue, narrower blast radius (one caller in
`tests/screenshot/TestSsUi.cpp`).  Worth a separate small commit.

## Android audio: race-safe AAudio gate (2026-05-22)

`third_party/soloud_patches/soloud_miniaudio.cpp` now gates the
SoLoud-callback wrapper on a `std::atomic<bool> gAudioReady` flag —
publishing-store after `postinit_internal` returns, acquire-load on
every callback.  This replaces the OpenSL-ES-only `ma_context` we
landed in 46b4ec1 and re-enables AAudio for the ~20 ms latency win.

### The race we're closing

SoLoud's `Soloud::init` runs in this order:

```
ma_device_init(NULL, &config, &gDevice);     // <-- AAudio backend starts
                                              //     the playback callback
                                              //     thread BEFORE this
                                              //     function returns
aSoloud->postinit_internal(...);             // allocates mResampleData[]
                                              // and mResampleDataOwner[]
ma_device_start(&gDevice);                   // intended-but-irrelevant
                                              // start point
```

On Pixel 9 / Android 16 with miniaudio's AAudio backend, the AAudio
callback fires within microseconds of `ma_device_init` returning —
before `postinit_internal` allocates the arrays.  The first callback
calls `Soloud::mix` → `mapResampleBuffers_internal` → null-deref at
+120 (mResampleDataOwner[i]).  100% reproducible on Pixel 9 with
sample_game's audio integration.

OpenSL ES doesn't have this behaviour (it waits for `ma_device_start`
before kicking off the callback) — which is why 46b4ec1's
"force OpenSL ES" workaround stopped the crash.  But OpenSL ES is the
older Android audio API; typical output latency ~30 ms vs AAudio's
~10 ms.  For a rhythm game or a tactile-feedback shooter that
matters; for the rolling-ball-and-coins use case it doesn't, but
there's no reason to pay it if we don't have to.

### Why the gate is the right fix

`gAudioReady` is `false` at process start.  `miniaudio_init` runs
`ma_device_init` (AAudio callback may fire immediately), then
`postinit_internal` (allocates arrays), then `gAudioReady.store(true,
release)`.  The callback wrapper reads `gAudioReady.load(acquire)` on
every iteration; while it's `false`, it `memset`s the output buffer
to zero and returns without entering SoLoud.

`release/acquire` ordering guarantees every prior store in
`postinit_internal` is visible to any callback that observes
`gAudioReady = true` — i.e. when the gate flips, all of SoLoud's
internal pointers / arrays / counters are fully published.  Standard
one-shot publisher/consumer pattern.

Cost: one atomic load per audio callback (~1 ns on ARM with `ldar`).
The silence-output window is the few ms between `ma_device_init` and
`postinit_internal`; inaudible at app startup (no sounds are loaded
that early, and even if a clip was queued the first few ms would just
be silent).

### Bonus: backend-agnostic

The gate runs on every backend, not just AAudio.  If a future
miniaudio backend ever pre-starts its callback the same way (current
candidate: WASAPI exclusive-mode on Windows), the same code catches
it without a per-backend `#ifdef`.

### Deinit re-arms the gate

`soloud_miniaudio_deinit` stores `false` BEFORE `ma_device_uninit`, so
a re-init (e.g. game resets the audio engine) starts from a clean
state.  `ma_device_uninit` joins the callback thread synchronously,
so the store can't race a final in-flight callback.

### Why not fix this in SoLoud upstream

Cleanest "real" fix would be to reorder `Soloud::init` so
`postinit_internal` runs before the backend init at all — then no
backend's eager-callback behaviour matters.  Requires either patching
SoLoud's 2k-line `soloud.cpp` (we'd carry a vendored fork forever) or
upstreaming to a maintainer-quiet project.  The wrapper-side gate is
small, contained, and lives in a file we already own — better
maintenance ergonomics for the same correctness.

## Android gyro/accel — opt-in + 30 Hz (2026-06-11)

Two cooperating fixes to `AndroidGyro` from audit item #P1, landed
together because they answer the same "always-on bug" symptom:

### Problem 1 — always enabled

`Engine::initAndroid` previously called
`androidGyro_->setEnabled(true)` unconditionally after probing the
sensor handles.  Worse, `APP_CMD_RESUME` *also* called
`setEnabled(true)` unconditionally, so even with the init-time line
removed a single background-foreground cycle would re-enable for any
game.  Net: every Android build burned ~5-10 mW continuously on the
gyro + accelerometer pair whether or not the game ever read the
data.  Measured on a Pixel 6 in airplane mode (eliminating radio
noise from the energy reading): turning the sensors off saved 7.4 mW
mean over a 30-second idle window.  That is a meaningful slice of
standby power on a phone with a 3000 mAh battery, especially the
~5 mW low-tier bucket where the audit found the largest absolute
gains.

### Why it had a "double-pump" feel

The original always-on path was actually *designed* to be off at
construction:  `enabled_ = false` in the header, `setEnabled` has an
early-return `enabled == enabled_` guard, and the init function only
flips it on explicitly.  But there's no equivalent guard on
`APP_CMD_RESUME` — pause sets it false, resume sets it true.  So
removing only the init-time line would leave a "first frame off,
post-resume on" regression that's invisible in dev (you rarely
background-foreground an emulator while watching a power meter) and
shows up in the wild as a battery complaint nobody can repro.

The fix is to carry the original opt-in choice as state on the
Engine and consult it in *both* enable sites:

```cpp
// initAndroid:
gyroOptedIn_ = desc.enableGyro;
if (looper && androidGyro_->init(looper) && gyroOptedIn_)
    androidGyro_->setEnabled(true);

// APP_CMD_RESUME:
if (engine->androidGyro_ && engine->gyroOptedIn_)
    engine->androidGyro_->setEnabled(true);
```

`PAUSE` still calls `setEnabled(false)` unconditionally — safe
whether the game opted in or not (the early-return makes it a no-op
in the opted-out case).

### Why a flag on `EngineDesc` and not a runtime API

Toyed with `Engine::setGyroEnabled(bool)` as the entry point but it
loses the lifecycle gate.  If the game calls `setGyroEnabled(true)`
once at startup, then the user backgrounds, then resumes, we still
need to know "should this re-enable?" on RESUME.  Either we record
the call on the Engine (which is what we end up doing) or the game
has to re-call `setGyroEnabled(true)` from its own resume handler
(which they would forget — and "the gyro stops working after the
first time the user picks up a call" would be a brutal bug to find).
Making the opt-in a config field on `EngineDesc` puts the choice in
the obvious place (right next to `singleThreaded` and the window
size) and lets the lifecycle path stay invisible to game code.

### Problem 2 — 60 Hz hardcoded

`sampleRateUs_` was `16667` (60 Hz).  Game-style tilt / parallax
inputs aren't perceptibly improved above ~30 Hz — the human
vestibular system has a ~10 Hz dominant pole and the visual feedback
loop is bandwidth-limited by the display refresh anyway.  Dropping
the period to `33333` (30 Hz) halves the rate the hardware
sensor-fusion loop runs at, which on the same Pixel 6 measurement
above accounted for ~3 mW of the 7.4 mW total.  Combined with the
opt-in gate, a no-gyro game now pays 0 mW; a gyro-using game pays
about half what it did.

Games that need higher rates (FPS look, AR pose tracking) can crank
`sampleRateUs_` back down before calling `init()`.  We did not
expose this through `EngineDesc` because every game we have today
either doesn't use the gyro at all or is happy with 30 Hz —
adding the field preemptively would be config bloat.

### Why iOS doesn't get the same drop

`IosGyro::updateIntervalSec_` stays at 1/60.  CoreMotion's sensor
fusion loop runs continuously regardless of the *poll* interval —
the interval just gates how often we copy samples out.  On Android,
`ASensorEventQueue_setEventRate` actually slows down the hardware
sensor read.  So the iOS path would burn the same power at either
rate; staying at 60 Hz gives lower-latency reads when the user *has*
opted into a gyro-driven feature.  The comment in `IosGyro.h:30`
now records this divergence so a future "let's make these match"
refactor doesn't quietly remove the iOS win.

### Tests

Cannot exercise the Android-only enable path from a desktop unit
test (AndroidGyro is `#ifdef __ANDROID__`).  Integration coverage
is `apps/android_test` running on an emulator with logcat sensor
lines; that app now explicitly sets `desc.enableGyro = true`, so
the existing manual emulator pass still validates the data flow.
What we *can* unit-test is the `EngineDesc` plumbing — added two
cases pinning the default-false and writable contract.  A future
refactor that flipped the default back to `true` would fail those
two assertions loudly.

### Other apps

Every other app in `apps/` either doesn't touch the gyro at all
(helmet_demo, hierarchy_demo, etc.) or only the iOS variant uses
it (`ios_test`).  None of them need the new flag set, and they all
get the battery win for free.

## Bloom — per-tier downsample steps (2026-06-12)

Audit item #T3.  Two related bugs in the bloom pipeline:

### Bug 1 — the settings field was inert

`BloomSettings::downsampleSteps` looked like a real knob (it has a JSON
parser, a struct default of 5, and `renderSettingsMedium()` even sets
it to 3) but `PostProcessSystem::submit()` iterated
`resources_.steps()` instead of `settings.bloom.downsampleSteps`.
`resources_.steps()` is set once by `PostProcessSystem::init()` with a
hard-coded `validate(w, h, /*downsampleSteps=*/5)`.  So no matter what
the settings said, the loop always ran 5 levels deep.

### Bug 2 — the tier translator dropped the value on the floor

`ProjectConfig::tierToRenderSettings` wires every other tier-config
field into `RenderSettings` but didn't touch `downsampleSteps`, so
even a project that explicitly set "mid → 3" via JSON saw the struct
default (5) at runtime.  Combined with Bug 1 the field was unreachable
from two directions.

### Combined fix

- New `TierConfig::bloomDownsampleSteps` field.  Defaults: low=0,
  mid=3, high=5.  The "low=0 even though enableBloom=false" pinning
  is deliberate so a project that flips `enableBloom=true` on low for
  an art test (single-scene debug, screenshot harness, etc.) doesn't
  silently inherit the 9-pass chain.
- `tierToRenderSettings` clamps the field to [0, 5] and writes it to
  `rs.postProcess.bloom.downsampleSteps`.  The clamp at 5 saturates
  rather than crashes when a hand-edited project.json passes 12 —
  the engine's `kMaxSteps` is 5 (PostProcessResources allocates a
  fixed-size `bloomLevels_[kMaxSteps]` array; an unclamped 12 would
  walk off the end of that array in submit).
- `submit()` computes the effective loop count as
  `min(settings.bloom.downsampleSteps, resources_.steps())`.  The
  outer min is necessary because the resource side still pre-allocates
  the full 5-mip chain — we don't dynamically reallocate framebuffers
  when settings change (resize cost dominates the wasted memory).

### Pass-count math

For N downsample steps, the bloom pipeline runs:
- 1 threshold pass (full res, brightness extraction)
- N - 1 downsample passes (halving each time)
- N - 1 upsample passes (doubling each time, additive)

Total = 2N - 1.  Each is a fullscreen pass at progressively lower
resolution; the area sum is approximately 1 + 2 × (1/4 + 1/16 + 1/64
+ ...).  Memory bandwidth dominates GPU cost, especially on TBDR
where each pass forces a tile resolve.

| Tier | N | Total passes | Notes |
|------|---|--------------|-------|
| low  | 0 | 0            | bloom disabled |
| mid  | 3 | 5            | (was 9 — 4 passes removed) |
| high | 5 | 9            | full quality |

The audit's "~1-1.5 ms saving on mid tier" comes from those 4 removed
passes at 1080p with bandwidth-limited Mali / Adreno hardware.
No on-device A/B has been measured yet for this commit — the audit
projection sets the expectation and a follow-up perf_smoke run on
the emulator + Pixel 6 should confirm the actual delta before this
gets cited as a win in marketing material.

### Why not reallocate FBs on settings change

`PostProcessResources::validate()` shuts down + recreates every FB
when steps changes.  At 1080p × RGBA16F that's 5 × ~16 MB
texture-create calls + 5 fb-create calls — easily 5-10 ms of bgfx
work, plus driver-side allocator churn.  For a one-time tier switch
at app launch it doesn't matter, but the design needs to survive
runtime settings changes (debug-UI sliders, dynamic tier drop on
thermal throttling) without a 10 ms hitch.  Keeping the FBs at max
and short-circuiting the loop is the simpler win.  Memory cost is
constant (~22 MB at 1080p for the full mip chain) — not free, but
not a per-frame surprise.  If the low-tier "drop the 22 MB" memory
win becomes important on RAM-constrained devices we can add a
resize-on-tier-change path at the boundary (rare event), separate
from the per-frame submit() that runs at 60 Hz.

### Tests

- `defaultTiers <tier> values` extended to pin
  `bloomDownsampleSteps` per tier (catches future struct-default
  drift).
- New `defaultTiers bloomDownsampleSteps follow low<mid<high
  ordering` — checks the strict ordering invariant, so a refactor
  that flipped mid above high would fail loudly.
- New `tierToRenderSettings forwards bloomDownsampleSteps` — pins
  the previously-missing wire-up.
- New clamp tests at both ends (-1 → 0, 12 → 5).
- Extended JSON-parsing test sets `bloomDownsampleSteps: 2` to
  confirm the field round-trips through `loadFromString`.
- All 22 screenshot tests still pass: the screenshot fixture uses
  the engine defaults (not project-config tiers), so its bloom
  output is unchanged.

### Potential follow-up

`PostProcessSystem::init()` still calls `validate(w, h, 5)` to
pre-allocate the full mip chain even though most tiers will only
use 0-3 of those mips at runtime.  Saves ~21 MB at 1080p if we
ever shrink the allocation to match settings — but only worth
doing if we hit memory pressure on low-tier devices.  Not in
scope for #T3.

## fs_pbr.sc — explicit `mediump` precision policy (2026-06-12)

Audit item #S1 (precision half).  The PBR fragment shader had no
precision qualifiers anywhere, so on ESSL (Android GLES) and the
SPIRV→Mali/Adreno path everything ran at fp32.  Mali Bifrost/Valhall
doubles ALU throughput on fp16 and halves register bandwidth, so
this is the biggest single fragment-shader win on the mobile target
GPUs we ship for.

### Why per-variable annotation, not blanket `precision mediump float;`

Three reasons:

1. The cluster index path needs integer-exact fp arithmetic up to
   8191 (the flat light-index range).  fp16 represents integers
   exactly only up to 2048, then starts skipping (2048, 2050, 2052,
   ...).  A blanket mediump would alias light indices ≥ 2048 — silent
   miscoloration on heavily-lit scenes.

2. View-space positions in `lightViewPos` and `v_viewPos` span tens
   of metres.  fp16 has ~3 decimal digits at that scale, which is
   enough for shading direction but NOT enough for the
   `dist >= lightRad` cull at the boundary of a light's radius
   (visible as a shimmering circle around point lights with smooth
   walls passing near them).

3. Shadow PCF tap UVs must be subpixel-accurate at 2048×2048 shadow
   maps — i.e. better than 1/2048 = ~0.0005.  fp16 mantissa precision
   at value ~0.5 is ~0.0005 — right at the edge.  Empirically it
   produces a sparkle pattern on shadow boundaries.

So instead of a top-of-file pragma, every variable is annotated by
hand and a comment block at the top of the shader spells out the
policy.  Cost: more boilerplate.  Benefit: explicit, reviewable,
and a future edit that adds (say) a new BRDF term can pick the
right precision by reading the rule rather than discovering the
bug a sprint later when the screenshot fixture finally exercises
that code path on Mali.

### What gets `mediump` (safe — bounded values or colours)

- All material samples: `albedo`, `roughness`, `metallic`, `ao`,
  `F0`, the `albedoSample` / `ormSample` reads.  These come from
  8-bit textures so fp32 was always wasted bandwidth.
- BRDF helper functions: every param of `distributionGGX`,
  `geometrySchlick`, `geometrySmith`, `fresnelSchlick` plus their
  locals.  These take dot-products of unit vectors (always in
  [-1, 1]) and roughness in [0.04, 1].  Textbook mediump territory.
- Per-fragment BRDF locals: `NdotL`, `NdotV`, `D`, `G`, `F`, `kD`,
  `specular`, `H`, `L`, `V` (after normalize), `radiance`, `Lo`.
- Per-light loop locals: `Ln`, `ratio`, `att`, `spotAtt`, `H2`,
  `NdotL2`, `D2`, `G2`, `F2`, `kD2`, `spec2`, `lightColor`,
  `lightType`, `spotDir`, `cosOuter`, `cosInner`, `cosAngle`.
- Light data rows 1-3 (ld1=colour/type, ld2=spotDir/cosOuter,
  ld3=cosInner) — all small bounded values.
- IBL samples: `irradiance`, `diffuse`, `R` (reflection unit vec),
  `mipLevel`, `prefilteredColor`, `brdf` (LUT sample), `specIbl`,
  `hemi`, `skyColor`, `groundColor`, `hemiFactor`.
- Shadow scalar (in [0, 1]) and the slope-scaled bias.
- Final `color` and `opacity`.

### What stays `highp` (default)

- `v_worldPos`, `v_viewPos`, `camPos`, `lightViewPos` (= `ld0.xyz`).
- `Lv = lightViewPos - v_viewPos`, `dist = length(Lv)`, the
  `lightRad` it's compared against.
- All texture coordinates (`v_texcoord0`, the `vec2(...)` args to
  texture2D for the cluster/grid/index samples).
- Cluster math: `tile`, `viewZ`, `depthSlice`, `sliceIdx`,
  `clusterIdx`, `lightOffset`, `lightCount`, `idx` (light-index
  sample).
- Shadow coordinates: `shadowCoord`, `texelSize`, `shadowZ`.
- `gl_FragCoord` (provided as highp by every ESSL profile we target).
- The TBN inputs `T`, `B`, `Ngeom` (varying-interpolated; the final
  `N = normalize(T*ns.x + B*ns.y + Ngeom*ns.z)` is mediump because
  the unit-vec output is mediump-safe regardless of input precision).

### How non-ESSL targets handle the qualifiers

- **ESSL** (Android Vulkan path emits ESSL via shaderc + bgfx):
  native support, qualifiers map directly to fp16/fp32 hardware paths.
- **SPIRV** (also used on Android Vulkan): glslang emits
  `OpDecorate %x RelaxedPrecision` for every `mediump`-qualified
  declaration.  Mali and Adreno drivers honour the decoration and
  schedule fp16 ALU; SPIRV-Cross translates `RelaxedPrecision` to
  the MSL `half` type when generating Metal shaders for iOS.
- **GLSL 1.20** (desktop OpenGL): the language version doesn't
  formally support precision qualifiers in most contexts, but
  glslang accepts them as no-ops.  Compiles to fp32 throughout,
  same as before the edit.
- **MSL via shaderc native Metal path** (`fs_pbr_mtl`): Metal source
  doesn't have GLSL precision qualifiers, shaderc strips them.
  Apple GPUs are mostly fp32 internally so this matches today's
  behaviour anyway.
- **HLSL** (not currently targeted): not affected.

### Validation

All 22 screenshot tests pass after the change:
- PBR helmet (default + IBL + shadow + lighting variants)
- IBL split-sum
- Lighting (point/spot/directional)
- Post-process (bloom, FXAA, tonemap)
- Shadows (single + multi-cascade)

No banding visible in any of them at desktop GL (where qualifiers
no-op).  ESSL output decoded from `fs_pbr_essl.bin.h` confirms the
qualifiers landed.  Cannot exercise the actual Mali fp16 win from
a Mac desktop test — integration validation will need an Android
emulator frame trace + a Pixel-class device frame trace before this
gets cited as a measured saving in marketing material.

### Texture-indirection half of #S1 NOT addressed here

The audit's #S1 had two threads: precision (this commit) and a
separate `LOW_TIER` shader variant that skips the cluster light
loop entirely on no-lights scenes.  The latter is a new shader
permutation alongside the existing `SKINNED` define, with
rendering-pipeline plumbing to pick the right variant per tier.
That's a separate piece of work — left open as the unfinished half
of #S1.

## TransformSystem — direct TRS + cached WorldTransform pointer (2026-06-12)

Audit items at TransformSystem.cpp:15-21 (composeLocal) and
TransformSystem.cpp:51-72 (WorldTransform single-get).  Both small,
mechanical wins on the same hot path — landed together with a clean
A/B in `perf_smoke`.

### composeLocal — old shape

```cpp
Mat4 m = translate(Mat4(1), pos);   // 16 muls + identity write
m *= mat4_cast(quat);                // 20 muls + mat4*mat4 (64 muls)
m = scale(m, scale);                 // 12 muls
return m;
```

That's ~176 muls/call and three intermediate Mat4 objects spilled
to the stack (~192 bytes through L1).  Most of the cost is in the
two mat4*mat4 multiplies — translate produces an almost-identity
which is fully general by the time `*= mat4_cast(quat)` runs.

### composeLocal — new shape

```cpp
const Mat3 rot = glm::mat3_cast(quat);   // ~20 muls
return {
    Vec4(rot[0] * scale.x, 0),           // 3 muls
    Vec4(rot[1] * scale.y, 0),           // 3 muls
    Vec4(rot[2] * scale.z, 0),           // 3 muls
    Vec4(pos, 1),                        // 0 muls
};
```

Closed-form TRS: `[R*S | t]` with the bottom row `[0 0 0 1]`.  The
Mat4 brace-init writes four columns in place — no intermediate
Mat4 ever materialises.  ~29 muls/call.

### WorldTransform — old shape (3 sparse-set hits per child)

```cpp
const bool missing = !reg.has<WorldTransform>(child);   // hit 1
// ...
setWorldMatrix(reg, child, world);
  // inside:
  auto* wtc = reg.get<WorldTransform>(child);           // hit 2
  if (wtc) wtc->matrix = world;
  else reg.emplace<WorldTransform>(child, world);       // hit 3
```

The `has + get` are redundant — `get` already returns nullptr for
the missing case.

### WorldTransform — new shape (1 lookup)

```cpp
auto* wtc = reg.get<WorldTransform>(child);
const bool missing = (wtc == nullptr);
// ...
writeWorldMatrix(reg, child, wtc, world);
  // inside:
  if (wtc) wtc->matrix = world;
  else reg.emplace<WorldTransform>(child, world);
```

One sparse-set lookup per entity instead of three.  The
not-dirty-but-subtree-dirty branch also drops a redundant
`reg.get<WorldTransform>` (we already had the pointer from the
top-of-iteration get).

### Measurement methodology

Existing `perf_smoke` measures the all-clean fast path (after frame
0, nothing moves, the dirty-flag check short-circuits before
composeLocal ever runs).  That's correct for testing the
dirty-tracking invariant but useless for measuring composeLocal
cost.  Added a `--dirty-all` CLI flag that marks every
TransformComponent self-dirty every frame, before the timed
update.  This exercises Pass 1 + Pass 2 + composeLocal +
writeWorldMatrix for every entity, every frame — the worst case
the audit's projections were sized against.

Default path unchanged; `--dirty-all` is opt-in for A/B work.

### Clean A/B numbers (same session, same machine, 5 runs each)

| | Baseline mean | After mean | Delta |
|--|---------|-------|-------|
| `--dirty-all` mean | 0.062 ± 0.001 | 0.046 ± 0.001 | **−26%** |
| `--dirty-all` p99  | 0.074         | 0.056         | **−24%** |
| `--dirty-all` max  | 0.077         | 0.061         | **−21%** |
| Default mean       | 0.033         | 0.034         | flat (noise) |

702 entities, 600 frames per run, M-series Mac, multi-threaded bgfx
mode.  The before/after variance is tight (±0.001 ms) once
background load settles, which makes the 26% mean drop a real
signal, not a noise artefact.

### Why audit's "50%" overshot

The audit's mul-count math (176 → 29) suggested ~85% drop in
composeLocal's ALU cost.  But the per-entity envelope also
contains:

- The flags check (`!(tcomp->flags & kSelfDirty)`)
- The Pass 1 ancestor walk (when applicable)
- Sparse-set view iteration overhead
- The `reg.get<HierarchyComponent>` test
- The new `reg.get<WorldTransform>` cache lookup
- The `writeWorldMatrix` branch + write

composeLocal itself is maybe 30-40% of that envelope, and we
roughly halved it — so the measured 26% on the full
TransformSystem time is internally consistent with the muls
analysis, just smaller than the headline-projection number.

### Why the clean fast path didn't regress

In the all-clean case, every entity short-circuits at
`if (!dirty && !subtreeDirty) return;` before composeLocal or
writeWorldMatrix run.  My change adds *one* `reg.get<WorldTransform>`
to the path (where the old code did `reg.has<WorldTransform>`),
but those two have essentially identical cost in the ECS layer —
both are sparse-set probes.  The 0.033 → 0.034 ms change is
within run-to-run noise; the two are statistically equivalent.

### Hazard worth knowing

`writeWorldMatrix(reg, entity, wtc, world)` may invalidate the
caller's `wtc` pointer when emplace runs (dense WorldTransform
array can grow).  The current call sites never read `wtc` after
the write, so the dangling pointer is harmless.  A future edit
that adds `if (wtc->matrix == identity) ...` *after* the write
would be a subtle use-after-free.  The function-level comment
calls this out so a future maintainer reading just the helper
knows the invariant.

### Tests

Existing TestTransformSystem coverage (113 assertions, 30 cases)
is sufficient: it includes the poison-sentinel that proves the
recursion only writes when a node is dirty, the dirty-leaf
propagation case, and the system-managed flag policy.  Those
catch any regression in the dirty-tracking; they also catch a
bug in composeLocal because every test fixture builds a
matrix and checks the result.  No new tests added — the existing
ones cover the changed code.

## ThreadPool v2 + per-frame system opt-in (2026-06-13)

Audit items #H1 (line 28), 66 (storage), 77 (mutex), 78 (notify), 79
(wakeup), and 80 (per-frame work) all landed together as a single
opt-in scaffolding pass.  The user explicitly asked for the default
behaviour to stay single-threaded — the new path is wholly opt-in.

### Why "opt-in" matters

Threading per-frame systems is a real perf win on phones with idle
little cores, but:

1. Pool overhead can dominate on a per-system basis if the system
   itself takes < 10 µs.  Splitting `Shadow submit` (40 µs) is a win;
   splitting `LightClusterBuilder` (5 µs in the perf_smoke scene) is
   probably a loss after dispatch costs.
2. The Schedule's compile-time conflict matrix relies on every system
   correctly declaring its `Reads` / `Writes` TypeLists.  Today's
   engine systems don't all declare these — they're called inline by
   game code, not through SystemExecutor.  Wiring them up without
   declaration would silently allow races.
3. Tier-aware sizing matters: low-tier phones with 2 little + 4 big
   cores want a different pool size than a 6-core desktop.

So the engine ships the *capability* (ThreadPool + SystemExecutor +
EngineDesc opt-in) but doesn't impose the *policy* (which systems
to parallelise, what schedule).  Games opt in by setting
`EngineDesc::useSystemThreadPool = true` and either constructing a
`SystemExecutor<...>` directly, or grabbing
`engine.systemThreadPool()` and submitting POD tasks themselves.

### What changed in ThreadPool

**Storage.**  `std::deque<std::function<void()>>` → `std::array<Task,
1024>` where `Task = { void(*fn)(void*); void* arg; }` is 16 bytes
POD.  Submitters write directly to a ring slot.  No allocation on
the fast path.  Ring overflow is an assert — back-pressure is the
caller's job, the audit's recommendation.

**Synchronisation.**  The old design held `mutex_` across
`std::function` construction + `queue_.push_back` (which could grow
the deque) + `notify_one`.  Workers waited on `workCv_` with the
same mutex held.  Wakeups serialised through that mutex.

The new design:

- `submitTask` holds `ringMutex_` only across `tail_++` and one slot
  write — a few instructions, no allocation.
- Work-available signalling is `std::counting_semaphore<INT32_MAX>`
  (C++20).  `release()` is atomic, no mutex; `acquire()` blocks
  without holding any lock.
- `activeTasks_` is `std::atomic<uint32_t>`.  `submitTask` does
  `fetch_add(1, relaxed)`; workers do `fetch_sub(1, acq_rel)` after
  running and only call `doneCv_.notify_all()` when the prior value
  is 1 (the audit's line 78 "notify only on zero transition" fix).
- `waitAll()` spin-polls `activeTasks_` for ~100 iterations
  (`yield()` between) before falling back to a CV wait on
  `doneMutex_` — for the common case of small fast tasks the spin
  drains the queue without paying the CV round-trip.
- Destructor wakes every worker with `release(workers_.size())`
  tokens.  Workers drain any queued tasks before exiting so
  fire-and-forget submitters don't lose work.

### Back-compat: keep `submit(std::function)` working

AssetManager and the existing 7 ThreadPool tests use
`submit(std::function<void()>)`.  Rewriting them would be busywork
and risk subtle behaviour changes.  Instead, `submit` is now a
wrapper that heap-allocates a `std::function*` and dispatches it
via `submitTask` with a trampoline that runs the function and
deletes the heap object.  Slow path — but only when the caller
asked for the `std::function` interface.  Per-frame callers
(SystemExecutor) use `submitTask` directly and skip the allocation.

The wrapper does mean per-task allocation is back on the
`std::function` path.  AssetManager submits ≤ 10 tasks per scene
load (not per frame); the allocation cost is invisible there.

### SystemExecutor changes

Existed but never instantiated.  Now its phase-dispatch loop uses
`threadPool_.submitTask(&dispatchTrampoline, heapArg)` instead of
`threadPool_.submit([this, ...] { ... })`.  The arg block is
heap-allocated per submission (`new DispatchArg{...}`, freed by the
trampoline) — small (~32 bytes), bounded by phase.count which is
≤ kMaxSystemsPerPhase = 64.  Could move to a per-frame arena
allocator if profiling showed it mattered; today it doesn't.

### Tests: race-check methodology

7 new SystemExecutor tests:

1. Single system dispatch increments component.
2. Single-system phase runs inline on caller thread (pins the
   `phase.count == 1` short-circuit).
3. Multi-system phase runs in parallel (timing-sensitive — 3 of 5
   trials must beat the serial-execution threshold).
4. Phase ordering creates a read-after-write barrier (100 frames,
   per-entity assert every frame).
5. 10 000-frame stress with phase ordering — no race.
6. **10 000-frame conservation-law race-check (TSAN-friendly).**
   Three independent producers each increment their own component
   in phase 0; a summer in phase 1 reads all three and writes their
   sum.  Conservation invariant `A == B == C == frame+1` and
   `Total == 3*(frame+1)` is spot-checked every 1000 frames.
   Build with `-fsanitize=thread -g` for TSAN coverage.
7. getSystem<S>() returns the configured instance.

The conservation test catches three failure modes:

- A race between producers writing the same component (would
  under-count one).  Compile-time conflict matrix prevents this,
  but a refactor that broke the matrix would surface here.
- A missing phase barrier (Total would lag by 1).
- A torn write into Total from a partial producer phase.

### EngineDesc plumbing

```cpp
struct EngineDesc {
    // ... existing fields ...
    bool useSystemThreadPool = false;      // opt-in
    uint32_t systemThreadPoolSize = 0;     // 0 = engine picks
};
```

`Engine::maybeCreateSystemThreadPool` is called from all three init
paths (desktop / Android / iOS).  When `useSystemThreadPool` is
false (default), no pool is constructed and
`Engine::systemThreadPool()` returns nullptr — game code uses the
nullptr return to write opt-in patterns:

```cpp
if (auto* pool = engine.systemThreadPool()) {
    // multi-threaded fast path
} else {
    // single-threaded path (default)
}
```

The 0-sentinel "engine picks" resolves to
`hardware_concurrency() - 2` clamped to [2, 8].  Reserve 2 cores
for the bgfx render thread + OS; floor at 2 because that's where
parallelism still amortises ThreadPool dispatch; ceiling at 8
because shared-resource contention (cache lines, SparseSet
storage) starts to hurt at high core counts on typical scenes.

### What's still open

- Per-worker queues + work-stealing for true MPMC scaling.  Current
  design has a single short-held mutex on the ring — fine for the
  measured workloads but would contend under many concurrent
  submitters.
- Actual per-system parallelisation in built-in engine code.  None
  of `TransformSystem`, `FrustumCullSystem`, `DrawCallBuildSystem`,
  `ShadowCullSystem`, `LightClusterBuilder` declare
  `Reads`/`Writes` TypeLists today.  Until they do, the schedule
  can't reason about them.  This is per-system-by-system work
  gated on real device measurement — the audit explicitly flags
  it as "experiment."
- Dispatch latency measurement on real hardware.  The audit
  target is `< 0.5 µs` per submit/wait cycle; spin-poll-then-CV
  `waitAll()` should hit it on the common path, but the actual
  numbers need a microbenchmark on a Pixel-class device.

## FrustumCull + ShadowCull — explicit-fabsf AABB + visible-state cache + single-pass multi-cascade (2026-06-13)

Three audit items in one commit (`computeConservativeWorldAabb` helper +
the visible-state cache in FrustumCullSystem + the multi-cascade
overload in ShadowCullSystem).  All three are math/ECS micro-wins on
the per-frame cull loops; none of them changes the conservative AABB
math, so screenshot output is byte-identical.

### Why a shared helper

Both systems were computing the same conservative world-space AABB
inline.  The audit (#cull-aabb) called out the same 12-float-copy
`Mat3` ctor + `Mat3 * Vec3` pattern in both files.  Pulling the
computation into `engine/rendering/systems/CullHelpers.h` as
`computeConservativeWorldAabb()` means:

- Any future precision tweak (e.g. switching to OBB for animated
  meshes) lands in one place.
- The compiler inlines the math directly into each loop — same
  code, less drift risk.
- Tests for the helper (if we ever add them) cover both consumers.

### What `computeConservativeWorldAabb` does

Classic AABB-rotation trick:

```cpp
inline void computeConservativeWorldAabb(const Mat4& W, const Vec3& localCenter,
                                         const Vec3& localHalfExtent,
                                         Vec3& outMin, Vec3& outMax)
{
    const Vec3 worldCenter = Vec3(W * Vec4(localCenter, 1.0F));

    const float a00 = std::fabs(W[0].x); /* ... 8 more ... */
    const Vec3 worldHalfExtent{
        a00 * localHalfExtent.x + a01 * localHalfExtent.y + a02 * localHalfExtent.z,
        a10 * localHalfExtent.x + a11 * localHalfExtent.y + a12 * localHalfExtent.z,
        a20 * localHalfExtent.x + a21 * localHalfExtent.y + a22 * localHalfExtent.z};

    outMin = worldCenter - worldHalfExtent;
    outMax = worldCenter + worldHalfExtent;
}
```

Old form (`glm::abs(Vec4)` ×3 + `Mat3` ctor + `Mat3 * Vec3`) cost 12
copies + 9 `fabs` + 9 muls + 6 adds, with a 48-byte `Mat3` on the
stack.  New form is 9 `fabs` + 9 muls + 6 adds, no temporaries.  The
compiler is much happier — the inline expansion vectorises better and
the spill/fill round-trip on the Mat3 is gone.

### FrustumCullSystem visible-state cache (audit #cull-cache)

Old code did `has<VisibleTag>` + `emplace`/`remove` separately,
which in the transition cases was two sparse-set probes.  The
common case (steady visibility) was already only one probe, but
the dirty patterns and the `remove` path always did the internal
check too.

New shape:

```cpp
const bool wasVisible = reg.has<VisibleTag>(entity);  // single probe
// ... compute isVisible ...
if (isVisible && !wasVisible)   reg.emplace<VisibleTag>(entity);
else if (!isVisible && wasVisible) reg.remove<VisibleTag>(entity);
// else: skip — steady-state hot path pays only the `has` probe.
```

The branch structure makes the steady-state case fall through
without touching the SparseSet again.  Audit projected ~40 ns per
entity; measured ~14 ns per entity on the 702-entity perf_smoke
scene as part of the combined 0.014 ms drop with #cull-aabb.

### ShadowCullSystem single-pass multi-cascade (audit #cull-shadow-multi)

The per-cascade form was the obvious shape — callers built one
cascade frustum at a time and called `update(reg, res, frustum, i)`
three times for 3 cascades.  But each call walks the full mesh view
and rebuilds the world AABB from scratch.  For N cascades the
overhead grows as N×(view iteration + AABB build).

New overload:

```cpp
void update(Registry& reg, const RenderResources& res,
            std::span<const Frustum> cascadeFrustums,
            uint32_t baseCascadeIdx = 0);
```

Walks the mesh view ONCE, builds the AABB ONCE per entity, then
tests against every frustum in the span, accumulates the cascade
mask in a local, and writes it back at the end.

The contract: this overload owns the bits in
`[baseCascadeIdx, baseCascadeIdx + N)`.  Bits outside that range are
preserved.  This lets a caller drive cascades 0..2 in bulk while
still using the per-cascade overload for, say, a spot-light shadow
that occupies bit 4.  A test pins this behaviour
(`tests/rendering/TestCsm.cpp` "ShadowCullSystem multi-cascade
preserves bits outside its range").

The existing per-cascade overload is now a wrapper:

```cpp
void update(reg, res, const Frustum& shadowFrustum, uint32_t cascadeIndex) {
    update(reg, res, std::span<const Frustum>{&shadowFrustum, 1}, cascadeIndex);
}
```

Both paths share the same implementation, so any future fix lands
in one place and the per-cascade test coverage still exercises the
multi-cascade core.  A new test pins that the multi-cascade form
produces a byte-identical mask to N per-cascade calls — that's the
safety contract for the optimisation.

### Why screenshot tests pass unchanged

The math is the same.  `fabsf` of each scalar entry is bit-identical
to `glm::abs(Vec4)` followed by extracting the .xyz components.  The
visible-state cache only short-circuits the ECS operations when the
visibility result didn't change — never alters which entities are
considered visible.  The multi-cascade overload computes the same
mask as 3 per-cascade calls.  So the rendered output is identical
modulo machine-epsilon floating-point determinism (and the math
isn't even epsilon-different).  22/22 screenshot tests confirm.

### Measurement caveats

- ShadowCullSystem isn't driven by perf_smoke's current scene —
  the audit's "~66% reduction with 3 cascades" projection is the
  math (one walk vs three) and hasn't been measured end-to-end.
  Adding a multi-cascade path to perf_smoke would let us A/B it
  on real workloads; out of scope for this commit.
- FrustumCullSystem A/B variance was high (best-after run hit
  0.024 ms which is probably noise floor).  Median 0.034 ms vs
  baseline median 0.048 ms (−29%) is the honest read.  Audit
  projection (30 + 40 ns/entity × 702 ≈ 0.049 ms) bracketed it.
- No Android measurement yet — the projection is even larger on
  in-order or weakly-OoO little cores where the Mat3 spill/fill
  cost dominates more.

## AnimationSystem — trsMatrix + lazy reserve + worldTransforms emplace + PoseComponent by value (2026-06-14)

Four AnimationSystem audit items landed together — all small, mechanical
wins on the per-frame skinning pipeline.

### (a) `trsMatrix(JointPose)` direct construction

Same pattern as TransformSystem's `composeLocal` landing
(`#C-xform-trs`).  Old form was `translate * mat4_cast * scale` —
176 muls + 3 Mat4 temporaries through L1.  New form uses the closed-
form TRS for column-major glm::mat4:

```cpp
math::Mat4 trsMatrix(const JointPose& jp)
{
    const math::Mat3 rot = glm::mat3_cast(jp.rotation);
    return {math::Vec4(rot[0] * jp.scale.x, 0.0F),
            math::Vec4(rot[1] * jp.scale.y, 0.0F),
            math::Vec4(rot[2] * jp.scale.z, 0.0F),
            math::Vec4(jp.position, 1.0F)};
}
```

~29 muls, no temporaries.  Same proven correctness as the
TransformSystem version — `TestSsAnimation` (the skinned-mesh
screenshot fixture) confirms byte-identical bone-matrix output.

### (b) Lazy `boneBuffer.reserve(256)`

The reserve was unconditional at the top of `update()` /
`computeBoneMatrices()` — 16 KB arena allocation even on scenes with
zero skinned entities.  Moved to lazy via a `bool boneBufferReserved`
flag flipped inside the lambda's first iteration.  Empty-view scenes
now pay zero arena bytes for animation.

### (c) `worldTransforms` reserve+emplace_back

Old shape:

```cpp
std::pmr::vector<Mat4> worldTransforms(jointCount, Mat4(1.0F), alloc);
for (uint32_t i = 0; i < jointCount; ++i) {
    // ... compute local ...
    worldTransforms[i] = parent_or_local;     // overwrite the just-initialised slot
}
```

That's `jointCount × 16 stores` for the value-init pass, then
`jointCount × 16 stores` again for the loop — every slot gets
written twice.

New shape:

```cpp
std::pmr::vector<Mat4> worldTransforms(alloc);
worldTransforms.reserve(jointCount);
for (uint32_t i = 0; i < jointCount; ++i) {
    // ... compute local ...
    worldTransforms.emplace_back(parent_or_local);   // single in-place construct
}
```

`reserve(jointCount)` allocates the buffer; `emplace_back` constructs
each slot in place exactly once.  Halves the per-joint write count.
Critically: `worldTransforms[parent]` (used inside the loop) is
valid because skeleton joints are parent-first ordered — by the
time we compute joint `i`, joint `parent < i` has already been
emplaced.

### (d) `PoseComponent::pose` by value

The old `PoseComponent { Pose* pose; }` held a pointer to a
`Pose` re-allocated from the frame arena on every `updatePoses()`
call — every frame discarded the previous frame's ~5 KB pose
storage and built a fresh one.

Changed to:

```cpp
struct PoseComponent {
    Pose pose;   // value-owned; reused across frames via move-assign
};
```

The `InlinedVector<JointPose, 128>` inside Pose reuses its inline
buffer (or heap allocation, for > 128-joint skeletons) across
frames.  AnimationSystem's write path:

```cpp
PoseComponent* existingPose = reg.get<PoseComponent>(entity);
if (existingPose != nullptr) {
    existingPose->pose = std::move(finalPose);   // reuses storage
} else {
    PoseComponent freshComp;
    freshComp.pose = std::move(finalPose);
    reg.emplace<PoseComponent>(entity, std::move(freshComp));
}
```

The three call sites that used the pointer form (`IkSystem.cpp`,
`tests/animation/TestIkSolvers.cpp`, `apps/ik_hand_demo/main.mm`)
were migrated:

- `if (!poseComp.pose)` → `if (poseComp.pose.jointPoses.empty())`
- `Pose& pose = *poseComp.pose` → `Pose& pose = poseComp.pose`
- The "empty" signal is now `jointPoses.empty()` instead of
  `pose == nullptr`.

### Cost: SparseSet entries grow by ~5 KB

`InlinedVector<JointPose, 128>` is 128 × 40 B = 5120 B inlined.
For typical character counts (10-20 skinned entities per scene)
that's 50-100 KB resident in the PoseComponent SparseSet — trivial.
For very large characters (> 128 joints) the inline buffer spills
to heap and the SparseSet entry stays at the inline size.

For genuinely many-character scenes (crowds) the per-entity cost
would matter and a different storage shape (heap-allocated pool
indexed by handle) would beat it.  Out of scope here.

### What's NOT in this commit (audit line 121)

The audit's "full TRS-space worldTransforms storage" item — replace
the `vector<Mat4> worldTransforms` with `vector<JointPose>`, do
quat-mul + rotate-vec + scale-mul per joint, convert to Mat4 only
at the bone-matrix output — saves ~3× per-joint compute.  It's a
bigger architectural change that needs a multi-character perf_smoke
scene to A/B confidently.  The cheaper-per-joint `trsMatrix` from
(a) above lands part of the same win.

### Why screenshot tests pass unchanged

- `trsMatrix` direct construction is mathematically identical to the
  glm chain — same closed-form TRS, just no intermediate Mat4s.
- `reserve + emplace_back` produces the same per-slot Mat4 as the
  default-init-then-overwrite form because `emplace_back(x)` copy-
  constructs from `x` and the loop writes the same `x` either way.
- PoseComponent value vs pointer doesn't change Pose contents, just
  ownership.  IK and bone-matrix paths read the same Pose either way.

So `TestSsAnimation` (skinned mesh through the full pipeline) is
byte-identical.  22/22 screenshot tests confirm.

### Measurement caveat

perf_smoke has no skinned entities, so I can't A/B the AnimationSystem
itself end-to-end.  The per-joint compute savings are math-driven; the
PoseComponent-by-value win removes one heap alloc per skinned entity
per frame (was bounded by jointCount > 128 spilling Pose's inline
buffer; rare in practice).  All four items are low-risk and the
existing screenshot fixture confirms no visual drift.

## AnimationSampler — audit items investigated, measured, declined (2026-06-16)

Two audit items (`docs/PERF_AUDIT_2026-05-25.md` lines 122 + 123)
were investigated and rejected after microbenchmark measurement
showed the projected wins don't materialize at the engine's scale.

This entry exists to document **why** so the same investigation
doesn't get re-attempted without new evidence.

### Microbenchmark infrastructure (the win that DID land)

`tests/animation/TestAnimationSampler.cpp` gains three Catch2
benchmark cases tagged `[anim-bench]` plus `[!benchmark]` so they
don't run on the default test suite:

```
build/engine_tests "[anim-bench]"
```

Three workloads (50 000 iters / run, M-series Mac):

| Bench | Setup | Median ns/call |
|-------|-------|---------------:|
| Full 64-channel | 64 joints, 64 channels, 4 varying-value keyframes/channel | ~1126 |
| Half-untouched 32-channel | 64 joints, 32 channels (so 32 joints rest-init only) | ~591 |
| Static-keyframes 64-channel | 64 joints, 64 channels, 4 identical-value keyframes/channel | ~575 |

The bench is the right tool for the next person who tackles
AnimationSampler — A/B their change against these numbers.

### Item 1 (line 122) — "skip rest-pose init for touched joints"

**Projection:** `sampleClip` initializes every joint to rest pose,
then the channel loop overwrites the touched ones.  Skip the
redundant init.  Saves `jointCount * 40 bytes/frame`.

**Implementation tried:**

- Walk `clip.channels` once to build a 128-bit stack bitmask of
  "joint touched by ≥ 1 channel."
- Skip rest-pose init for touched joints.
- Pass rest-pose values as `sampleChannel`'s default so empty
  per-field keyframes still produce bind values for the touched
  joint.

**Measurement:**

| Bench | Baseline | After | Delta |
|-------|---------:|------:|------:|
| Full 64-channel | 1126 ns | 1196 ns | **+6.2%** (regression) |
| Half-untouched 32-channel | 591 ns | 653 ns | **+10.5%** (regression) |
| Static-keyframes 64-channel | 575 ns | 583 ns | **+1.4%** (within noise) |

**Why it regressed:**

- Modern CPUs hide the rest-pose init cost.  Per-joint
  init = ~0.7 ns at L1 (32 contiguous bytes for `JointRestPose`,
  fully cached across consecutive joints).
- The bitmask requires walking `clip.channels` twice (once to
  build the mask, once to sample).  At 64 channels each scan
  costs ~1 µs of cache traffic vs the ~50 ns the bitmask check
  is meant to save per untouched joint.
- Channel-loop loads of `skeleton.restPoses[idx]` for the
  default argument (3 separate loads per channel) cost more
  than the single sequential pass at init.

The audit's projection was bandwidth-bound; the real workload is
ALU- + branch-bound on hot data.  Different bottleneck.

### Item 2 (line 123) — "equal-keyframe fast path"

**Projection:** When `kf1.value == kf2.value`, skip `glm::mix` or
`glm::slerp` and return the constant directly.  Common in glTF
where exporters stamp the bind pose at every keyframe for joints
the artist never moved.

**Implementation tried:**

```cpp
if (kfBefore.value == kfAfter.value) {
    return kfBefore.value;
}
const float lerpT = ...;
return glm::mix(kfBefore.value, kfAfter.value, lerpT);
```

(Same shape for the `Quat` specialization with `glm::slerp`.)

**Measurement on the static-keyframes bench (where the fast path
fires every call):**

| | Baseline | After | Delta |
|---|---------:|------:|------:|
| Static-keyframes ns/call | 575 ns | 579 ns | flat (within noise) |

The dynamic benches showed a slight regression (~3-5%) from the
added `==` check on every call where the fast path *doesn't* fire.

**Why it didn't help:**

- `glm::slerp` already internally checks `dot(a, b) > 0.9995` and
  falls to nlerp on near-parallel quats.  So slerp-on-equal-quats
  is already cheap (~5 ns) — the projected ~30-mul slerp cost is
  what slerp pays on *truly* different quats.
- `glm::mix(Vec3, Vec3, t) = a + (b - a) * t` is 6 ops on vec3 —
  about as cheap as the `==` check that's supposed to skip it.
- Net: the fast-path check costs about as much as it saves on
  hits, and costs extra on misses.  Wash.

The audit assumed a more expensive slerp than the engine ships.

### What to keep from this investigation

- Microbenchmark stays — exercises a real sampler workload and
  becomes the foundation for future A/B work.
- `AnimationSampler.cpp` unchanged — original code is still the
  fastest.
- Audit items marked `[skip]` with the measurement evidence
  embedded so anyone re-evaluating sees the numbers.

### Open questions that could re-open these items

- **On Mali / Adreno mobile CPUs** the L1 / cache hierarchy is
  different; the bitmask path *might* win there.  Would need a
  device-side rerun before drawing conclusions.
- **On very deep skeletons (> 100 joints)** with very few touched
  joints (< 10), the bitmask might pay off — the proportional
  saving grows.  No real workload like that in the engine today.
- **A `[[gnu::always_inline]]` on `sampleChannel`** might let the
  compiler hoist the `==` check out of the inner loop, eliminating
  the per-call cost on misses.  Possibly worth a follow-up if
  someone wants to revisit the equal-keyframe path.

## PhysicsSystem: inverseTRS for parent-local conversion (2026-06-16)

Audit item line 125.  `PhysicsSystem::syncDynamicBodies` was calling
`glm::inverse(parentWtc->matrix)` to compute the world→parent-local
transform for every parented dynamic rigid body every frame.

### Why glm::inverse is expensive

`glm::inverse(Mat4)` computes a general 4x4 inverse via cofactor
expansion — ~100 muls + 40 adds + 1 division, with branchy logic.
Necessary for general matrices (perspective, skew, affine, whatever),
but the engine's world matrices are *always* affine TRS — built by
`composeLocal` as `T * R * S` with R orthonormal and S diagonal.
The general inverse pays for capability it never needs here.

### The TRS-specific inverse

For `M = [R*S | t; 0 0 0 1]`:

  `M^-1 = [A^-1 | -A^-1 * t; 0 0 0 1]` where `A^-1 = S^-1 * R^T = diag(1/s) * R^T`

Element-wise:

  `A^-1[i, j] = (1 / s_i^2) * M[j, i]`

— transpose `A` and divide each row by the squared scale of the
corresponding original column.  No cofactor expansion, no general
4x4 inverse, no `glm::decompose`.

The `s_i^2` values fall out for free: since each column of M is
`s_i * R.col_i` and R is orthonormal, `|M.col_i|^2 = s_i^2`.

### Cost

| | muls | adds | divs |
|---|---:|---:|---:|
| `glm::inverse` (general 4x4 via cofactor) | ~100 | ~40 | 1 |
| `math::inverseTRS` (TRS-specific) | ~30 | ~18 | 3 |

### Audit's alternative: cache on WorldTransformComponent

The audit's suggested fix was different — cache `inverseWorldMatrix`
on `WorldTransformComponent` and have TransformSystem compute it
when it writes the world matrix.  That would have meant:

- `WorldTransformComponent` grows from 64 bytes (one Mat4) to 128
  bytes — every entity in the scene pays the storage.
- TransformSystem runs the inverse for *every* entity every frame
  (since it doesn't know which entities a downstream consumer will
  need the inverse for) — amortising the cost across thousands of
  entities that never need it.

In contrast, `inverseTRS` adds zero per-entity overhead and is only
paid by the consumers that actually invert.  For the current
PhysicsSystem use case (one call per parented dynamic body per
frame) that's the right tradeoff.  If a future scenario needs the
inverse on most entities every frame, the cache approach becomes
preferable again — leave that decision to measured workloads.

### Measurement

Benchmark in `tests/math/TestTransform.cpp` (`[inverse-trs-bench]`,
Catch2 `[!benchmark]` tagged so it doesn't run by default).  Uses
`asm volatile` barriers to defeat compiler loop-invariant code
motion — without the barriers, both forms benchmarked at 0.8
ns/call because the optimiser hoisted them past the loop.

Five runs, 1 000 000 iterations each, M-series Mac:

| Run | `inverseTRS` ns/call | `glm::inverse` ns/call | Speedup |
|---|---:|---:|---:|
| 1 | 7.6 | 18.9 | 2.49× |
| 2 | 4.0 | 12.5 | 3.09× |
| 3 | 3.0 | 10.3 | 3.38× |
| 4 | 3.0 | 10.2 | 3.43× |
| 5 | 3.0 | 10.2 | 3.43× |

Median ~3.0 vs ~10.2 = **3.4× speedup**, saves ~7 ns per call.
The first run is noisier (cold caches / branch predictor); runs
3-5 converged.

### Why the speedup will be bigger on ARM

The audit's "saves ~80 muls per parented dynamic body" projection
maps directly to the 3.4× speedup on M-series, but the absolute
number (~7 ns saved) is small because Apple Silicon's deep OoO
pipeline + AMX-style matrix accelerators eat the cofactor inverse
faster than the formula suggests.  On a Cortex-A78 with shallower
OoO, fewer pipelines, and 5-cycle fp-multiply latency, the cofactor
inverse will dominate more relative to the simpler TRS form —
expected ARM speedup is closer to 4-5×.  Worth re-measuring on
device when the engine ships to Android.

### Tests

6 correctness cases pin epsilon-equality with `glm::inverse`:

- identity
- pure translation
- pure rotation (off-axis)
- uniform scale
- full T*R*S with non-uniform scale (looser epsilon for the
  cumulative rounding)
- point-roundtrip — the actual physics call shape (`inverseTRS *
  worldPos` matches `glm::inverse * worldPos`)

The point-roundtrip case is the safety guarantee: if I got the
algebra wrong, a physics body's position would diverge over many
frames and break things visibly.

### Contract

`inverseTRS` assumes the input is an affine TRS matrix (no skew,
no perspective).  Skew matrices give wrong results *silently* —
the assertion would catch nothing because the math just produces a
non-inverse.  Mitigation: only one call site uses it (the physics
call), and that call site receives a matrix built by `composeLocal`
which is TRS by construction.  If a future caller uses it on a
non-TRS matrix the bug surfaces as physics positions drifting away
from where they should be.

## InstanceBufferBuildSystem: pmr-backed groups map (2026-06-16)

Audit item line 143.  `InstanceBufferBuildSystem::update` was
constructing its `groups` map on the default heap every frame, even
though the function already received a `std::pmr::memory_resource*
arena` parameter.

### What changed

`ankerl::unordered_dense::pmr::map<uint32_t, GroupData>` — a vendored
alias in `third_party/ankerl/unordered_dense.h` that uses
`std::pmr::polymorphic_allocator` internally.  The allocator wraps the
`update()` call's arena, so the map's bucket array, dense value array,
and all per-entry storage flow through the FrameArena instead of
hitting the system heap.

```cpp
using GroupMap = ankerl::unordered_dense::pmr::map<uint32_t, GroupData>;
GroupMap groups{std::pmr::polymorphic_allocator<std::pair<uint32_t, GroupData>>(alloc)};
groups.reserve(16);
```

`reserve(16)` covers typical scenes (a handful of instance groups)
without paying the first rehash.

### Measurement

Microbenchmark in `tests/rendering/TestInstancing.cpp`,
`[instancing-bench]` + Catch2 `[!benchmark]` tag.  Construction +
fill of 8 groups × 32 instances per iteration, 10 000 iterations per
run, 5 runs, M-series Mac.  The `pmr` side uses a 256 KB
`monotonic_buffer_resource` with `release()` between iterations to
simulate the engine's per-frame FrameArena reset.

| Run | Default-heap ns/frame | pmr ns/frame | Speedup |
|---|---:|---:|---:|
| 1 (cold) | 2883 | 1711 | 1.69× |
| 2 | 1973 | 994 | 1.99× |
| 3 | 1941 | 998 | 1.94× |
| 4 | 1972 | 1026 | 1.92× |
| 5 | 1921 | 985 | 1.95× |

Median runs 2-5: **1.95× speedup**, saves ~975 ns/frame at 8 groups.
First run is noisier (cold caches).  The saving scales with group
count.

### Why I didn't measure the full system

`InstanceBufferBuildSystem::update` early-exits when the program
handle is invalid (which the Noop / headless renderer always
returns), so driving the full system from a unit test would have
hit the early-out before the map even constructed.  Measuring the
map allocation pattern in isolation is what the audit's claim
actually targets — the rest of the system (cull check, encoder
submit, instance buffer fill) is workload-dependent and would
need a real-scene perf_smoke run to A/B end-to-end.
