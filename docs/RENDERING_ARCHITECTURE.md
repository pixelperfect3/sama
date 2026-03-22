# Rendering Architecture

The renderer sits on top of **bgfx** (Metal + Vulkan backends only) and is driven by the ECS. It is not a self-contained subsystem — it is a set of systems and components that live in the same registry as everything else. A `RenderSystem` reads transform and mesh/material data from the ECS each frame, frustum-culls it, sorts it, and submits draw calls to bgfx.

---

## Design Principles

- **ECS-driven.** Everything renderable is an entity with components. There is no separate scene representation.
- **Forward+.** Tiled clustered forward shading. Handles many dynamic lights, supports transparent objects naturally, works on mobile.
- **No render graph (yet).** Passes are a hardcoded linear pipeline. A proper render graph is the natural next step when the pipeline grows beyond ~6 passes — noted as a future upgrade.
- **PBR materials.** Physically-based rendering (albedo, roughness, metallic, normal, AO). One material format engine-wide.
- **bgfx views = passes.** Each render pass is a bgfx view. View IDs are fixed constants so pass ordering is explicit and easy to change.
- **Assets behind handles.** All GPU resources (meshes, textures, materials, shaders) are referenced by a typed `AssetHandle<T>`. The `RenderResources` registry maps handles to live bgfx handles.

---

## System Overview

```mermaid
graph TD
    ECS["ECS Registry\nCameraComponent\nMeshComponent\nMaterialComponent\nWorldTransformComponent\nLightComponents\nInstancedMeshComponent"]
    RS["RenderSystem\n(runs each frame)"]
    FC["Frustum Culling\n(engine/math/Frustum.h)"]
    Sort["Draw Call Sort\n(material → depth)"]
    Inst["Instance Buffer\nBuilder"]
    bgfx["bgfx\nMetal / Vulkan"]
    RR["RenderResources\nMesh → bgfx::VertexBufferHandle\nTexture → bgfx::TextureHandle\nShader → bgfx::ProgramHandle"]

    ECS -->|query renderables| RS
    RS --> FC
    FC --> Sort
    Sort --> bgfx
    RS --> Inst
    Inst --> bgfx
    RS -->|look up GPU handles| RR
    RR --> bgfx
```

---

## ECS Components

```mermaid
classDiagram
    class CameraComponent {
        ProjectionType type
        float fovY
        float nearPlane
        float farPlane
        float aspectRatio
        uint8_t viewLayer
    }

    class MeshComponent {
        AssetHandle~Mesh~ mesh
    }

    class MaterialComponent {
        AssetHandle~Material~ material
    }

    class DirectionalLightComponent {
        Vec3 direction
        Vec3 color
        float intensity
        bool castShadows
    }

    class PointLightComponent {
        Vec3 color
        float intensity
        float radius
    }

    class SpotLightComponent {
        Vec3 direction
        Vec3 color
        float intensity
        float innerAngle
        float outerAngle
        float radius
    }

    class InstancedMeshComponent {
        AssetHandle~Mesh~ mesh
        AssetHandle~Material~ material
        uint32_t instanceGroupId
    }
```

**Renderable entity** = `MeshComponent` + `MaterialComponent` + `WorldTransformComponent`.
**Camera entity** = `CameraComponent` + `WorldTransformComponent`.
**Light entity** = one of the light components.

`WorldTransformComponent` is written by `TransformSystem` (scene graph). `RenderSystem` reads it — no transform logic lives in the renderer.

---

## Asset Types

```mermaid
classDiagram
    class Mesh {
        bgfx::VertexBufferHandle vbh
        bgfx::IndexBufferHandle ibh
        bgfx::VertexLayout layout
        AABB bounds
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

## Render Pipeline

```mermaid
graph LR
    V0["View 0\nShadow Maps\n(directional + spot)"]
    V1["View 1\nDepth Prepass\n(opaque only)"]
    V2["View 2\nOpaque Pass\nForward+ PBR"]
    V3["View 3\nTransparency Pass\nback-to-front sorted"]
    V4["View 4\nPost-Processing\nbloom · SSAO · tonemap · FXAA"]
    V5["View 5\nUI / HUD\northographic · sprites · ImGui"]

    V0 --> V1 --> V2 --> V3 --> V4 --> V5
```

### View 0 — Shadow Maps

- One shadow map per directional light (CSM — 3 cascades), one per active spot light.
- Resolution: 2048² directional, 1024² spot (configurable).
- Depth-only render: no color attachment.
- Output: shadow map textures sampled in the opaque pass.

### View 1 — Depth Prepass

- Renders only opaque geometry to populate the depth buffer.
- Eliminates overdraw in the opaque pass (especially important for dense foliage/terrain).
- Can be disabled on mobile if the prepass cost exceeds the overdraw cost (GPU-dependent, profile-driven).

### View 2 — Opaque Pass (Forward+)

- All opaque, non-instanced geometry.
- Light data passed as a uniform buffer (point/spot lights, up to 256 active lights).
- Clustered culling: view frustum divided into a 3D grid of tiles; each tile knows which lights affect it. Computed once on CPU per frame before submission.
- Reads shadow maps from View 0 for shadow-receiving surfaces.
- PBR shader: GGX specular, Lambert diffuse, IBL ambient, shadow PCF.

### View 3 — Transparency Pass

- Alpha-blended and alpha-tested geometry.
- Sorted back-to-front by depth (camera distance) each frame.
- No depth writes. Reads depth buffer from View 1/2 for soft particle depth tests.

### View 4 — Post-Processing Chain

Each post effect is a full-screen quad blit in its own sub-pass (or chained into a single shader):

| Effect | When | Notes |
|---|---|---|
| SSAO | optional, desktop only | Reconstructed from depth + normals |
| Bloom | always | Threshold → downsample → upsample → composite |
| Tonemapping | always | ACES filmic |
| FXAA | always | Temporal AA deferred to future |

### View 5 — UI / HUD

- Orthographic projection. All 2D sprites and UI panels render here.
- ImGui rendered last in this pass (editor mode) or suppressed (shipped game).
- Depth test disabled — UI always draws on top.

---

## bgfx Integration

```mermaid
sequenceDiagram
    participant App
    participant RenderSystem
    participant bgfx

    App->>bgfx: bgfx::init(Metal/Vulkan, windowHandle)

    loop Every frame
        App->>RenderSystem: runFrame(registry, dt)
        RenderSystem->>bgfx: bgfx::setViewRect / setViewClear (all views)
        RenderSystem->>bgfx: bgfx::touch(viewId) for each active view

        RenderSystem->>bgfx: submit shadow geometry (view 0)
        RenderSystem->>bgfx: submit depth prepass (view 1)
        RenderSystem->>bgfx: submit opaque + instanced (view 2)
        RenderSystem->>bgfx: submit transparent (view 3)
        RenderSystem->>bgfx: submit post-process quads (view 4)
        RenderSystem->>bgfx: submit UI + ImGui (view 5)

        App->>bgfx: bgfx::frame()
    end

    App->>bgfx: bgfx::shutdown()
```

**bgfx threading model:** bgfx is called from the main thread only. bgfx internally manages a render thread that consumes the command buffer produced by `bgfx::frame()`. This matches our ECS threading model: `RenderSystem` runs on the main thread in its assigned phase; it never overlaps with thread-pool workers touching the same data.

**Platform-specific init:**

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
- Once GPU-driven culling is needed (1M+ instances), this becomes a compute shader — noted as future work.

---

## Lighting Model

**Ambient:** Image-based lighting (IBL) — a prefiltered environment cubemap + BRDF LUT. One IBL per scene; blended between two IBLs in transition zones.

**Directional:** Sun light. One per scene. Casts cascaded shadow maps (3 cascades, configurable split lambda).

**Point / Spot:** Dynamic, clustered. Up to 256 active lights total. The clustered grid is 16×9×24 tiles (matches 16:9 viewport, 24 depth slices). Per-tile light lists are computed on CPU and uploaded as a uniform buffer before the opaque pass.

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

Each effect is a full-screen pass. The chain is configurable at runtime (effects can be toggled via the in-game visualiser). On mobile, SSAO is disabled by default.

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

Within each pass, draw calls are sorted to minimise GPU state changes:

**Opaque pass sort key (64-bit, most significant first):**
1. Shader program ID — state changes are most expensive
2. Texture set hash — texture binding changes
3. Depth (front-to-back) — early-z rejection (less important after depth prepass, still useful)

**Transparency pass sort key:**
1. Depth (back-to-front) — required for correct blending

bgfx accepts a 64-bit sort key on `bgfx::submit()`; we pack our sort key into it directly.

---

## Implementation Plan

The renderer is built in phases, each independently shippable:

| Phase | What | Dependency |
|---|---|---|
| 1 | bgfx init + window integration, clear screen | Platform layer |
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
