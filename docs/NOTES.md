# Engine Development Notes

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
  ```
  mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make engine_tests
  ./engine_tests --reporter compact
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
- [ ] ECS implementation not started

---

## Rendering

### Decisions
- **Abstraction layer:** bgfx (Metal + Vulkan backends only, all others disabled at compile time)
  - Chosen over IGL (Meta): bgfx has stronger game engine track record, more documentation/examples, includes shader tooling (shaderc)
  - IGL noted as stronger for Android long-tail device fragmentation — revisit if that becomes a pain point
  - Chosen over custom HAL: binary size savings (~1–2MB) do not justify months of low-level Vulkan/Metal boilerplate
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

All 235 test cases pass (4472 assertions).

### Status
- [x] All 11 rendering phases complete and committed

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
- [ ] Physics not started

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
- [ ] Audio not started

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
- [ ] Scene graph not started

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
- [ ] rapidjson not yet integrated (FetchContent entry needed in CMakeLists.txt)
- [ ] `engine/io/Json.h` wrapper not yet implemented

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
- [ ] Math library setup not started

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

### Status
- [ ] Input not started

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
- [ ] Editor not started

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
- [ ] 2D orthographic camera + sprite rendering not started
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
| Skeletal animation system | First game with animated characters or creatures | Not yet designed |
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

---

### Trigger: upstream / external contributions

Side projects to improve third-party libraries used by the engine.

| Item | Trigger | Notes |
|---|---|---|
| Jolt soft body upstream contributions | After Jolt is integrated and real usage surfaces the most painful gaps | Open GitHub discussion with jrouwe before writing code. Priority gaps: mesh/heightfield hair collision, GPU-accelerated soft body. See NOTES.md → Physics → Side Projects |

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

### Status
- Policy defined; applied as each library is integrated

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
- **Dirty flag optimization deferred** — unconditional recompute at 50k nodes is <1ms. Will add when profiling shows need.

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

---
