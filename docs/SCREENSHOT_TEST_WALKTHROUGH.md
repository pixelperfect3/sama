# Screenshot Test Walkthrough: Damaged Helmet

A detailed step-by-step explanation of how `TestSsHelmet.cpp` works — from file loading through GPU upload, ECS spawn, shadow pass, PBR shading, and final golden comparison.

---

## Setup: `BgfxContext` and `ScreenshotFixture`

Before any tests run, `main()` creates a `BgfxContext`. This initializes bgfx exactly once per process (a hard bgfx constraint). It creates a hidden GLFW window as the native surface Metal requires, at 320×240.

Each test then constructs a `ScreenshotFixture`, which owns:
- **`rtTex_`** — a BGRA8 render target (the GPU renders into this)
- **`blitTex_`** — a CPU-readable copy target (bgfx requires a separate texture for readback)
- **`captureFb_`** — a framebuffer wrapping `rtTex_` and a matching depth texture
- **`whiteTex_`** — a 1×1 white RGBA8 texture used as a default fallback for unbound samplers

---

## Stage 1: Asset Loading (worker thread via `AssetManager`)

```cpp
assets.load<GltfAsset>(helmetPath)
```

1. `AssetManager::load` checks if the path is already in `pathToSlot_`. It isn't, so `allocSlot` creates a new `Record` (state = Pending) and returns a handle encoding `(index, generation)`.

2. The `AssetManager` dispatches a lambda to the `ThreadPool` (2 threads). On the worker:
   - `StdFileSystem::read` reads `DamagedHelmet.glb` (~7 MB) into a `vector<uint8_t>`.
   - The registered `GltfLoader` (matched by `.glb` extension) decodes it via `cgltf_parse`.

3. **GLB buffer setup**: cgltf parses the JSON header but leaves `buf.data = null`. The GLB binary chunk is in `data->bin`. The loader manually points `buf.data = data->bin` with `data_free_method = none` — this is required for embedded GLB buffers so `cgltf_free` does not double-free the binary chunk.

4. **Textures**: For each `cgltf_image`, `decodeImage` reads the buffer view bytes (JPEG/PNG embedded in the GLB), then `stbi_load_from_memory` decodes to RGBA8, producing `CpuTextureData{pixels, width, height}`. The DamagedHelmet has 4 images: albedo, normal, metallicRoughness (ORM), and emissive.

5. **Meshes**: `convertPrimitive` reads the glTF accessors:
   - Positions: raw `float3` array
   - Normals: decoded and **oct-encoded** to `int16×2` (L1 octahedron projection into snorm16)
   - Tangents: **oct-encoded** to `uint8×4` (xyz oct, w = sign bit as 0 or 255)
   - UVs: converted from `float2` to `uint16×2` using `glm::packHalf2x16` (IEEE float16)
   - Indices: uint32 → uint16 (helmet stays within 65535 vertices)

6. **Materials**: `cgltf_material` is converted to `CpuMaterialData` — albedo factor, roughness, metallic, and 0-based image indices for albedo/normal/ORM textures.

7. **Node tree**: `buildNodeTree` recurses over the scene hierarchy, recording per-node `localTransform` (from TRS or raw matrix), `meshIndex`, `materialIndex`, and child indices.

8. The completed `CpuSceneData` is pushed onto `uploadQueue_` via `pushUpload`.

---

## Stage 2: GPU Upload (`processUploads` on main thread)

The test polls in a loop calling `assets.processUploads()` then `bgfx::frame()` (the `bgfx::frame()` is required to commit deferred resource-creation commands before checking state):

```cpp
while (...) {
    assets.processUploads();
    bgfx::frame();
    if (state == Ready || Failed) break;
}
```

`processUploads` → `uploadOne` → `upload(rec, CpuSceneData&&)`:

**Textures**: For each `CpuTextureData`, `createTexture2DWithMips` uploads:
```cpp
bgfx::createTexture2D(w, h, /*hasMips=*/false, 1, RGBA8, flags, mem)
```
Single mip level only. `hasMips=true` causes the Metal GPU to compute LOD=max (near-black output) due to an apparent UV-derivative issue on this hardware. `hasMips=false` forces the sampler to clamp to LOD=0, giving correct (if aliased) sampling.

**Meshes**: `rendering::buildMesh(meshData)` creates two vertex buffers and one index buffer:
- `positionVbh` (stream 0): tightly packed `float3` positions
- `surfaceVbh` (stream 1): 12-byte surface vertex — `int16×2` oct-normal + `uint8×4` oct-tangent + `uint16×2` half-float UVs
- `ibh`: `uint16` indices

**Materials**: Scalar PBR values (albedo, roughness, metallic, emissiveScale) are copied into `rendering::Material`. Texture indices are converted from 0-based asset-local to **1-based** (`cpuMat.albedoTexIndex + 1`), where 0 means "no texture".

**Nodes**: Copied field-by-field from `CpuSceneData::Node` to `GltfAsset::Node`.

After upload, `rec.state = AssetState::Ready`.

---

## Stage 3: ECS Spawn (`GltfSceneSpawner::spawn`)

```cpp
GltfSceneSpawner::spawn(*helmet, reg, res);
```

**`registerResources`**:
- Each `GltfAsset::Mesh` is registered into `RenderResources` → gets a stable `uint32_t` mesh ID
- Each `GltfAsset::Texture` handle is registered → gets a texture ID (sequential, 1-based)
- Each material's 1-based asset-texture indices are remapped through `texIds` to the actual RenderResources texture IDs

**`spawnNode`** recurses the node tree. For each node with a mesh, it creates an ECS entity with five components:
- `WorldTransformComponent{worldTransform}` — parent × local matrix accumulated down the hierarchy
- `MeshComponent{meshId}` — RenderResources ID
- `MaterialComponent{matId}` — RenderResources ID
- `VisibleTag` — marks it for the opaque pass
- `ShadowVisibleTag{cascadeMask=1}` — marks it for shadow cascade 0

---

## Stage 4: Shadow Pass (view 0)

```cpp
shadow.init({resolution=512, cascadeCount=1});
shadow.beginCascade(0, lightView, lightProj);
```

`beginCascade` binds the shadow atlas framebuffer to bgfx view 0, sets a 512×512 viewport, clears depth to 1.0, and uploads `lightView/lightProj` as the view transform.

Light direction is `normalize(0.6, 1.0, 0.8)` — upper-right-front, so the visible metallic panels are directly illuminated rather than in shadow.

```cpp
drawCallSys.submitShadowDrawCalls(reg, res, shadowProg, 0);
```

`submitShadowDrawCalls` queries entities with `ShadowVisibleTag + WorldTransformComponent + MeshComponent`. For each:
- `bgfx::setTransform` — world matrix
- `bgfx::setVertexBuffer(0, mesh->positionVbh)` — positions only (surface attributes not needed for depth-only)
- `bgfx::setIndexBuffer`
- `bgfx::setState(WRITE_Z | DEPTH_TEST_LESS | CULL_CCW)`
- `bgfx::submit(shadowView, shadowProg)` — depth-only pass writes Z into the 512×512 atlas

---

## Stage 5: Opaque PBR Pass (view 9)

Camera at `(1.5, 0.5, 2.8)` looking at origin, 45° FOV, near=0.05, far=50. Renders to the off-screen framebuffer (not the window backbuffer).

```cpp
bgfx::setViewFrameBuffer(kViewOpaque, fx.captureFb());  // render off-screen
bgfx::setViewRect(kViewOpaque, 0, 0, 320, 240);
bgfx::setViewClear(kViewOpaque, COLOR|DEPTH, 0x1A1A2EFF, 1.0f, 0);  // dark blue-grey background
bgfx::setViewTransform(kViewOpaque, &view, &proj);
```

A `PbrFrameParams` is assembled:
- `lightData[8]`: dir light direction `normalize(0.6, 1.0, 0.8)`, color `(4.0, 3.8, 3.6)` (warm white at 4× intensity)
- `shadowMatrix`: `biasMatrix * lightProj * lightView` — transforms world positions to shadow atlas UV
- `shadowAtlas`: the 512×512 depth texture from `ShadowRenderer`
- `viewportW/H`: 320×240
- `nearPlane/farPlane`: 0.05/50
- `camPos`: (1.5, 0.5, 2.8)

`drawCallSys.update(reg, res, pbrProg, uniforms, frame)` iterates `VisibleTag + WorldTransformComponent + MeshComponent + MaterialComponent`. **bgfx clears all per-draw state after each `submit()`, so everything must be re-uploaded before each draw:**

**Uniforms set each draw**:
- `u_material[2]`: `{albedo.rgb, roughness}` + `{metallic, emissiveScale, 0, 0}`
- `u_dirLight[2]`: direction + radiance
- `u_shadowMatrix[4]`: shadow matrix (all 4 slots, cascade 0 is active)
- `u_frameParams[2]`: viewport size, near/far, camera world position
- `u_lightParams`: `{0, 320, 240, 0}` — numLights=0 disables the clustered light loop
- `u_iblParams`: `{0, 0, 0, 0}` — IBL disabled, hemisphere ambient fallback used instead

**Textures bound each draw**:

| Slot | Uniform | Value |
|------|---------|-------|
| 0 | `s_albedo` | helmet albedo texture (or whiteTex fallback) |
| 1 | `s_normal` | helmet normal map (or whiteTex fallback) |
| 2 | `s_orm` | metallicRoughness texture as ORM (or whiteTex fallback) |
| 5 | `s_shadowMap` | 512×512 shadow atlas depth texture |
| 6 | `s_irradiance` | whiteCubeTex fallback (IBL disabled) |
| 7 | `s_prefiltered` | whiteCubeTex fallback (IBL disabled) |
| 8 | `s_brdfLut` | whiteTex fallback (IBL disabled) |
| 12 | `s_lightData` | whiteTex fallback (clustered lighting disabled) |
| 13 | `s_lightGrid` | whiteTex fallback |
| 14 | `s_lightIndex` | whiteTex fallback |

All 10 sampler slots declared in `fs_pbr.sc` are explicitly bound to prevent Metal validation errors from nil texture arguments.

---

## Stage 6: The PBR Fragment Shader (`fs_pbr.sc`)

For each fragment, the shader executes:

1. **Material inputs**: albedo = `u_material[0].xyz` × albedo texture sample; roughness/metallic from uniform × ORM texture channels G/B (ORM layout: R=occlusion, G=roughness, B=metallic).

2. **TBN normal mapping**: `vs_pbr.sc` outputs decoded oct-normal, oct-tangent, and computed bitangent as world-space vectors. The fragment shader builds a `mat3 TBN = mtxFromCols3(T, B, Ngeom)`, samples the normal map (decoding `[0,1]→[-1,1]`), and transforms to world space: `N = normalize(TBN * normalSample)`.

3. **Directional light Cook-Torrance BRDF**:
   - GGX normal distribution `D`
   - Smith geometry function `G` (product of two Schlick-GGX terms)
   - Fresnel-Schlick `F` with `F0 = mix(0.04, albedo, metallic)`
   - `NdotL` uses the smooth geometry normal `Ngeom` (not the normal-mapped `N`) to prevent sparkle artifacts on rough metallic surfaces
   - Outgoing radiance: `Lo = (kD * albedo/PI + D*G*F / (4 * NdotV * NdotL)) * radiance * NdotL`

4. **PCF shadow**: 2×2 tap pattern around the shadow coordinate, bias=0.005. Fully lit (shadow=1.0) if `shadowCoord.w ≤ 0.0001` or if the coordinate falls outside `[0,1]³` (fragment beyond shadow coverage). Both conditions occur in unshadowed areas and when the shadow matrix is zero.

5. **Clustered lights**: loop is skipped entirely because `lightCount = int(gridEntry.y) = 0` (whiteTex in `s_lightGrid` returns 0).

6. **Hemisphere ambient** (IBL disabled path): sky color `(0.90, 0.95, 1.20)` mixed with ground color `(0.35, 0.28, 0.18)` based on `Ngeom.y * 0.5 + 0.5`, multiplied by AO (ORM.R, floored at 0.1 to prevent fully-black ambient). An `F0`-weighted term gives metallic surfaces their color from indirect light.

7. **Emissive**: `albedo * emissiveScale`.

8. **Reinhard tonemap**: `color = color / (color + 1)`, then gamma correction `color = color^(1/2.2)`.

---

## Stage 7: Frame Capture and Golden Comparison

```cpp
auto pixels = fx.captureFrame();
```

`captureFrame` executes the bgfx readback pipeline:
1. `bgfx::blit(blitView, blitTex_, 0,0, rtTex_, 0,0, 320,240)` — copies the GPU render target to the CPU-readable blit texture
2. `bgfx::readTexture(blitTex_, pixels.data())` — schedules async readback
3. Two `bgfx::frame()` pumps — bgfx requires one frame to commit the blit and another to make the readback data available on the CPU
4. B/R channel swap — bgfx readback returns BGRA; the comparison infrastructure expects RGBA

```cpp
engine::screenshot::compareOrUpdateGolden("damaged_helmet", pixels, 320, 240)
```

Loads `tests/golden/damaged_helmet.png` and compares pixel-by-pixel. The comparison passes if **≤1% of pixels** have any channel differing by more than tolerance=8 (out of 255). This loose tolerance absorbs minor GPU driver differences between runs and machines.

On first run, or with `--update-goldens`, the current pixels are written as the new golden instead of compared.

---

## Data Flow Summary

```
DamagedHelmet.glb
  │  (worker thread)
  ├─ cgltf_parse → JSON + GLB binary chunk
  ├─ stbi_load × 4 → RGBA8 pixel buffers (albedo, normal, ORM, emissive)
  ├─ encodeOctNormal / encodeOctTangent / packHalf2x16 → packed vertex streams
  └─ pushUpload(CpuSceneData)
         │
  (main thread — processUploads + bgfx::frame loop)
  ├─ createTexture2D(hasMips=false) × 4 → bgfx TextureHandle
  ├─ buildMesh × 1 → positionVbh + surfaceVbh + ibh
  └─ AssetState::Ready
         │
  GltfSceneSpawner::spawn
  ├─ RenderResources: meshId, texIds[], matIds[]
  └─ ECS entity per node: WorldTransform + Mesh + Material + VisibleTag + ShadowVisibleTag
         │
  Shadow pass (view 0)
  └─ depth-only draw → 512×512 shadow atlas
         │
  PBR pass (view 9)
  └─ full BRDF per entity → 320×240 off-screen FBO
         │
  captureFrame
  └─ blit → readback → RGBA8 pixels (320×240×4 bytes)
         │
  compareOrUpdateGolden("damaged_helmet", pixels)
  └─ PASS (≤1% pixels differ by >8) / FAIL
```
