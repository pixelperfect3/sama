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
  - Views 8–13: fixed passes (depth prepass=8, opaque=9, transparency=10, reserved=11–13)
  - Views 14–15: UI/HUD
  - Views 16–47: post-process sub-passes (bloom needs 10+ sequential passes, each requiring its own view)
  This replaces the original "6 fixed views" design — the post-process chain is not a single view.

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
- [ ] Material editor: changing a field in the material inspector doesn't apply to the selected entity's `MaterialComponent`. The UI binds, but the write-back path is missing — likely needs a `MaterialInspector::commit()` hook in the same place rotation/scale edits got fixed (`b76908d`). *Why:* the inspector exists but is non-functional, so the panel is misleading.
- [ ] Selection outline: viewport-clicked entities have no visible highlight — the gizmo appears but the mesh itself is not stenciled. Spec calls for a single-pass stencil outline (§ 18.7 Rendering checklist). *Why:* hard to tell what's selected when the gizmo is occluded by geometry.
- [x] Viewport dirty-flagging: implemented in `EditorApp::run()` via a `viewportDirty` flag on `Impl`. The opaque/skinned PBR pass, selection outline, skybox, gizmos, and HUD overlay are all skipped when the flag is false; bgfx views are still touched so the swapchain re-presents the previous frame. The flag is set true on camera orbit/zoom (only when there's actual mouse delta), gizmo hover/mode/drag, picking, every property/material/light/visibility/transform write-back, every hierarchy mutation (rename excluded), undo/redo, scene new/open, asset import, environment loads, window resize, active animator playback, and forced true every frame while in Play mode. A "redraws: N" counter is shown in the HUD; idle frames stop incrementing it within 1-2 frames. *Tradeoff:* the FPS readout in the HUD only refreshes when the viewport redraws — acceptable since the whole point is to stop redrawing when idle. *Why this approach:* a global flag in `EditorApp::Impl` keeps the change confined to the editor (no engine API churn) and slots into the existing `hierarchyDirty` / `propertiesDirty` pattern.
- [ ] `TransformInspector` keyboard editing path (Tab/Arrow/+/-) bypasses the play-state gate added in Phase 12 — the gizmo and PropertiesPanel callbacks are blocked during Play, but typing into the inspector still mutates `TransformComponent` and races `PhysicsSystem::syncDynamicBodies`. Cleanest fix: thread `EditorState&` into `IComponentInspector::inspect()`. *Why:* a user inspecting numbers during Play could accidentally fight the simulation and not understand why nothing happens.
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

`Engine::beginFrame()` on Android continues to call `renderer_.beginFrameDirect()` for parity with all desktop demos: every existing demo (helmet, hierarchy, scene, physics, ik, animation, audio) calls `eng.renderer().beginFrameDirect()` itself rather than relying on the engine to set up the post-process framebuffer chain. Apps that want the full HDR + bloom + tonemap + FXAA chain must call `renderer().beginFrame()` followed by `renderer().postProcess().submit(...)` themselves — the same pattern as desktop. The post-process programs are valid on Android now (loaded from the APK), so opting in works identically across platforms.

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
