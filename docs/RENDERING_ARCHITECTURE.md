# Rendering Architecture

The renderer sits on top of **bgfx** (Metal + Vulkan backends only) and is driven by the ECS. It is not a self-contained subsystem — it is a set of systems and components that live in the same registry as everything else. The work is split across several focused systems that execute in ECS phases: transforms are updated, then visibility is culled and lights are assigned in parallel, then draw calls are recorded in parallel using bgfx encoders, then post-processing runs sequentially. bgfx's internal render thread consumes the resulting command buffer independently.

---

## Design Principles

- **ECS-driven.** Everything renderable is an entity with components. There is no separate scene representation.
- **Forward+.** Tiled clustered forward shading. Handles many dynamic lights, supports transparent objects naturally, works on mobile.
- **No render graph (yet).** Passes are a hardcoded linear pipeline. A proper render graph is the natural next step when the pipeline grows beyond ~6 passes — noted as a future upgrade.
- **PBR materials.** Physically-based rendering (albedo, roughness, metallic, normal, AO). One material format engine-wide.
- **bgfx views = passes.** Each render pass is a bgfx view. View IDs are fixed constants so pass ordering is explicit and easy to change.
- **Assets behind handles.** All GPU resources (meshes, textures, materials, shaders) are referenced by a typed `AssetHandle<T>`. The `RenderResources` registry maps handles to live bgfx handles.
- **Two levels of parallelism.** System-level: non-conflicting render systems run concurrently via the DAG scheduler. Data-level: individual systems (frustum cull, draw call recording) split their work across thread pool workers. bgfx encoders allow multiple threads to record draw calls simultaneously.
- **Configurable quality.** Every feature is individually toggleable and every scalable parameter (shadow resolution, cascade count, light budget, post-process quality) lives in a single `RenderSettings` struct. Systems read settings — nothing is hardcoded. Presets provide sensible per-platform defaults; games can override any value.

---

## System Overview

The render workload is split across five systems that map onto ECS phases. The DAG scheduler serializes phases with data dependencies and runs independent systems within a phase in parallel.

```mermaid
graph TD
    subgraph "Phase N — Scene Graph"
        TS["TransformSystem\nWrites: WorldTransformComponent"]
    end

    subgraph "Phase N+1 — Parallel cull + light prep"
        FCS["FrustumCullSystem\nReads: WorldTransform, Mesh\nWrites: VisibleTag"]
        LCS["LightCullSystem\nReads: LightComponents, CameraComponent\nWrites: LightClusterBuffer"]
    end

    subgraph "Phase N+2 — Parallel shadow cull"
        SCS["ShadowCullSystem\nReads: WorldTransform, VisibleTag\nWrites: ShadowVisibleTag\n(one task per cascade / spot light)"]
    end

    subgraph "Phase N+3 — Parallel draw call recording"
        DCBS["DrawCallBuildSystem\nReads: Visible, WorldTransform,\nMesh, Material, LightClusterBuffer\nWorkers record via bgfx encoders"]
        IBBS["InstanceBufferBuildSystem\nReads: InstancedMeshComponent,\nWorldTransform, VisibleTag\nBuilds per-group instance buffers"]
        UIS["UiRenderSystem\nReads: SpriteComponent\nView 5 — independent of 3D views"]
    end

    subgraph "Phase N+4 — Sequential"
        PPS["PostProcessSystem\nFull-screen passes — each depends\non previous pass output"]
    end

    subgraph "Main thread only"
        FRAME["bgfx::frame()\nFlushes all encoder command buffers\nto bgfx internal render thread"]
    end

    TS --> FCS
    TS --> LCS
    FCS --> SCS
    LCS --> SCS
    SCS --> DCBS
    SCS --> IBBS
    DCBS --> PPS
    IBBS --> PPS
    PPS --> FRAME
    UIS --> FRAME
```

`FrustumCullSystem` and `LightCullSystem` have disjoint Writes so the DAG scheduler places them in the same phase and runs them concurrently. `UiRenderSystem` targets a different bgfx view and has no data conflict with `PostProcessSystem`, so they also run in parallel.

Within `FrustumCullSystem` and `DrawCallBuildSystem`, the thread pool is used for data-level parallelism — entity lists are partitioned across workers for culling and draw call encoding respectively.

---

## ECS Components

Every component is laid out to eliminate implicit padding. Fields are ordered to satisfy alignment naturally (largest alignment first, small fields grouped at the end). `static_assert` on `sizeof` and `offsetof` catches accidental regressions at compile time.

`AssetHandle<T>` is a `uint32_t` — 4 bytes, supports 4 billion unique assets.

### Tag Components

Tag components carry no data — entity presence in the SparseSet is the signal. The SparseSet specialises for empty types and allocates no dense array, so storing a tag on 50k entities costs only the sparse index array (50k × 4 bytes = 200 KB), not a dense data array.

```cpp
// Presence = visible to the main camera. Added/removed by FrustumCullSystem each frame.
struct VisibleTag {};
static_assert(sizeof(VisibleTag) == 1);

// cascadeMask: bit N set = entity is visible to shadow cascade N (up to 8 cascades).
// Set by ShadowCullSystem. DrawCallBuildSystem tests the relevant bit before recording
// a shadow draw call, avoiding work for entities not visible to a given cascade.
struct ShadowVisibleTag
{
    uint8_t cascadeMask;
};
static_assert(sizeof(ShadowVisibleTag) == 1);
```

### Camera

`ProjectionType` uses an explicit `uint8_t` underlying type. Without it the compiler defaults to `int` (4 bytes), creating a 3-byte hole between the floats and `viewLayer`.

```cpp
enum class ProjectionType : uint8_t { Perspective, Orthographic };

struct CameraComponent              // offset  size
{
    float          fovY;            //  0       4
    float          nearPlane;       //  4       4
    float          farPlane;        //  8       4
    float          aspectRatio;     // 12       4
    ProjectionType type;            // 16       1
    uint8_t        viewLayer;       // 17       1
    uint8_t        _pad[2];         // 18       2  (explicit — documents intent)
};                                  // total:  20 bytes
static_assert(sizeof(CameraComponent)         == 20);
static_assert(offsetof(CameraComponent, type) == 16);
```

### Mesh & Material

Handle-only components — one `uint32_t` each. Deliberately split so `FrustumCullSystem` can query `MeshComponent` for AABB bounds without touching `MaterialComponent`.

```cpp
struct MeshComponent     { uint32_t mesh;     }; // 4 bytes
struct MaterialComponent { uint32_t material; }; // 4 bytes
static_assert(sizeof(MeshComponent)     == 4);
static_assert(sizeof(MaterialComponent) == 4);
```

### Lights

`bool` has size 1 but alignment 1, so `bool castShadows` at offset 28 leaves 3 bytes of implicit padding to reach the next 4-byte boundary. Replacing it with `uint8_t flags` makes the padding explicit and reserves the remaining 7 bits for future toggles (receive shadows, affect specular, etc.).

```cpp
// Directional — one per scene (sun).
struct DirectionalLightComponent        // offset  size
{
    Vec3    direction;                  //  0      12
    Vec3    color;                      // 12      12
    float   intensity;                  // 24       4
    uint8_t flags;                      // 28       1  bit 0: castShadows
    uint8_t _pad[3];                    // 29       3
};                                      // total:  32 bytes
static_assert(sizeof(DirectionalLightComponent) == 32);

// Point — no angular attenuation.
struct PointLightComponent              // offset  size
{
    Vec3  color;                        //  0      12
    float intensity;                    // 12       4
    float radius;                       // 16       4
};                                      // total:  20 bytes
static_assert(sizeof(PointLightComponent) == 20);

// Spot — stores cos(innerAngle) and cos(outerAngle), not the raw angles.
// The shader computes dot(lightDir, fragDir) and compares to the precomputed cosines.
// Storing raw angles would require acos() per pixel on every lit fragment — eliminated here.
struct SpotLightComponent               // offset  size
{
    Vec3    direction;                  //  0      12
    Vec3    color;                      // 12      12
    float   intensity;                  // 24       4
    float   cosInnerAngle;              // 28       4  precomputed: cos(innerAngle)
    float   cosOuterAngle;              // 32       4  precomputed: cos(outerAngle)
    float   radius;                     // 36       4
};                                      // total:  40 bytes
static_assert(sizeof(SpotLightComponent) == 40);
```

### Instanced Mesh

```cpp
struct InstancedMeshComponent           // offset  size
{
    uint32_t mesh;                      //  0       4
    uint32_t material;                  //  4       4
    uint32_t instanceGroupId;           //  8       4
};                                      // total:  12 bytes
static_assert(sizeof(InstancedMeshComponent) == 12);
```

### Scene Graph (TransformSystem — not rendering-specific but used by all render systems)

`WorldTransformComponent` is 64 bytes / 16-byte aligned — one `Mat4`. This is the single hot read for `DrawCallBuildSystem`; keeping it at 64 bytes means exactly one cache line per entity's world transform.

```cpp
// Local TRS — written by game code, read by TransformSystem.
// dirty flag in bit 0 of flags; set when position/rotation/scale changes.
struct TransformComponent               // offset  size
{
    Vec3    position;                   //  0      12
    Quat    rotation;                   // 12      16
    Vec3    scale;                      // 28      12
    uint8_t flags;                      // 40       1  bit 0: dirty
    uint8_t _pad[3];                    // 41       3
};                                      // total:  44 bytes
static_assert(sizeof(TransformComponent)             == 44);
static_assert(offsetof(TransformComponent, rotation) == 12);

// World matrix — written by TransformSystem, read by all render systems.
// Mat4 is 16-byte aligned — SIMD-friendly, one cache line per entity.
struct WorldTransformComponent
{
    Mat4 matrix;                        // 64 bytes, offset 0
};
static_assert(sizeof(WorldTransformComponent) == 64);
```

---

**Renderable entity** = `MeshComponent` + `MaterialComponent` + `WorldTransformComponent`.
**Camera entity** = `CameraComponent` + `WorldTransformComponent`.
**Light entity** = one of the light components.

`WorldTransformComponent` is written by `TransformSystem`. All render systems read it — no transform logic lives in the renderer.

---

## Asset Types

```mermaid
classDiagram
    class Mesh {
        bgfx::VertexBufferHandle positionVbh
        bgfx::VertexBufferHandle surfaceVbh
        bgfx::IndexBufferHandle ibh
        AABB bounds
        uint32_t vertexCount
        uint32_t indexCount
    }

    class Material {
        AssetHandle~Shader~ shader
        Vec4 albedo
        float roughness
        float metallic
        AssetHandle~Texture~ albedoMap
        AssetHandle~Texture~ normalMap
        AssetHandle~Texture~ ormMap
        BlendMode blendMode
    }

    class Texture {
        bgfx::TextureHandle handle
        uint16_t width
        uint16_t height
        TextureFormat format
    }

    class Shader {
        bgfx::ProgramHandle program
        ShaderType type
    }
```

All assets are loaded through the `AssetSystem` (streaming / asset cache layer). The `RenderResources` registry holds the live bgfx handles; assets themselves store metadata only.

---

## GPU Data Layout

Everything the GPU touches is laid out to minimise bandwidth: vertices are split into two streams so depth-only passes skip surface data entirely, attributes are packed to the smallest accurate representation, index buffers default to 16-bit, and texture formats are selected per-platform from `GpuFeatures` at load time.

### Vertex Streams

Meshes use **two vertex buffers** bound as separate streams. Depth-only passes (shadow maps, depth prepass) bind only Stream 0. The surface stream never enters the vertex fetch cache for those passes.

```
Stream 0 — Position only
┌──────────────────────────┐
│ float px, py, pz         │  12 bytes / vertex
└──────────────────────────┘

Stream 1 — Surface attributes
┌────────────────────────────────────────────────────────┐
│ snorm16 normalX, normalY  │  4 bytes  oct-encoded normal│
│ snorm8  tanX, tanY, tanZ  │           oct-encoded tangent│
│ snorm8  tanSign           │  4 bytes  bitangent handedness│
│ float16 u, v              │  4 bytes  UV coordinates    │
└────────────────────────────────────────────────────────┘
                               12 bytes / vertex
```

**Total: 24 bytes/vertex.** Naïve layout (float3 pos + float3 normal + float4 tangent + float2 UV) = 48 bytes/vertex — a 50% reduction in vertex fetch bandwidth.

**Normal/tangent encoding (oct):** A unit vector maps onto an octahedron, flattened to a square in [-1, 1]². Two `snorm16` values encode the normal; the shader reconstructs Z with `sqrt(1 - dot(xy, xy))` and a sign flip. Tangent is encoded the same way with an extra `snorm8` for bitangent handedness. Precision loss is visually imperceptible for normal maps.

**UV precision:** `float16` gives ~3 decimal digits of precision — sufficient for UV coordinates in the range [0, 1] and for typical 0–8× tiling. If a mesh needs sub-texel precision at high tiling factors, the importer falls back to `float32` for that mesh's Stream 1.

**bgfx vertex layout declarations:**

```cpp
// engine/rendering/VertexLayouts.h

inline bgfx::VertexLayout positionLayout()
{
    bgfx::VertexLayout l;
    l.begin()
     .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
     .end();
    return l;  // stride: 12 bytes
}

inline bgfx::VertexLayout surfaceLayout(const GpuFeatures& gpu)
{
    bgfx::VertexLayout l;
    l.begin()
     .add(bgfx::Attrib::Normal,    2, bgfx::AttribType::Int16, /*normalized=*/true)
     .add(bgfx::Attrib::Tangent,   4, bgfx::AttribType::Int8,  /*normalized=*/true)
     .add(bgfx::Attrib::TexCoord0, 2,
          gpu.halfPrecisionAttribs ? bgfx::AttribType::Half : bgfx::AttribType::Float)
     .end();
    return l;  // stride: 12 bytes (half UV) or 16 bytes (float UV fallback)
}
```

The `GpuFeatures` check on UV type is the only per-platform branch in the vertex layout. All other attributes are universally supported by Metal and Vulkan.

**Skinned meshes** add a third stream (Stream 2) — bone indices (`uint8_t × 4`) and bone weights (`unorm8 × 4`) — 8 bytes/vertex. Bound only for skinned draw calls; static geometry never pays for it.

### Index Buffers

16-bit indices (65 535 max vertices per mesh) are the default — half the memory and bandwidth of 32-bit. The importer checks vertex count at build time and emits `BGFX_BUFFER_INDEX_32` only when the mesh genuinely exceeds 65 535 vertices. Most game meshes don't.

```cpp
const bool needs32bit = vertexCount > 65535u;
const uint16_t flags  = needs32bit ? BGFX_BUFFER_INDEX_32 : BGFX_BUFFER_NONE;
mesh.ibh = bgfx::createIndexBuffer(mem, flags);
```

### Mesh Optimisation Pipeline

Applied offline by the asset importer (not at runtime) using **meshoptimizer** (~100 KB, MIT):

```
Raw mesh (from glTF / FBX)
        │
        ▼
1. Weld duplicate vertices
        │
        ▼
2. Vertex cache optimisation   — Forsyth reorder, maximises post-transform cache hits
        │
        ▼
3. Overdraw optimisation       — reorder triangles to reduce pixel overdraw (threshold: 1.05)
        │
        ▼
4. Vertex fetch optimisation   — remap vertices to match draw order, maximises prefetch
        │
        ▼
5. Attribute packing           — encode normals/tangents to oct, UVs to float16
        │
        ▼
6. Meshlet generation          — compute meshlets for future GPU-driven culling (stored, unused now)
        │
        ▼
Packed binary mesh file (assets/meshes/*.nmesh)
```

Steps 2–4 are three orthogonal passes in meshoptimizer (`optimizeVertexCache`, `optimizeOverdraw`, `optimizeVertexFetch`). Running all three takes ~10 ms per mesh on a desktop — fine for offline import, never acceptable at runtime.

### Uniform and Constant Buffer Packing

bgfx uses `Vec4` as the minimum uniform unit — a single `float` still occupies a full 16-byte slot. Packing rules:

**Per-frame constants** (set once per frame, shared across all draw calls in a view):

```
u_frameParams [Vec4 × 2]
  .xy  = viewport size (pixels)
  .zw  = { near, far }
  [1].xyzw = time, deltaTime, 0, 0
```

**Per-material constants** (set once per material batch):

```
u_material [Vec4 × 2]
  [0].rgb  = albedo
  [0].a    = roughness
  [1].r    = metallic
  [1].gba  = emissive RGB
```

**Light data** (uploaded as a `Vec4` array before the opaque pass, indexed in the cluster list):

```
u_lights [Vec4 × 3 per light, max 256 lights]
  [0].xyz  = world position (point/spot) or direction (directional)
  [0].w    = radius
  [1].rgb  = colour × intensity (pre-multiplied)
  [1].w    = type (0=point, 1=spot, 2=directional)
  [2].xyz  = spot direction
  [2].w    = cos(outerAngle)  ← matches SpotLightComponent precomputation
```

Keeping the `cos(outerAngle)` from `SpotLightComponent` already precomputed means the CPU just copies it directly into the uniform array — no per-frame trig.

### Texture Compression

Textures are compressed offline at import time. The engine selects the right binary at load time based on `GpuFeatures`:

| Texture role | Mac / iOS (Metal) | Windows (Vulkan) | Android modern | Android legacy |
|---|---|---|---|---|
| Albedo (sRGB) | ASTC 4×4 | BC7 | ASTC 4×4 | ETC2 RGB8 |
| Normal map (linear, 2-channel) | ASTC 4×4 | BC5 | ASTC 4×4 | ETC2 RG11 |
| ORM (occlusion/roughness/metallic) | ASTC 4×4 | BC7 | ASTC 4×4 | ETC2 RGB8 |
| HDR environment (linear float) | ASTC HDR 6×6 | BC6H | ASTC HDR 6×6 | RGBA16F (uncompressed) |

BC5 for normal maps stores only RG and lets the shader reconstruct B — better quality per byte than BC7 for 2-channel data.

Each texture asset ships with multiple compressed variants in the same `.ntex` file. The asset loader reads `GpuFeatures.preferredTextureFormat` and picks the matching variant.

### GPU Feature Detection

`GpuFeatures` is populated once at startup from `bgfx::getCaps()` and passed by `const` reference wherever rendering decisions depend on hardware capability.

```cpp
// engine/rendering/GpuFeatures.h

struct GpuFeatures
{
    // Vertex attributes
    bool halfPrecisionAttribs;   // AttribType::Half supported (float16 UVs)
    bool uint10Attribs;          // AttribType::Uint10 supported (RGB10A2 — alternative packing)

    // Texture formats
    bool textureBC;              // BC1–BC7 (desktop)
    bool textureASTC;            // ASTC LDR + HDR (Apple, modern Android)
    bool textureETC2;            // ETC2 (Android fallback)
    bool textureShadowCompare;   // BGFX_CAPS_TEXTURE_COMPARE_LEQUAL — PCF shadow sampling
    bool textureBC6H;            // BC6H — HDR environment maps (desktop)

    // GPU architecture
    bool isTBDR;                 // Tile-Based Deferred Renderer (all mobile GPUs, Apple Silicon)
                                 // Drives: depth prepass disabled, sort key priority flip,
                                 // transient attachment hints, SSAO off by default

    // Rendering features
    bool computeShaders;         // BGFX_CAPS_COMPUTE — GPU-driven culling path (future)
    bool indirectDraw;           // BGFX_CAPS_DRAW_INDIRECT — GPU-driven instances (future)
    bool instancing;             // BGFX_CAPS_INSTANCING — hardware instancing

    // Limits
    uint32_t maxTextureSize;     // caps shadow map and environment map resolution
    uint32_t maxDrawCalls;       // hard per-frame budget reported by bgfx

    // Preferred compressed format for this device (resolved at startup)
    enum class TextureFormat : uint8_t { ASTC, BC, ETC2, Uncompressed } preferredTextureFormat;

    static GpuFeatures query();  // calls bgfx::getCaps() and populates the struct
};
```

`RenderSettings::platformDefault()` reads `GpuFeatures` when constructing defaults — e.g., `maxTextureSize` caps `ShadowSettings::directionalRes` so a device that only supports 2048² textures never tries to allocate a 4096² shadow map.

---

## Mobile GPU Architecture (TBDR)

All mobile GPUs — ARM Mali (Pixel 7: Mali-G710), Qualcomm Adreno, Apple A-series, PowerVR — use **Tile-Based Deferred Rendering (TBDR)**. Desktop GPUs use **Immediate Mode Rendering (IMR)**. The two architectures have opposite relationships with several pipeline techniques.

### TBDR vs IMR

```mermaid
graph LR
    subgraph "IMR (Desktop — Nvidia, AMD)"
        V["Vertex shader"] --> R["Rasterise"] --> F["Fragment shader"] --> FB["Framebuffer\n(main VRAM)"]
    end

    subgraph "TBDR (Mobile — Mali, Adreno, Apple)"
        V2["Vertex shader\n(all geometry)"] --> BIN["Tile binning\n(sort geometry per tile)"]
        BIN --> HSR["HSR / FPK\n(kill occluded fragments\nbefore fragment shader)"]
        HSR --> F2["Fragment shader\n(visible fragments only)"]
        F2 --> TILE["On-chip tile SRAM\n(colour + depth + stencil)"]
        TILE -->|"resolve at tile end"| FB2["Framebuffer\n(main memory)"]
    end
```

**Hidden Surface Removal (HSR) / Forward Pixel Kill (FPK):** The hardware sorts geometry within each tile and kills fragments that are provably occluded *before* running the fragment shader. This is the hardware equivalent of what a depth prepass provides on IMR — but it costs nothing and requires no extra draw calls.

### Depth Prepass on TBDR — Harmful, Not Helpful

A depth prepass on TBDR:

1. **Doubles vertex work.** Every vertex is processed twice — depth-only pass and main pass.
2. **Forces a tile resolve.** The depth buffer in tile SRAM must flush to main memory after the prepass so the main pass can read it back. This is exactly the expensive operation TBDR is designed to avoid.
3. **Wastes the HSR.** TBDR kills occluded fragments for free. The prepass replicates this at CPU-visible cost.

**`depthPrepassEnabled` defaults to `false` on all TBDR platforms** (iOS, Android) and `true` on IMR (Windows, Mac desktop with discrete GPU). `GpuFeatures.isTBDR` drives this in `RenderSettings::platformDefault()`.

**Exception — alpha-tested geometry only:** TBDR's HSR cannot pre-kill alpha-tested fragments (foliage, fences, hair) because the GPU doesn't know visibility until the alpha test runs in the shader. A narrow depth-only prepass covering masked geometry *only* can still help on TBDR for scenes with heavy foliage. This is an optional pass, off by default, toggled by `depthPrepassAlphaTestedOnly` in `RenderSettings`.

### Sort Key Priority Flip

Without a depth prepass, front-to-back sort order is the primary input to TBDR's HSR. The opaque sort key priority flips on mobile:

| Platform | Sort key priority |
|---|---|
| IMR (desktop) | shader ID → texture set → depth front-to-back |
| TBDR (mobile) | depth front-to-back → shader ID → texture set |

### Transient Attachments

On TBDR, attachments that are written in one pass and only read in the same pass never need to touch main memory. Marking them as transient prevents the resolve. bgfx exposes this via `BGFX_TEXTURE_RT_WRITE_ONLY` for depth/stencil and via the Metal backend's `MTLStoreActionDontCare` path.

Depth buffer after the opaque pass: if SSAO is disabled (default on mobile), the depth buffer is never read by post-processing. Mark it transient — the tile resolve is skipped entirely.

### SSAO on TBDR

SSAO needs to sample the depth buffer from the previous pass. On IMR, depth is already in GPU memory — free. On TBDR, it forces a tile resolve (flush depth SRAM to main memory) before SSAO can sample it. This is why SSAO defaults off on mobile — the bandwidth cost exists even before the ALU cost.

The proper solution is **Vulkan subpasses**: a subsequent pass reads the previous pass's tile output directly from on-chip SRAM, no resolve required. bgfx does not expose Vulkan subpasses (see bgfx limitations below). Until it does, SSAO on mobile requires a full resolve and is only worth enabling on high-end devices that can absorb the bandwidth cost.

### Platform Map

| Device | GPU | Architecture | Depth prepass | SSAO default |
|---|---|---|---|---|
| Pixel 7 | Mali-G710 (ARM) | TBDR | off | off |
| Pixel 8 Pro | Tensor G3 / Mali-G715 | TBDR | off | off |
| iPhone 15 | Apple A16 | TBDR | off | off |
| Samsung S24 | Adreno 750 | TBDR | off | off |
| Mac (M-series) | Apple GPU | TBDR | off | configurable |
| Mac (Intel/AMD discrete) | IMR | IMR | on | on |
| Windows PC | Nvidia / AMD | IMR | on | on |

Apple Silicon Macs are TBDR. `isTBDR` is set based on the active Metal device type (`MTLGPUFamilyApple*` vs `MTLGPUFamilyMac*`) — not platform alone.

---

## bgfx Limitations and Modification Assessment

bgfx covers everything needed for the base architecture without modification. Three features would require forking bgfx and are deferred until profiling justifies the cost:

| Feature | Status | Notes |
|---|---|---|
| Packed vertex attributes (half, snorm16, snorm8) | **Supported natively** | `AttribType::Half`, `Int16`, `Int8` — no changes needed |
| 16-bit index buffers | **Supported natively** | `BGFX_BUFFER_NONE` default |
| Per-GPU feature caps | **Supported natively** | `bgfx::getCaps()` covers all needed flags |
| Multi-stream vertex buffers | **Supported natively** | Multiple `bgfx::setVertexBuffer` calls per draw |
| Compressed texture formats | **Supported natively** | `BGFX_TEXTURE_FORMAT_BC*`, `ASTC*`, `ETC2*` |
| Transient attachments (`WRITE_ONLY`) | **Supported natively** | `BGFX_TEXTURE_RT_WRITE_ONLY` → `DontCare` store on Metal/Vulkan |
| **Vulkan subpasses** | **Not exposed** | Needed for efficient SSAO and post-effects on TBDR — reading previous pass output from tile SRAM without a resolve. Workaround: SSAO off by default on mobile; accept resolve cost on high-end mobile if enabled. Trigger: mobile post-effects become a shipping priority |
| **Bindless textures** | **Not supported** | Would need bgfx fork + Vulkan `VK_EXT_descriptor_indexing` + Metal argument buffers. Workaround: sort draw calls by material (already in sort key). Trigger: profiling shows material rebinding is a CPU bottleneck |
| **Push constants / root constants** | **Not exposed** | bgfx uses UBO path for all uniforms. Push constants would reduce per-draw CPU overhead on mobile. Trigger: profiling shows uniform binding is a bottleneck at high draw counts |
| GPU-driven indirect draw with variable count | **Partial** | `BGFX_CAPS_DRAW_INDIRECT` exists; `drawIndirectCount` (variable GPU-generated count) not supported. Needed for full GPU-driven culling. Trigger: instance count exceeds ~100k |

**No bgfx modifications are needed to ship the base renderer.** The Vulkan subpass gap is the most relevant for mobile quality; the bindless and push-constant gaps only matter at draw/material counts well above the initial target.

---

## Render Pipeline

**Desktop (IMR):** full 6-pass pipeline.
**Mobile (TBDR):** depth prepass removed; TBDR's HSR replaces it at no cost.

```mermaid
graph LR
    V0["View 0\nShadow Maps\n(directional + spot)"]
    V1["View 1\nDepth Prepass\nIMR only\n(disabled on TBDR)"]
    V2["View 2\nOpaque Pass\nForward+ PBR"]
    V3["View 3\nTransparency Pass\nback-to-front sorted"]
    V4["View 4\nPost-Processing\nbloom · SSAO · tonemap · FXAA"]
    V5["View 5\nUI / HUD\northographic · sprites · ImGui"]

    V0 --> V1 --> V2 --> V3 --> V4 --> V5
    V0 -.->|"TBDR: skip V1"| V2
```

### View 0 — Shadow Maps

- Skipped entirely if `ShadowSettings.enabled = false`.
- One shadow map per directional light (CSM), one per active spot light.
- Resolution: `ShadowSettings.directionalRes` × `ShadowSettings.directionalRes` for directional (default 2048²), `ShadowSettings.spotRes` for spot (default 1024²). Both are power-of-two and configurable at runtime.
- Cascade count: `ShadowSettings.cascadeCount` (1–4, default 3).
- Depth-only render: no color attachment.
- Output: shadow map textures sampled in the opaque pass.

### View 1 — Depth Prepass (IMR / desktop only)

- **Disabled on all TBDR platforms** (mobile, Apple Silicon Mac). TBDR's HSR/FPK provides the same overdraw elimination in hardware at no cost; a prepass would double vertex work and force an expensive tile resolve.
- On IMR (discrete GPU desktop), renders only opaque geometry to pre-populate the depth buffer so early-Z kills occluded fragments in the main pass.
- Controlled by `RenderSettings.depthPrepassEnabled`, set automatically by `platformDefault()` via `GpuFeatures.isTBDR`.
- **Exception — `depthPrepassAlphaTestedOnly`:** An optional narrow prepass covering only alpha-tested geometry (foliage, fences) can help even on TBDR, since HSR cannot pre-kill masked fragments. Off by default; enable per-game if foliage overdraw is measured as a bottleneck.

### View 2 — Opaque Pass (Forward+)

- All opaque, non-instanced geometry.
- Light data passed as a uniform buffer (point/spot lights, up to 256 active lights).
- Clustered culling: view frustum divided into a 3D grid of tiles; each tile knows which lights affect it. Computed once on CPU per frame before submission.
- Reads shadow maps from View 0 for shadow-receiving surfaces.
- PBR shader: GGX specular, Lambert diffuse, IBL ambient, shadow PCF.

### View 3 — Transparency Pass

- Alpha-blended and alpha-tested geometry.
- Sorted back-to-front by depth (camera distance) each frame.
- No depth writes. Reads depth buffer from View 2 (opaque pass) for soft particle depth tests. On TBDR, this read forces a tile resolve if the depth attachment wasn't already stored — mark the depth attachment as non-transient if transparency is in use.

### View 4 — Post-Processing Chain

Each post effect is a full-screen quad blit in its own sub-pass. Each is independently toggled by `PostProcessSettings`:

| Effect | Controlled by | Default (desktop / mobile) |
|---|---|---|
| SSAO | `ssao.enabled`, `ssao.sampleCount` | on 16 samples / off |
| Bloom | `bloom.enabled`, `bloom.intensity`, `bloom.downsampleSteps` | on / off |
| Tonemapping | `toneMapper`, `exposure` | ACES, 1.0 / same |
| FXAA | `fxaaEnabled` | on / on |

### View 5 — UI / HUD

- Orthographic projection. All 2D sprites and UI panels render here.
- ImGui rendered last in this pass (editor mode) or suppressed (shipped game).
- Depth test disabled — UI always draws on top.

---

## bgfx Integration

```mermaid
sequenceDiagram
    participant Main
    participant Workers
    participant bgfx
    participant RenderThread as bgfx Render Thread

    Main->>bgfx: bgfx::init(Metal/Vulkan, windowHandle)

    loop Every frame
        Main->>bgfx: setViewRect / setViewClear / touch (all views)
        Note over Main: Phase N+1: FrustumCullSystem + LightCullSystem (parallel)
        Note over Main: Phase N+2: ShadowCullSystem (parallel per cascade)

        Main->>Workers: Phase N+3: dispatch draw call workers
        Workers->>bgfx: bgfx::begin(true) → Encoder per worker
        Workers->>bgfx: encoder→setTransform / setVertexBuffer / submit (views 0–3)
        Workers->>bgfx: bgfx::end(encoder)
        Note over Workers: UiRenderSystem runs in parallel (view 5)
        Main->>Workers: join all workers

        Main->>bgfx: Phase N+4: PostProcessSystem (views 4, main thread)
        Main->>bgfx: bgfx::frame()
        bgfx->>RenderThread: flush command buffer → Metal / Vulkan draw calls
    end

    Main->>bgfx: bgfx::shutdown()
```

### bgfx Encoder Rules

bgfx encoders allow worker threads to record draw calls concurrently into separate command buffers that bgfx merges before `bgfx::frame()`.

| Operation | Thread | Reason |
|---|---|---|
| `bgfx::init` / `bgfx::shutdown` | Main only | One-time lifecycle |
| `bgfx::frame()` | Main only | Synchronization point |
| `bgfx::setViewRect` / `setViewClear` / `touch` | Main only | View state is global |
| GPU resource creation (`createVertexBuffer`, `createTexture`, …) | Main only | bgfx resource API not thread-safe |
| `bgfx::begin(true)` / encoder calls / `bgfx::end` | Any worker thread | Safe — each encoder is independent |
| Post-process full-screen submits (views 4) | Main thread encoder | Sequential dependency on prior passes |

---

## Threading Model

### Two Levels of Parallelism

```mermaid
graph LR
    subgraph "Level 1 — System-level (DAG scheduler)"
        FCS2["FrustumCullSystem"]
        LCS2["LightCullSystem"]
        FCS2 ~~~ LCS2
        Note1["Same phase → run concurrently\n(disjoint Writes)"]
    end

    subgraph "Level 2 — Data-level (thread pool within a system)"
        W1["Worker 0\nentities 0..12k\nbgfx::Encoder A"]
        W2["Worker 1\nentities 12k..24k\nbgfx::Encoder B"]
        W3["Worker 2\nentities 24k..36k\nbgfx::Encoder C"]
        W4["Worker 3\nentities 36k..50k\nbgfx::Encoder D"]
    end

    FCS2 --> W1
    FCS2 --> W2
    FCS2 --> W3
    FCS2 --> W4
```

**Level 1** is handled by the DAG scheduler automatically from the Reads/Writes declarations on each system. No manual coordination needed — systems with disjoint writes land in the same phase and run on separate thread pool threads.

**Level 2** is explicit within a system. `FrustumCullSystem` and `DrawCallBuildSystem` partition their entity lists into N chunks (N = thread count) and submit tasks to the `ThreadPool`. Each draw call worker holds its own `bgfx::Encoder` for the duration of its task.

### What Is and Isn't Parallelized

| Work | Parallel? | How |
|---|---|---|
| Frustum cull (main camera) | Yes | Data-parallel — entity list split across workers |
| Shadow frustum cull | Yes | One task per cascade + spot light (system-level) |
| Clustered light list building | Yes | Data-parallel — tile grid split across workers |
| Instance buffer construction | Yes | One task per instance group |
| Draw call recording (opaque, shadow, transparent) | Yes | N workers, each with a bgfx::Encoder |
| Post-processing passes | No | Each pass reads the previous pass output |
| UI / sprite rendering | Yes (with post-process) | Different bgfx view — no data conflict |
| GPU resource upload (mesh, texture) | No | bgfx resource API is main-thread only; use staging queue |
| bgfx::frame() | No | Main-thread synchronization point |

### Staging Queue for Async Asset Upload

GPU resource creation (`bgfx::createVertexBuffer`, `bgfx::createTexture`) is main-thread only. When the asset system finishes loading a mesh or texture on a background thread, it pushes a `PendingUpload` to a thread-safe queue. At the start of each frame (before view setup), the main thread drains this queue and calls the bgfx create functions. This keeps worker threads free of main-thread-only bgfx calls while still feeding the GPU promptly.

### Platform-specific init:

| Platform | Backend | Window handle |
|---|---|---|
| Mac | Metal | `NSWindow*` (wrapped in `bgfx::PlatformData`) |
| iOS | Metal | `CAMetalLayer*` |
| Windows | Vulkan | `HWND` |
| Android | Vulkan | `ANativeWindow*` |

---

## Shader Pipeline

bgfx uses **shaderc** — shaders are written in a GLSL-like dialect and compiled offline to Metal Shader Language (Metal) or SPIR-V (Vulkan). No runtime compilation.

```
engine/rendering/shaders/
├── common/
│   ├── uniforms.sh       — shared uniform declarations
│   └── pbr.sh            — PBR lighting functions (GGX, Lambert, IBL)
├── pbr/
│   ├── vs_pbr.sc         — PBR vertex shader
│   └── fs_pbr.sc         — PBR fragment shader
├── shadow/
│   ├── vs_shadow.sc      — depth-only vertex shader
│   └── fs_shadow.sc      — depth-only fragment shader (empty)
├── post/
│   ├── vs_fullscreen.sc  — full-screen triangle vertex shader
│   ├── fs_bloom.sc
│   ├── fs_ssao.sc
│   ├── fs_tonemap.sc
│   └── fs_fxaa.sc
└── ui/
    ├── vs_sprite.sc
    └── fs_sprite.sc
```

**Build step:** A CMake custom command runs shaderc on all `.sc` files and outputs compiled shader binaries into `assets/shaders/`. The engine loads these binaries at startup via the asset system. Shaders are never compiled at runtime.

**Platform targets compiled per shader:**
- `--platform osx --type metal` → `.bin` for Metal
- `--platform android --type spirv` → `.bin` for Vulkan

Both are packaged and the engine selects the correct binary based on the active bgfx backend.

---

## GPU Instancing

For vegetation, foliage, rocks, and other high-count repeated meshes. A single draw call renders thousands of instances.

```mermaid
graph TD
    Entities["Entities with\nInstancedMeshComponent\n(groupId, mesh, material)"]
    Collect["RenderSystem: collect all\ninstanced entities by groupId"]
    IB["Build InstanceBuffer\nper groupId:\nworld matrices + per-instance data"]
    Submit["bgfx::setInstanceDataBuffer\nbgfx::submit (one call per group)"]

    Entities --> Collect
    Collect --> IB
    IB --> Submit
```

- Instances are grouped by `(mesh, material)` pair — one draw call per unique pair.
- The instance buffer is rebuilt from ECS data each frame (dynamic, CPU-side).
- Buffer construction is parallel: `InstanceBufferBuildSystem` submits one thread pool task per group — groups are fully independent.
- Once GPU-driven culling is needed (1M+ instances), this becomes a compute shader — noted as future work.

---

## Render Settings & Quality Scaling

All rendering behaviour is controlled by a single `RenderSettings` struct passed by `const` reference to every render system. Nothing in the renderer is hardcoded. Features can be toggled per-frame; scalable parameters take effect on the next frame with no GPU work except where noted.

### The RenderSettings Struct

```cpp
// engine/rendering/RenderSettings.h

enum class ShadowFilter { Hard, PCF4x4, PCF8x8 };
enum class ToneMapper   { ACES, Reinhard, Uncharted2 };

struct ShadowSettings
{
    bool     enabled              = true;
    uint16_t directionalRes       = 2048;  // power of two: 512 / 1024 / 2048 / 4096
    uint16_t spotRes              = 1024;
    uint8_t  cascadeCount         = 3;     // 1 – 4
    float    maxDistance          = 150.0f;
    ShadowFilter filter           = ShadowFilter::PCF4x4;
};

struct SsaoSettings
{
    bool    enabled     = true;
    float   radius      = 0.5f;
    float   bias        = 0.025f;
    uint8_t sampleCount = 16;   // 8 / 16 / 32
};

struct BloomSettings
{
    bool    enabled        = true;
    float   threshold      = 1.0f;
    float   intensity      = 0.04f;
    uint8_t downsampleSteps = 5;   // 3 / 4 / 5
};

struct PostProcessSettings
{
    SsaoSettings  ssao;
    BloomSettings bloom;
    bool          fxaaEnabled  = true;
    ToneMapper    toneMapper   = ToneMapper::ACES;
    float         exposure     = 1.0f;
};

struct LightingSettings
{
    uint16_t maxActiveLights = 256;   // capped by cluster uniform buffer size
    bool     iblEnabled      = true;
};

struct RenderSettings
{
    ShadowSettings      shadows;
    LightingSettings    lighting;
    PostProcessSettings postProcess;
    bool                depthPrepassEnabled          = true;  // false on TBDR via platformDefault()
    bool                depthPrepassAlphaTestedOnly  = false; // TBDR exception: depth-only pass for
                                                              // masked geometry only (foliage, fences)
    uint8_t             anisotropicFiltering         = 8;   // 1 / 4 / 8 / 16
    float               renderScale                  = 1.0f; // 0.5 – 1.0 (internal resolution)

    // Factory: returns appropriate defaults for the current platform.
    static RenderSettings platformDefault();
};
```

Sub-structs group related knobs so systems only take the slice they need (e.g. `ShadowCullSystem` takes `const ShadowSettings&`).

### Quality Presets

Presets are plain factory functions returning a `RenderSettings` value. They are not an enum the struct carries — the game starts from a preset and overrides individual fields freely.

```cpp
namespace RenderPresets
{
    RenderSettings ultraDesktop();    // 4096 shadows, 4 cascades, PCF8x8, SSAO 32 samples
    RenderSettings highDesktop();     // 2048 shadows, 3 cascades, PCF4x4, SSAO 16 samples
    RenderSettings mediumDesktop();   // 1024 shadows, 2 cascades, PCF4x4, SSAO off
    RenderSettings lowDesktop();      // 512 shadows,  1 cascade,  Hard,   SSAO off, no bloom
    RenderSettings highMobile();      // 1024 shadows, 2 cascades, Hard,   SSAO off
    RenderSettings mediumMobile();    // 512 shadows,  1 cascade,  Hard,   SSAO off, bloom off
    RenderSettings lowMobile();       // shadows off, SSAO off, bloom off, render scale 0.75
}
```

**Platform defaults** (returned by `RenderSettings::platformDefault()`):

| Platform | Default preset | Rationale |
|---|---|---|
| Mac (desktop) | `highDesktop` | Metal, capable GPU |
| Windows | `highDesktop` | Vulkan, capable GPU |
| iOS | `highMobile` | Metal, thermal constraints |
| Android | `mediumMobile` | Wide hardware variance; conservative default |

Games can override at startup and expose a quality menu to players:

```cpp
RenderSettings settings = RenderSettings::platformDefault();
settings.shadows.directionalRes = 1024;  // this game has close-range combat, lower res is fine
settings.postProcess.bloom.intensity = 0.08f;
renderer.applySettings(settings);
```

### How Systems Consume Settings

Each system receives a `const RenderSettings&` (or the relevant sub-struct) on its `update()` call. The struct is owned by the `Renderer` and does not live in the ECS.

```mermaid
graph LR
    RS["RenderSettings\n(owned by Renderer)"]
    SCS2["ShadowCullSystem\nconst ShadowSettings&"]
    DCBS2["DrawCallBuildSystem\nconst RenderSettings&"]
    PPS2["PostProcessSystem\nconst PostProcessSettings&"]
    LCS2["LightCullSystem\nconst LightingSettings&"]

    RS --> SCS2
    RS --> DCBS2
    RS --> PPS2
    RS --> LCS2
```

If a feature is disabled, the system that owns it short-circuits immediately and does no work — no special-casing at the call site.

### Settings Changes at Runtime

Most changes take effect on the next frame with no GPU work. Two settings require GPU resource re-allocation and trigger a brief one-frame stall:

| Setting changed | GPU impact | Handled by |
|---|---|---|
| `shadows.directionalRes` / `spotRes` | Shadow map textures must be re-created | `RenderResources` detects size mismatch, destroys and recreates on next frame |
| `renderScale` | Internal framebuffer must be re-created | Same — framebuffer resize |
| All other settings | No GPU work | Take effect next `update()` call |

Re-allocations are done at the start of the frame before any views are set up, so the frame that applies the change renders correctly.

### Serialization

`RenderSettings` serializes to / from JSON so settings can be saved to a player config file and reloaded on next launch. JSON parser to be decided under the external library policy (nlohmann/json or rapidjson — see Scene Graph section in NOTES.md). Binary format is not needed here — settings files are small and only read at startup.

---

## Lighting Model

**Ambient:** Image-based lighting (IBL) — a prefiltered environment cubemap + BRDF LUT. One IBL per scene; blended between two IBLs in transition zones.

**Directional:** Sun light. One per scene. Casts cascaded shadow maps. Cascade count and shadow distance come from `ShadowSettings` — 1–4 cascades, configurable split lambda.

**Point / Spot:** Dynamic, clustered. Active light budget set by `LightingSettings.maxActiveLights` (default 256, lower on mobile). The clustered grid is 16×9×24 tiles (matches 16:9 viewport, 24 depth slices). Per-tile light lists are computed on CPU and uploaded as a uniform buffer before the opaque pass.

**Emissive:** Materials can have an emissive color + intensity. No light is emitted (no lightmaps), purely a visual additive contribution.

**Future — lightmaps / light probes:** Static geometry can be baked offline and stored as light probe volumes. Not in scope for the initial renderer.

---

## Post-Processing

```mermaid
graph LR
    Opaque["Opaque\ncolor buffer"]
    SSAO["SSAO\n(desktop only)\nambient occlusion\nfrom depth+normals"]
    Bloom["Bloom\nthreshold → 5x\ndownsample → upsample\n→ composite"]
    Tonemap["Tonemapping\nACES filmic\n+ exposure control"]
    FXAA["FXAA\npost-resolve\nanti-aliasing"]
    Out["Final\nframebuffer"]

    Opaque --> SSAO --> Bloom --> Tonemap --> FXAA --> Out
```

Each effect is a full-screen pass. `PostProcessSystem` skips any disabled pass entirely — no GPU draw call is issued. The in-game visualiser can toggle passes live via `RenderSettings` without restarting.

---

## 2D / UI Layer (View 5)

Rendered last, on top of everything. Orthographic projection — no depth test.

**Sprites:** A sprite is a quad mesh (two triangles) with a texture region. The `SpriteComponent` holds a texture handle + UV rect. `SpriteRenderSystem` batches all sprites sharing the same texture atlas into a single draw call per atlas.

**ImGui:** Rendered in View 5 in editor mode. In shipped builds, the ImGui render step is compiled out (`#ifndef NIMBUS_EDITOR`).

---

## RenderResources Registry

Maps typed asset handles to live bgfx handles. Owns all GPU resource lifetime.

```cpp
// engine/rendering/RenderResources.h
class RenderResources
{
public:
    bgfx::VertexBufferHandle  getMesh(AssetHandle<Mesh>) const noexcept;
    bgfx::TextureHandle       getTexture(AssetHandle<Texture>) const noexcept;
    bgfx::ProgramHandle       getShader(AssetHandle<Shader>) const noexcept;

    void uploadMesh(AssetHandle<Mesh>, const MeshData&);
    void uploadTexture(AssetHandle<Texture>, const TextureData&);
    void uploadShader(AssetHandle<Shader>, const ShaderData&);

    void release(AssetHandle<Mesh>);
    void release(AssetHandle<Texture>);
    void release(AssetHandle<Shader>);
};
```

Called by the asset system when a mesh/texture/shader finishes loading. Release called when the asset is evicted from the cache.

---

## Draw Call Sort Order

Sort keys are packed into bgfx's 64-bit `submit()` sort key directly.

**Opaque pass — sort key priority differs by GPU architecture:**

| Bit field (MSB → LSB) | IMR (desktop) | TBDR (mobile) |
|---|---|---|
| Highest | Shader program ID | Depth front-to-back |
| Middle | Texture set hash | Shader program ID |
| Lowest | Depth front-to-back | Texture set hash |

On IMR, minimising shader/texture switches is most important because the depth prepass already handles overdraw. On TBDR, front-to-back order is the primary input to HSR/FPK — submitting geometry front-to-back lets the hardware kill more occluded fragments early. Shader switches are secondary because TBDR processes geometry in tile bins where each tile is small enough that switch cost is lower.

`DrawCallBuildSystem` reads `GpuFeatures.isTBDR` once at startup and packs sort keys with the appropriate field order.

**Transparency pass sort key:**
1. Depth (back-to-front) — required for correct alpha blending, same on all platforms.

---

## Profiling

The profiling system has two tiers. Tier 0 is always present with effectively zero overhead — bgfx view names and `bgfx::Stats` give per-pass timing with no instrumentation code in hot paths. Tier 1 is opt-in for profiling builds and enables deep CPU+GPU tracing via **Tracy**, which integrates directly with bgfx's callback interface to get per-draw-call GPU timing without manual instrumentation.

### Strategy Overview

```
Tier 0 — always on, zero overhead in release
    bgfx view names          → pass labels in RenderDoc, Instruments, Android GPU Inspector
    bgfx::Stats (post-frame) → per-view CPU+GPU time, draw counts → in-game visualiser

Tier 1 — profiling builds only (NIMBUS_PROFILING=ON)
    NIMBUS_CPU_ZONE macros   → CPU timeline in Tracy viewer
    Tracy bgfx callback      → per-draw GPU timeline in Tracy, sourced from bgfx profiler API
    NIMBUS_GPU_ZONE macros   → explicit GPU zones for sub-pass work not covered by bgfx
```

### CPU Zone Macros

All engine systems instrument their work with zone macros. In non-profiling builds these expand to nothing — no overhead, no binary size cost.

```cpp
// engine/profiling/Profiler.h

#ifdef NIMBUS_PROFILING
  #include <tracy/Tracy.hpp>
  #define NIMBUS_FRAME_MARK         FrameMark
  #define NIMBUS_CPU_ZONE(name)     ZoneScopedN(name)
  #define NIMBUS_CPU_ZONE_COLOR(name, color) ZoneScopedNC(name, color)
#else
  #define NIMBUS_FRAME_MARK
  #define NIMBUS_CPU_ZONE(name)
  #define NIMBUS_CPU_ZONE_COLOR(name, color)
#endif
```

Every render system uses these at the start of its `update()`:

```cpp
void FrustumCullSystem::update(Registry& reg, float dt)
{
    NIMBUS_CPU_ZONE("FrustumCull");
    // worker tasks also zone themselves:
    pool.submit([chunk] {
        NIMBUS_CPU_ZONE("FrustumCull::worker");
        // ...
    });
}

void DrawCallBuildSystem::update(Registry& reg, float dt)
{
    NIMBUS_CPU_ZONE("DrawCallBuild");
    // ...
}

void PostProcessSystem::update(Registry& reg, float dt)
{
    NIMBUS_CPU_ZONE("PostProcess");
    { NIMBUS_CPU_ZONE("PostProcess::SSAO");  submitSsao();  }
    { NIMBUS_CPU_ZONE("PostProcess::Bloom"); submitBloom(); }
    { NIMBUS_CPU_ZONE("PostProcess::Tonemap+FXAA"); submitTonemapFxaa(); }
}
```

Thread pool workers inherit the Tracy fiber context automatically, so the CPU timeline shows which thread executed each worker task.

### GPU Timing — bgfx::Stats (Tier 0)

After every `bgfx::frame()`, `bgfx::getStats()` returns `bgfx::Stats` containing per-view (per-pass) GPU and CPU timing. No instrumentation required — bgfx records these automatically.

```cpp
// Called once per frame after bgfx::frame(), feeds the in-game visualiser.
void Renderer::readStats()
{
    const bgfx::Stats* s = bgfx::getStats();

    // Per-frame totals
    const double gpuMs = 1000.0 * double(s->gpuTimeEnd - s->gpuTimeBegin)
                       / double(s->gpuTimerFreq);
    const double cpuMs = 1000.0 * double(s->cpuTimeEnd - s->cpuTimeBegin)
                       / double(s->cpuTimerFreq);

    // Per-pass breakdown (one entry per bgfx view)
    for (uint16_t i = 0; i < s->numViews; ++i)
    {
        const bgfx::ViewStats& v = s->viewStats[i];
        const double passGpuMs = 1000.0 * double(v.gpuTimeEnd - v.gpuTimeBegin)
                               / double(s->gpuTimerFreq);
        const double passCpuMs = 1000.0 * double(v.cpuTimeEnd - v.cpuTimeBegin)
                               / double(s->cpuTimerFreq);
        // v.name is the string set by bgfx::setViewName()
        // v.numDraw, v.numPrims also available
    }

    // Resource counts
    // s->numDraw, s->numCompute, s->numPrims[]
    // s->textureMemoryUsed, s->rtMemoryUsed
}
```

**What bgfx::Stats covers per view:**

| Field | Description |
|---|---|
| `gpuTimeBegin` / `gpuTimeEnd` | GPU start/end timestamp for the entire pass |
| `cpuTimeBegin` / `cpuTimeEnd` | CPU time spent encoding commands for the pass |
| `numDraw` | Draw call count |
| `numPrims[topology]` | Primitive count by topology type |

### GPU Timing — Tracy (Tier 1)

Tracy integrates with bgfx via `bgfx::CallbackI` — bgfx calls `profilerBegin` / `profilerEnd` internally for every region it profiles, and Tracy's implementation records these as GPU zones on the Tracy timeline. This gives sub-pass GPU timing (e.g. individual draw call batches) without any per-draw instrumentation in engine code.

```cpp
// engine/profiling/TracyBgfxCallback.h  (profiling builds only)
#ifdef NIMBUS_PROFILING
#include <tracy/TracyBgfx.hpp>

// Pass to bgfx::init() in profiling builds.
// Tracy handles the rest — records GPU timestamps for every bgfx profiler region.
struct TracyBgfxCallback : public bgfx::CallbackI { /* ... */ };
#endif
```

```cpp
// Renderer::init()
bgfx::Init init;
init.type     = bgfx::RendererType::Vulkan; // or Metal
init.vendorId = BGFX_PCI_ID_NONE;
#ifdef NIMBUS_PROFILING
init.callback = &m_tracyCallback;  // Tracy hooks in here
#endif
bgfx::init(init);
```

With this in place, the Tracy desktop viewer shows:
- **CPU timeline**: all `NIMBUS_CPU_ZONE` zones across all threads, including thread pool workers
- **GPU timeline**: per-pass zones from bgfx's internal profiler regions, aligned with the CPU timeline
- **Frame markers**: frame boundaries from `NIMBUS_FRAME_MARK`
- **Memory**: optional heap tracking via Tracy's overridden `new`/`delete`

### Debug Markers and View Names (Tier 0)

View names set via `bgfx::setViewName()` appear as pass labels in every GPU debugger and profiler — RenderDoc, Apple Instruments, Android GPU Inspector, and the Tracy GPU timeline. Set once at startup:

```cpp
bgfx::setViewName(kViewShadow,       "Shadow Maps");
bgfx::setViewName(kViewDepthPrepass, "Depth Prepass");
bgfx::setViewName(kViewOpaque,       "Opaque Forward+");
bgfx::setViewName(kViewTransparent,  "Transparency");
bgfx::setViewName(kViewPostProcess,  "Post-Process");
bgfx::setViewName(kViewUi,           "UI / HUD");
```

`bgfx::setMarker(label)` adds a point marker visible in RenderDoc and Instruments — used to label individual draw call groups within a pass (e.g. "Instanced vegetation", "Skinned characters") for easier GPU capture navigation:

```cpp
bgfx::setMarker("Vegetation instances");
bgfx::submit(kViewOpaque, vegetationProgram);
bgfx::setMarker("Skinned characters");
bgfx::submit(kViewOpaque, skinnedProgram);
```

### External Profiler Compatibility

| Tool | Platform | How to use | What you get |
|---|---|---|---|
| **Tracy** | Win / Mac / Linux / iOS / Android | Enable `NIMBUS_PROFILING`, pass `TracyBgfxCallback` | Full CPU+GPU timeline, per-draw zones, memory tracking |
| **RenderDoc** | Win / Mac / Linux / Android | Launch via RenderDoc or `BGFX_DEBUG_IFH`; view names + markers appear automatically | Frame capture, per-draw state inspection, resource viewer |
| **Apple Instruments** (Metal GPU Trace) | Mac / iOS | Run from Xcode with Metal GPU Trace template | Full GPU timeline, per-encoder timing, shader execution stats, bandwidth |
| **Android GPU Inspector** | Android | `adb` + AGI; Vulkan debug labels from view names appear automatically | GPU timeline, shader profiling, memory heatmaps |
| **PIX** | Windows | `WinPixEventRuntime`; wire into `bgfx::setMarker()` on Vulkan backend | GPU capture, per-draw timing, pipeline state |
| **Xcode Organizer / MetricKit** | iOS (production) | Automatic — no engine changes | On-device frame time, GPU hang reports from real users |

No code changes are required to use RenderDoc, Instruments, or Android GPU Inspector beyond setting view names (already done). Tracy and PIX require the profiling build flag.

### In-Game Visualiser

`bgfx::Stats` is read every frame and shown in the in-game visualiser panel (visible in both editor and play mode). Displayed per pass:

```
┌─────────────────────────────────────────────────────┐
│ Frame  CPU 4.2ms   GPU 8.7ms   Draw 1,243           │
├──────────────────┬──────────┬──────────┬────────────┤
│ Pass             │ CPU (ms) │ GPU (ms) │ Draw calls │
├──────────────────┼──────────┼──────────┼────────────┤
│ Shadow Maps      │  0.8     │  2.1     │ 312        │
│ Depth Prepass    │  0.3     │  0.6     │ 201        │
│ Opaque Forward+  │  1.4     │  3.8     │ 587        │
│ Transparency     │  0.2     │  0.9     │  87        │
│ Post-Process     │  0.1     │  1.0     │   4        │
│ UI / HUD         │  0.1     │  0.3     │  52        │
└──────────────────┴──────────┴──────────┴────────────┘
│ Textures 312 MB   RT 48 MB   Primitives 2.4M        │
└─────────────────────────────────────────────────────┘
```

The visualiser also exposes the `RenderSettings` toggles (SSAO, bloom, depth prepass, shadow quality) so individual passes can be disabled live to isolate their cost.

### What Requires a bgfx Fork

Nothing. bgfx's `bgfx::CallbackI` profiler API, `bgfx::Stats`, `bgfx::setViewName`, and `bgfx::setMarker` are all part of the public stable API. Tracy's bgfx integration uses only these. No modifications to bgfx are needed for any part of this profiling system.

---

## Implementation Plan

The renderer is built in phases, each independently shippable:

| Phase | What | Dependency |
|---|---|---|
| 1 | bgfx init + window integration, clear screen | Platform layer |
| 1b | `RenderSettings` struct, presets, `platformDefault()` | Phase 1 |
| 2 | Mesh upload, unlit draw call, camera | Math library ✓ |
| 3 | PBR material + directional light (no shadows) | Phase 2 |
| 4 | Shadow maps (directional, single cascade) | Phase 3 |
| 5 | Instanced mesh rendering | Phase 2 |
| 6 | Point + spot lights (clustered) | Phase 3 |
| 7 | Post-processing (bloom, tonemap, FXAA) | Phase 3 |
| 8 | SSAO | Phase 7 |
| 9 | CSM (3-cascade shadows) | Phase 4 |
| 10 | 2D sprite batching + UI pass | Phase 2 |
| 11 | IBL (environment cubemap) | Phase 3 |

---

## Open / Deferred Decisions

| Topic | Decision |
|---|---|
| Render graph | Hardcoded linear pipeline for now; render graph when pass count or resource dependencies become unwieldy |
| TAA (Temporal Anti-Aliasing) | Deferred — requires jitter + reprojection; FXAA ships first |
| Decals | Not in initial scope |
| Terrain renderer | Separate concern — heightfield + clipmap LOD; design when first game needs it |
| GPU-driven rendering (compute culling, indirect draw) | Future — triggers when instanced mesh count exceeds ~100k and CPU bottleneck is confirmed |
| Lightmaps / light probes | Deferred — static bake pipeline is significant work; dynamic lights only first |
| Ray tracing | Long-term — tabled (see NOTES.md Rendering section) |
