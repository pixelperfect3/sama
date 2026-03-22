# Engine Development Notes

Tracks all decisions and progress made during development.

---

## Project Setup

### Tooling
- **clang-format:** Run after every C++ file write/edit to enforce consistent style
  ```
  brew install clang-format   # one-time setup if not installed
  clang-format -i engine/ecs/*.h engine/ecs/*.cpp tests/ecs/*.cpp
  ```
  Config: `.clang-format` at project root (Allman braces, 4-space indent, 100 char limit)
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
- **C++ version:** C++20 (Modules excluded due to inconsistent cross-platform build support)
- **Design target:** ~50k active entities

- **Threading model:** System-level parallelism (independent systems run concurrently)
  - Note: Revisit component-level (worker thread) parallelism later if it becomes a bottleneck for higher-end games

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

## Open Decisions (Pending Discussion)
- Input framework per platform
- Networking
- Editor design
- 2D support timing
- External library policy
