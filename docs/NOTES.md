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
- **Intra-system threading:** Deferred — revisit component-level worker threads if a single system becomes a bottleneck on high-end targets
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
- **Ray tracing: tabled as long-term**
  - bgfx does not expose RT APIs (no DXR, no VK_KHR_ray_tracing_pipeline, no Metal RT)
  - Adding RT to bgfx would require forking it: new resource types (BLAS, TLAS, shader binding tables), new shader stages (raygen, miss, hit), shaderc changes, and separate Metal/Vulkan backend implementations — not a clean fit into bgfx's abstraction model
  - RT on mobile is not viable in the near term (requires A17 Pro/M-series for Metal, high-end Android for Vulkan RT)
  - Revisit when RT is ready to implement — evaluate bgfx RT support status at that time, or consider a targeted custom RT layer on top

### Status
- [ ] Rendering not started

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
  - JSON parser to decide: nlohmann/json (header-only, ~1MB) or rapidjson (faster, smaller) — pending external library policy discussion
- **GPU instancing:** Separate system from the scene graph, needed early for vegetation/foliage/rocks
  - Millions of instances (grass, trees, pebbles) rendered via single draw calls — not scene graph nodes
- **Scale target:** ~20k–50k actively managed scene graph nodes at runtime, with region-based streaming loading/unloading around the player

### Status
- [ ] Scene graph not started

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

One architectural decision is still open and blocks implementation of the scene graph serialization system.

| Item | Blocked by | Notes |
|---|---|---|
| JSON parser choice: nlohmann/json vs rapidjson | External library policy evaluation | nlohmann/json: header-only, ~1MB, easy to use. rapidjson: faster parse, smaller binary. Evaluate at integration time per external library policy. See NOTES.md → Scene Graph |
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
