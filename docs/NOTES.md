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

### Decisions
- **Library:** libcurl (`curl_multi` interface for concurrent non-blocking downloads)
- **Streaming:** Assets downloadable at runtime while game runs (Fortnite-style) — not just batch pre-launch updates
- **Update triggers:** On launch + manually triggered by player
- **DLC/paid content:** Deferred — free patches and updates only for now
- **Architecture:**
  - **Download Manager:** Background thread running `curl_multi` poll loop, accepts prioritized requests, reports completions via thread-safe queue
  - **Asset Cache:** Local disk cache with LRU eviction and checksum verification on load
  - **Asset Handle:** Lightweight token returned immediately on request — game polls `isReady()` or registers a callback; never blocks the game loop
  - **Priority queue:** Three levels — Critical (needed now), High (prefetch, player approaching), Background (speculative)
  - **Manifest system:** JSON manifest on server listing every streamable asset (path, version, checksum, size); fetched on launch and on manual trigger; delta comparison determines what to download
- **World streaming integration:** `WorldStreaming` system detects player proximity to zones and issues `High` priority prefetch requests ahead of time
- **Placeholder/fallback strategy:** Low-res or lowest-LOD version shown while full asset downloads
  - Textures: low-res placeholder
  - Meshes: simplified proxy or bounding box
  - Audio: silence or generic ambient

### Status
- [ ] Networking not started

---

## Open Decisions (Pending Discussion)
- Editor design
- 2D support timing
- External library policy
