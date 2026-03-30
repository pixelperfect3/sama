# Anatomy of a Frame -- Nimbus Engine

This document traces what happens during a single game frame in the Nimbus
engine, from the moment `Engine::beginFrame()` is called to the moment
`bgfx::frame()` presents the result. Every statement is grounded in the
actual source code under `engine/`.

---

## 1. ASCII Frame Timeline

```
Time (ms, 60 fps = 16.67 ms budget)
|
|  PHASE                         CPU/GPU   Est. Time
|  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~  ~~~~~~~~  ~~~~~~~~~
|  [0] Timing (glfwGetTime)      CPU       < 0.01 ms
|  [1] Window Events (pollEvents)CPU       0.01-0.1 ms
|  [2] Resize Check              CPU       < 0.01 ms
|  [3] Input System Update       CPU       0.01-0.05 ms
|  [4] ImGui Begin Frame         CPU       0.02-0.1 ms
|  ---- end of Engine::beginFrame() ----
|  [5] Asset Uploads             CPU+GPU   0-2 ms (amortised)
|  [6] Animation System          CPU       0.1-1.0 ms
|  [7] Transform System          CPU       0.05-0.5 ms
|  [8] Physics System            CPU       0.5-4.0 ms
|  [9] Light Cluster Build       CPU+GPU   0.1-0.5 ms
|  [10] Frustum + Shadow Cull    CPU       0.05-0.3 ms
|  [11] Shadow Draw Calls        CPU->GPU  0.1-0.5 ms (CPU submit)
|  [12] Opaque Draw Calls (PBR)  CPU->GPU  0.2-1.0 ms (CPU submit)
|  [13] Post-Process Chain       CPU->GPU  0.1-0.5 ms (CPU submit)
|  ---- Engine::endFrame() ----
|  [14] ImGui End Frame          CPU       0.01-0.1 ms
|  [15] Frame Arena Reset        CPU       < 0.01 ms (O(1))
|  [16] bgfx::frame()            CPU->GPU  0.05-0.5 ms (submit + flip)
|                                          GPU work executes asynchronously
|                                          after bgfx::frame() returns
|
v
```

Notes on timing: bgfx operates in single-threaded mode (no internal render
thread -- see `Renderer::init` which calls `bgfx::renderFrame()` once before
`bgfx::init()` to prevent the default thread). All `bgfx::submit()` calls
during phases 11-13 record command buffers on the CPU; the GPU executes them
after `bgfx::frame()`.

---

## 2. Phase-by-Phase Breakdown

### Phase 0 -- Timing

**Source:** `Engine::beginFrame()` in `engine/core/Engine.cpp:183-191`

```
double now = glfwGetTime();
outDt = static_cast<float>(std::min(now - prevTime_, 0.05));
prevTime_ = now;
```

- **Reads:** `prevTime_` (double), GLFW monotonic clock
- **Writes:** `outDt` (capped at 50 ms to prevent spiral-of-death)
- **CPU/GPU:** CPU only
- **Memory:** Zero allocations

### Phase 1 -- Window Events

**Source:** `Engine::beginFrame()` -> `window_->pollEvents()`

- Calls `glfwPollEvents()` which dispatches all queued OS events (mouse,
  keyboard, resize, close) through registered GLFW callbacks.
- The scroll callback accumulates into `imguiScrollF_` for ImGui.
- **Reads:** OS event queue
- **Writes:** GLFW internal state, `imguiScrollF_`
- **CPU/GPU:** CPU only

### Phase 2 -- Resize Check

**Source:** `Engine::beginFrame()` lines 198-210

- Queries `glfwGetFramebufferSize` for physical pixel dimensions.
- If changed: calls `renderer_.resize()` which executes `bgfx::reset()` and
  `postProcess_.resize()` (reallocates HDR/bloom/LDR framebuffer textures).
- If minimised (0x0): calls `renderer_.endFrame()` early and returns with
  `outDt = 0`.
- **Reads:** `fbW_`, `fbH_`, framebuffer size
- **Writes:** `fbW_`, `fbH_`, GPU framebuffer handles (on resize only)
- **CPU/GPU:** CPU, with GPU resource allocation on resize

### Phase 3 -- Input System Update

**Source:** `Engine::beginFrame()` -> `inputSys_->update(inputState_)`

`InputSystem::update()` (see `engine/input/InputSystem.h`):
1. Polls the `IInputBackend` (GLFW backend) for raw events.
2. Computes per-key transitions (pressed/released) by diffing against
   `prevKeyDown_[]`.
3. Computes mouse delta from `prevMouseX_/Y_`.
4. Updates `InputState` snapshot: key states, mouse position, mouse delta,
   active touches.

- **Reads:** GLFW key/mouse/touch state, `prevKeyDown_[]`, `prevMouseDown_[]`
- **Writes:** `InputState` (keyboard, mouse, touch arrays)
- **CPU/GPU:** CPU only
- **Memory:** `eventBuf_` (persistent vector, reused each frame)

### Phase 4 -- ImGui Begin Frame

**Source:** `Engine::beginFrame()` lines 217-239

- Reads cursor position via `glfwGetCursorPos`, scales by DPI content scale.
- Reads mouse button state from GLFW.
- Feeds keyboard navigation keys into `ImGuiIO::KeysDown[]`.
- Calls `imguiBeginFrame()` with mouse position, buttons, scroll, and
  framebuffer dimensions. The view is `kViewImGui` (view 15).

- **Reads:** GLFW cursor/mouse/key state, `contentScaleX/Y_`
- **Writes:** ImGui internal state
- **CPU/GPU:** CPU only

### Phase 5 -- Asset Uploads (Application-Driven)

**Source:** `apps/animation_demo/main.mm` -> `assets.processUploads()`

- The application calls `AssetManager::processUploads()` to check for
  completed async loads (running on the `ThreadPool`).
- Completed assets (meshes, textures, skeletons, animation clips) have their
  GPU resources created (`bgfx::createVertexBuffer`, `bgfx::createTexture2D`,
  etc.) on the main thread (bgfx requirement).
- First spawn of a glTF model calls `GltfSceneSpawner::spawn()` which creates
  entities with `TransformComponent`, `MeshComponent`, `MaterialComponent`,
  `SkeletonComponent`, `AnimatorComponent`, `SkinComponent`, and hierarchy
  components.

- **Reads:** Async load results from worker threads
- **Writes:** `RenderResources` (mesh/material/texture tables), ECS registry
- **CPU/GPU:** CPU + GPU resource creation
- **Memory:** One-time allocations for new assets; amortised to zero in
  steady state

### Phase 6 -- Animation System

**Source:** `engine/animation/AnimationSystem.cpp`

`AnimationSystem::update(reg, dt, animRes, arena)`:
1. Allocates a `std::pmr::vector<Mat4> boneBuffer` from the **frame arena**
   (or default allocator if arena is null).
2. For each entity with `(SkeletonComponent, AnimatorComponent, SkinComponent)`:
   a. Advances `playbackTime` by `dt * speed`, handles looping/clamping.
   b. Samples the current `AnimationClip` via `sampleClip()` -- interpolates
      keyframes for each joint's position, rotation, scale.
   c. If blending is active, samples the target clip and calls `blendPoses()`
      with the current blend factor.
   d. Forward-pass matrix computation: for each joint in parent-first order,
      `worldTransforms[i] = worldTransforms[parent] * localTRS(pose[i])`.
   e. Computes final bone matrices:
      `boneMatrix[i] = worldTransforms[i] * inverseBindMatrix[i]`.
   f. Appends to the shared `boneBuffer`, recording
      `skinComp.boneMatrixOffset` and `skinComp.boneCount`.

- **Reads:** `AnimatorComponent`, `SkeletonComponent`, `AnimationClip`
  keyframes, `Skeleton` joint hierarchy and inverse bind matrices
- **Writes:** `AnimatorComponent` (playbackTime, blend state),
  `SkinComponent` (boneMatrixOffset, boneCount), `boneBuffer_` pointer
- **CPU/GPU:** CPU only
- **Memory:** `boneBuffer` allocated from FrameArena (zero-alloc after first
  frame if arena has capacity). For 256 joints: 256 * 64 bytes = 16 KB.

### Phase 7 -- Transform System

**Source:** `engine/scene/TransformSystem.cpp`

`TransformSystem::update(reg)`:
1. Iterates all entities with `TransformComponent`.
2. Skips non-root entities (those with `HierarchyComponent`).
3. For each root: if dirty flag set (bit 0 of `tc.flags`) or
   `WorldTransformComponent` missing:
   a. Composes local TRS: `translate * rotate * scale`.
   b. Writes result to `WorldTransformComponent::matrix`.
   c. Clears the dirty flag.
   d. Recursively updates children via `updateChildren()`, propagating the
      parent world matrix.
4. Even clean roots recurse into children (a descendant may be independently
   dirty -- e.g., physics wrote back a new position).

- **Reads:** `TransformComponent` (position, rotation, scale, flags),
  `HierarchyComponent`, `ChildrenComponent`
- **Writes:** `WorldTransformComponent::matrix`, `TransformComponent::flags`
  (clears dirty)
- **CPU/GPU:** CPU only
- **Memory:** Zero allocations (all in-place ECS writes)

### Phase 8 -- Physics System

**Source:** `engine/physics/PhysicsSystem.cpp`

`PhysicsSystem::update(reg, physics, deltaTime)`:
1. **Register new bodies:** Finds entities with `(RigidBodyComponent,
   ColliderComponent)` but no `PhysicsBodyCreatedTag`. Creates Jolt bodies
   using world-space position/rotation from `WorldTransformComponent`.
2. **Sync kinematic bodies:** For each kinematic body, reads
   `WorldTransformComponent`, decomposes to position+rotation, calls
   `physics.moveKinematic()`.
3. **Step physics:** `physics.step(deltaTime, kMaxSubSteps=4)` with fixed
   timestep of 1/60s. Jolt runs collision detection and constraint solving.
4. **Write back dynamic bodies:** For each dynamic body, reads the new
   position/rotation from Jolt. If the entity has a parent hierarchy, converts
   world-space to local-space via inverse parent matrix. Writes to
   `TransformComponent` and **sets the dirty flag** (bit 0) so
   TransformSystem picks it up next frame.
5. **Cleanup destroyed bodies:** Removes Jolt bodies for entities that no
   longer exist in the registry.

- **Reads:** `WorldTransformComponent`, `RigidBodyComponent`,
  `ColliderComponent`, `HierarchyComponent`
- **Writes:** `TransformComponent` (position, rotation, dirty flag),
  `PhysicsBodyCreatedTag` (emplace), Jolt internal state
- **CPU/GPU:** CPU only (Jolt is CPU-side)
- **Memory:** `InlinedVector<EntityID, 16>` for batch collection (stack-
  allocated for <= 16 entities, heap fallback for more)

### Phase 9 -- Light Cluster Build

**Source:** `engine/rendering/LightClusterBuilder.cpp`

`LightClusterBuilder::update(reg, viewMatrix, projMatrix, near, far, w, h)`:
1. **collectLights:** Iterates `(WorldTransformComponent, PointLightComponent)`
   and `(WorldTransformComponent, SpotLightComponent)`. Transforms positions
   to view space. Sorts spot-before-point, then by distance. Caps at 256
   lights.
2. **buildClusters:** Partitions the frustum into a 16x9x24 grid (3,456
   clusters) using exponential (log-depth) Z slicing. For each cluster,
   computes a view-space AABB by unprojecting NDC tile corners. Tests every
   light sphere against the AABB. Two-pass: count pass (prefix sum for
   offsets), then fill pass (writes light indices).
3. **uploadTextures:** Packs data into three textures via `bgfx::updateTexture2D`:
   - `s_lightData`: 256x4 RGBA32F (position/radius, color/type, spotDir/cos, padding)
   - `s_lightGrid`: 3456x1 RGBA32F (offset, count per cluster)
   - `s_lightIndex`: 8192x1 R32F (flat light index array)

- **Reads:** `WorldTransformComponent`, `PointLightComponent`,
  `SpotLightComponent`, view/projection matrices
- **Writes:** Three GPU textures
- **CPU/GPU:** CPU computation, GPU texture upload
- **Memory:** All fixed-size arrays reused across frames (zero heap
  allocation): `lights_[256]`, `grid_[3456]`, `indices_[8192]`

### Phase 10 -- Frustum Cull + Shadow Cull

**Source:** `engine/rendering/systems/FrustumCullSystem.cpp`,
`engine/rendering/systems/ShadowCullSystem.cpp`

**FrustumCullSystem::update(reg, res, frustum):**
- For each entity with `(WorldTransformComponent, MeshComponent)`:
  - Computes a conservative world-space AABB from the mesh's local bounds,
    transformed by the absolute rotation columns of the world matrix.
  - Tests against the camera frustum (6-plane test via `Frustum::containsAABB`).
  - Adds `VisibleTag` if inside, removes if outside.

**ShadowCullSystem::update(reg, res, shadowFrustum, cascadeIndex):**
- Same AABB computation as frustum cull.
- Tests against the light-space frustum for the given cascade.
- Sets/clears bits in `ShadowVisibleTag::cascadeMask` (one bit per cascade).

- **Reads:** `WorldTransformComponent`, `MeshComponent` (for AABB bounds),
  camera/light frustum planes
- **Writes:** `VisibleTag` (emplace/remove), `ShadowVisibleTag`
  (emplace/remove/mask update)
- **CPU/GPU:** CPU only
- **Memory:** Zero allocations (tag components are zero-size or 1 byte)

### Phase 11 -- Shadow Draw Calls

**Source:** `engine/rendering/systems/DrawCallBuildSystem.cpp:243-330`

For each shadow cascade (typically 1, up to 4):
1. `ShadowRenderer::beginCascade(i, lightView, lightProj)` configures the
   bgfx view: binds the per-cascade framebuffer (tile within the 2048x2048
   atlas), sets viewport, clears depth to 1.0, uploads light matrices.
2. `DrawCallBuildSystem::submitShadowDrawCalls()` iterates entities with
   `(ShadowVisibleTag, WorldTransformComponent, MeshComponent)`:
   - Skips entities with `SkinComponent` (handled separately).
   - Checks `cascadeMask` bit for this cascade.
   - Submits depth-only draw: position stream only, no surface attributes.
   - State: `WRITE_Z | DEPTH_TEST_LESS | CULL_CW`.
3. `DrawCallBuildSystem::submitSkinnedShadowDrawCalls()` does the same for
   skinned entities, uploading bone matrices via `bgfx::setTransform()` with
   `count = boneCount`.

- **Reads:** `ShadowVisibleTag`, `WorldTransformComponent`, `MeshComponent`,
  `SkinComponent`, bone buffer
- **Writes:** bgfx command buffer (draw calls on shadow views 0-7)
- **CPU/GPU:** CPU submits; GPU executes later
- **GPU View IDs:** `kViewShadowBase + cascadeIndex` (views 0-7)
- **Shader:** `vs_shadow` / `fs_shadow` (depth-only, no fragment output)

### Phase 12 -- Opaque Draw Calls (PBR)

**Source:** `engine/rendering/systems/DrawCallBuildSystem.cpp:120-443`

`Renderer::beginFrameDirect()` or `Renderer::beginFrame()` configures
`kViewOpaque` (view 9) to render either directly to the backbuffer or to the
HDR scene framebuffer (for post-processing).

The application sets up the `RenderPass(kViewOpaque)` with camera view/proj
matrices and clear color, then calls:

1. **Static PBR pass** (`DrawCallBuildSystem::update` with `PbrFrameParams`):
   Iterates `(VisibleTag, WorldTransformComponent, MeshComponent, MaterialComponent)`,
   skipping entities with `SkinComponent`. For each entity:
   - Uploads per-draw uniforms: `u_material` (albedo, roughness, metallic,
     emissiveScale), `u_dirLight`, `u_shadowMatrix`, `u_frameParams`,
     `u_lightParams`, `u_iblParams`.
   - Binds texture slots 0-8, 12-14: albedo, normal, ORM, emissive,
     occlusion, shadowMap, irradiance, prefiltered, brdfLut, lightData,
     lightGrid, lightIndex.
   - Submits to `kViewOpaque` (view 9) with `BGFX_STATE_DEFAULT`.

2. **Skinned PBR pass** (`DrawCallBuildSystem::updateSkinned`):
   Iterates `(VisibleTag, SkinComponent, MeshComponent, MaterialComponent)`.
   Same uniform/texture setup. Additionally:
   - Uploads bone matrices via `bgfx::setTransform(&bones[0], boneCount)`.
   - Binds skinning vertex stream (stream 2: joint indices + weights).
   - Submits to `kViewOpaque` with the skinned PBR program.

- **Reads:** `VisibleTag`, `WorldTransformComponent`, `MeshComponent`,
  `MaterialComponent`, `SkinComponent`, bone buffer, `Material`, `Mesh`,
  all texture handles
- **Writes:** bgfx command buffer (draw calls on view 9)
- **CPU/GPU:** CPU submits; GPU executes later
- **GPU View ID:** `kViewOpaque` (9)
- **Shaders:** `vs_pbr`/`fs_pbr` (static), `vs_skinned_pbr`/`fs_pbr` (skinned)
- **Texture Slots:** 15 total (0-8 material/IBL, 5 shadow, 12-14 cluster)

### Phase 13 -- Post-Process Chain

**Source:** `engine/rendering/systems/PostProcessSystem.cpp`

`PostProcessSystem::submit(settings, uniforms, firstViewId)` (views 16+):

All passes use a fullscreen triangle (3 vertices, clip-space) submitted as a
single draw call per pass.

| Sub-pass           | Input Texture(s)       | Output              | View ID |
|--------------------|------------------------|---------------------|---------|
| SSAO (optional)    | Scene depth            | ssaoMap_ (R8)       | 16      |
| Bloom threshold    | HDR color              | bloomLevel[0]       | 17      |
| Bloom downsample   | bloomLevel[i-1]        | bloomLevel[i]       | 18-21   |
| Bloom upsample     | bloomLevel[i] + [i-1]  | bloomLevel[i-1]     | 22-25   |
| Tonemap            | HDR color + bloom[0]   | LDR FB (or backbuf) | 26      |
| FXAA (optional)    | LDR color              | Backbuffer          | 27      |

Bloom uses 5 downsample steps (full res down to 1/32), then 4 upsample steps
back to full res. Total bloom views: 1 (threshold) + 4 (down) + 4 (up) = 9.

- **Reads:** HDR scene color, scene depth, bloom mip chain
- **Writes:** Bloom mip textures, LDR framebuffer, backbuffer
- **CPU/GPU:** CPU submits fullscreen triangles; GPU executes shader passes
- **Shaders:** `vs_fullscreen` paired with `fs_bloom_threshold`,
  `fs_bloom_downsample`, `fs_bloom_upsample`, `fs_tonemap`, `fs_fxaa`,
  `fs_ssao`

### Phase 14-16 -- Present (Engine::endFrame)

**Source:** `Engine::endFrame()` in `engine/core/Engine.cpp:244-252`

1. `imguiEndFrame()` -- Finalises ImGui draw lists and submits them to
   `kViewImGui` (view 15).
2. `frameArena_->reset()` -- Resets the monotonic buffer resource. O(1):
   just moves the pointer back to the start. All arena-allocated memory
   (e.g., animation bone buffers) is instantly reclaimed. No destructors.
3. `renderer_.endFrame()` -> `bgfx::frame()` -- Submits all recorded draw
   calls to the GPU. On Metal (macOS), this encodes a command buffer and
   commits it. Returns immediately; the GPU executes asynchronously.
   With `BGFX_RESET_VSYNC`, presentation waits for the next display refresh.

- **CPU/GPU:** CPU finalises; GPU begins executing the full frame
- **Memory:** Frame arena reset to zero used bytes

---

## 3. Data Flow Diagram

```
                    ECS Registry (Central Data Store)
                    ==================================
                    TransformComponent (local TRS + dirty flag)
                    WorldTransformComponent (world Mat4)
                    MeshComponent (mesh handle)
                    MaterialComponent (material handle)
                    SkeletonComponent, AnimatorComponent, SkinComponent
                    RigidBodyComponent, ColliderComponent
                    PointLightComponent, SpotLightComponent
                    AudioListenerComponent, AudioSourceComponent
                    VisibleTag, ShadowVisibleTag
                    HierarchyComponent, ChildrenComponent

  +-----------+     +-------------+     +-----------+
  |  Input    |---->| Game Logic  |---->| Animation |
  |  System   |     | (app code)  |     |  System   |
  +-----------+     +-------------+     +-----------+
  Writes:            Writes:             Reads: AnimatorComp, Skeleton
  InputState         TransformComp       Writes: AnimatorComp (time),
                     (dirty flag)               SkinComp (offsets),
                                                boneBuffer (arena)
                                                    |
                                                    v
  +-----------+     +-----------+        +-----------+
  |  Physics  |<----|Transform  |<-------|           |
  |  System   |     |  System   |        |           |
  +-----------+     +-----------+        |           |
  Reads: WorldTransform  Reads: TransformComp        |
  Writes: TransformComp  Writes: WorldTransformComp  |
  (pos, rot, dirty)      (clears dirty flag)         |
        |                       |                    |
        v                       v                    v
  +-----------+     +-----------+     +--------------------+
  |  Audio    |     | Frustum   |     |  Light Cluster     |
  |  System   |     | Cull      |     |  Builder           |
  +-----------+     +-----------+     +--------------------+
  Reads: WorldTransform  Reads: WorldTransform  Reads: WorldTransform,
         AudioComps             MeshComp (AABB)        PointLight, SpotLight
  Writes: IAudioEngine   Writes: VisibleTag     Writes: 3 GPU textures
                                |                   (lightData/Grid/Index)
                                v
                         +-----------+
                         | Shadow    |
                         | Cull      |
                         +-----------+
                         Reads: WorldTransform, MeshComp
                         Writes: ShadowVisibleTag
                                |
                                v
                    +------------------------+
                    |  DrawCallBuildSystem    |
                    +------------------------+
                    Reads: VisibleTag, ShadowVisibleTag,
                           WorldTransform, MeshComp,
                           MaterialComp, SkinComp,
                           boneBuffer, Material, Mesh,
                           all textures
                    Writes: bgfx command buffer
                           (shadow views 0-7, opaque view 9)
                                |
                                v
                    +------------------------+
                    |  PostProcessSystem     |
                    +------------------------+
                    Reads: HDR scene FB, depth, bloom mips
                    Writes: Bloom textures, LDR FB, backbuffer
                    (views 16-27)
                                |
                                v
                    +------------------------+
                    |  bgfx::frame()         |
                    +------------------------+
                    GPU executes all views in ID order
```

---

## 4. GPU Pipeline

### View Ordering (bgfx view IDs)

bgfx executes views in ascending ID order. The Nimbus layout
(`engine/rendering/ViewIds.h`):

```
View ID   Pass                    Target                  State
-------   ----                    ------                  -----
0-7       Shadow maps             Shadow atlas (D32F)     Depth write, CULL_CW
8         Depth prepass           (reserved, future)      --
9         Opaque (Forward+ PBR)  HDR scene FB (RGBA16F)  Default (depth+color)
10        Transparent             HDR scene FB            (reserved, future)
14        UI / 3D sprites         (reserved)              --
15        ImGui overlay           Backbuffer              Color write
16+       Post-process chain      Various FBs -> backbuf  Color write only
```

### Shader Stages

**Shadow pass (views 0-7):**
- Vertex: `vs_shadow` -- transforms position by `u_model * u_viewProj`
  (or `vs_skinned_shadow` with bone palette)
- Fragment: `fs_shadow` -- no color output, depth-only

**PBR pass (view 9):**
- Vertex: `vs_pbr` -- outputs world position, TBN basis, UV.
  Skinned variant (`vs_skinned_pbr`) applies bone transforms.
- Fragment: `fs_pbr` -- Forward+ PBR:
  1. Sample albedo, normal, ORM, emissive textures
  2. Reconstruct TBN normal map
  3. Compute cluster index from `gl_FragCoord` (16x9x24 grid)
  4. Loop over lights in cluster (from `s_lightGrid`/`s_lightIndex`/`s_lightData`)
  5. Cook-Torrance BRDF (GGX NDF, Smith-GGX geometry, Fresnel-Schlick)
  6. Directional light + shadow (PCF from `s_shadowMap`)
  7. IBL ambient (irradiance + prefiltered env + BRDF LUT)
  8. Output linear HDR color

**Post-process passes (views 16+):**
- All use `vs_fullscreen` (passthrough clip-space triangle -> UV)
- `fs_bloom_threshold`: extract bright pixels above threshold
- `fs_bloom_downsample`: 13-tap bilinear downsample (Kawase-like)
- `fs_bloom_upsample`: additive upsample + blend with coarser level
- `fs_tonemap`: ACES tonemap + gamma correction + bloom composite
- `fs_fxaa`: FXAA 3.11 (edge detection + sub-pixel anti-aliasing)
- `fs_ssao`: hemisphere sampling from depth buffer, outputs R8 occlusion

### Texture Slot Map (PBR pass)

```
Slot  Sampler          Content                   Format
----  -------          -------                   ------
0     s_albedo         Base color map             RGBA8/sRGB
1     s_normal         Normal map (tangent-space) RGBA8
2     s_orm            Occlusion/Roughness/Metal  RGBA8
3     s_emissive       Emissive map               RGBA8/sRGB
4     s_occlusion      Ambient occlusion map      RGBA8
5     s_shadowMap      Shadow atlas               D32F
6     s_irradiance     Diffuse IBL cubemap        RGBA16F
7     s_prefiltered    Specular IBL cubemap        RGBA16F
8     s_brdfLut        BRDF integration LUT       RG16F
12    s_lightData      Light data texture          RGBA32F
13    s_lightGrid      Cluster grid texture        RGBA32F
14    s_lightIndex     Light index texture          R32F
```

---

## 5. Memory Budget Per Frame

### Frame Arena

Configured at init: `EngineDesc::frameArenaSize = 2 MB` (default).

The `FrameArena` is a `std::pmr::monotonic_buffer_resource` backed by a
2 MB pre-allocated buffer. It provides O(1) bump allocation and O(1) reset
(pointer reset at `endFrame`, no destructors called).

**Per-frame arena consumers:**
- AnimationSystem bone buffer: `jointCount * sizeof(Mat4)` per skinned entity
  - Example: 1 entity with 64 joints = 64 * 64 = 4,096 bytes
  - Example: 10 entities with 128 joints each = 81,920 bytes (80 KB)
- Future: frustum cull results, draw call sorting keys

**Arena utilisation** is displayed in the HUD:
`Arena: XX KB / 2048 KB`

### Zero-Allocation Systems

These systems allocate nothing per frame (all data is in ECS component
arrays or fixed-size buffers):

| System              | Memory Strategy                                |
|---------------------|------------------------------------------------|
| TransformSystem     | In-place writes to WorldTransformComponent     |
| FrustumCullSystem   | Emplace/remove 1-byte VisibleTag               |
| ShadowCullSystem    | Emplace/remove 1-byte ShadowVisibleTag         |
| LightClusterBuilder | Fixed arrays: lights_[256], grid_[3456], indices_[8192] |
| InputSystem         | Reuses persistent eventBuf_ vector             |

### PhysicsSystem

Uses `InlinedVector<EntityID, 16>` -- stack-allocated for up to 16 entities,
falls back to heap for larger batches. In steady state (no new entities being
registered), this is effectively zero-allocation.

### GPU Memory Per Frame

| Resource               | Size                      | Lifetime      |
|------------------------|---------------------------|---------------|
| Shadow atlas           | 2048x2048 D32F = 16 MB    | Persistent    |
| HDR scene FB           | WxH RGBA16F = ~7 MB @1080p| Persistent    |
| Bloom mip chain (5 lvl)| ~2.3 MB @1080p            | Persistent    |
| LDR FB (for FXAA)      | WxH RGBA8 = ~8 MB @1080p  | Persistent    |
| SSAO map               | WxH R8 = ~2 MB @1080p     | Persistent    |
| Light data texture     | 256x4 RGBA32F = 16 KB     | Persistent    |
| Light grid texture     | 3456x1 RGBA32F = 55 KB    | Persistent    |
| Light index texture    | 8192x1 R32F = 32 KB       | Persistent    |
| Bone matrices (transient)| Per-draw via setTransform | Per-frame     |

All framebuffer textures are allocated once and reused. Only the light
cluster textures are re-uploaded each frame (via `bgfx::updateTexture2D`
with `bgfx::copy`, which allocates a transient copy in bgfx's internal
allocator).

---

## 6. Threading Model

### Current: Single-Threaded Main Loop

The Nimbus engine currently runs the entire frame on a single thread:

```
Main Thread:
  beginFrame()          -- OS events, input, ImGui
  [app simulation]      -- animation, transform, physics, audio
  [app rendering]       -- cull, light cluster, draw call submission
  endFrame()            -- ImGui end, arena reset, bgfx::frame()
```

bgfx is initialised in **single-threaded mode** (see `Renderer::init`:
`bgfx::renderFrame()` is called before `bgfx::init()` to prevent bgfx from
spawning its own render thread). All `bgfx::submit()` calls happen on the
main thread; GPU work is dispatched at `bgfx::frame()`.

The only parallelism today is **asset loading**: the `ThreadPool` (2 worker
threads in the animation demo) runs file I/O and glTF parsing off the main
thread. GPU resource creation (`bgfx::create*`) must still happen on the
main thread, deferred to `assets.processUploads()`.

### Future: DAG-Based Parallel Frame

The existing architecture is designed for future parallelism:

1. **System dependencies form a DAG:**
   ```
   Input ─────────────────────────┐
   Animation ──> Transform ──┐    │
                              ├──> FrustumCull ──> DrawCallBuild
   Physics ──> Transform* ───┘    │
                                  ├──> ShadowCull ──> ShadowDrawCalls
   Audio (independent) ──────────┘
   LightCluster (independent) ───────> DrawCallBuild
   ```
   (* Physics writes dirty flags; Transform propagates them. In a parallel
   schedule, Physics and Animation could run concurrently since they write
   to disjoint entity sets.)

2. **ECS component access patterns enable safe parallelism:**
   - Each system declares its read/write component sets.
   - Systems that access disjoint component types can run concurrently.
   - Example: AudioSystem (reads WorldTransform + AudioComponents) can run
     in parallel with AnimationSystem (reads/writes AnimatorComponent +
     SkinComponent).

3. **The ThreadPool already exists** (`engine/threading/ThreadPool.h`) and
   supports `submit()` + `waitAll()`. A future scheduler would:
   a. Build the system DAG at startup.
   b. Each frame, submit ready systems (no unsatisfied dependencies) to the
      pool.
   c. As systems complete, unlock dependent systems.
   d. `waitAll()` before `endFrame()`.

4. **Memory considerations for parallel execution:**
   - The FrameArena is not thread-safe (monotonic_buffer_resource is not).
     Each worker thread would need its own arena, or a synchronized arena.
   - ECS Registry operations (emplace/remove) are not thread-safe. Deferred
     command buffers would be needed for structural changes (adding/removing
     components).
   - Fixed-size arrays in LightClusterBuilder are already thread-safe
     (single writer).

5. **bgfx constraint:** All `bgfx::submit()` calls must happen on the main
   thread (or on the thread that called `bgfx::init()`). Draw call building
   could be parallelised by collecting draw commands into thread-local
   buffers, then replaying them on the main thread.

---

## Appendix: Component Sizes

| Component                  | Size (bytes) | Alignment |
|----------------------------|-------------|-----------|
| TransformComponent         | 44          | 4         |
| WorldTransformComponent    | 64          | 16 (Mat4) |
| MeshComponent              | 4           | 4         |
| MaterialComponent          | 4           | 4         |
| VisibleTag                 | 1           | 1         |
| ShadowVisibleTag           | 1           | 1         |
| CameraComponent            | 20          | 4         |
| PointLightComponent        | 20          | 4         |
| SpotLightComponent         | 40          | 4         |
| DirectionalLightComponent  | 32          | 4         |
| InstancedMeshComponent     | 12          | 4         |

All component structs use explicit padding and `static_assert` on size and
offset to prevent silent layout changes. Fields are ordered
largest-alignment-first to minimise implicit padding.
```

---

### Critical Files for Implementation

- /Users/shayanj/claude/engine/engine/core/Engine.cpp
- /Users/shayanj/claude/engine/engine/rendering/ViewIds.h
- /Users/shayanj/claude/engine/engine/rendering/systems/DrawCallBuildSystem.cpp
- /Users/shayanj/claude/engine/engine/rendering/systems/PostProcessSystem.cpp
- /Users/shayanj/claude/engine/apps/animation_demo/main.mm