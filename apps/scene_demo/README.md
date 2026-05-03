# Scene Demo

A macOS PBR scene with directional shadows and a free-fly camera.
Exercises the engine's platform, rendering, input, and ECS abstractions end-to-end.

## Controls

| Input | Action |
|---|---|
| LMB / RMB click | Capture mouse (hidden cursor + raw motion) |
| Escape | Release mouse |
| WASD | Move in look direction |
| Q / E | Move down / up (world-space) |
| Shift | 3× speed |
| F | Toggle HUD |

## Architecture

### Engine layers used

```
┌─────────────────────────────────────────────────────────────────┐
│                          scene_demo                             │
│  Camera    Registry    DrawCallBuildSystem    HUD               │
└──────────┬──────────────────┬──────────────────────────────────-┘
           │                  │
    ┌──────▼──────┐   ┌───────▼────────────────────────────────┐
    │   Input     │   │              Rendering                  │
    │             │   │                                         │
    │ IInputBackend│  │  IWindow ──► Renderer ──► RenderPass   │
    │ GlfwInput   │   │  GlfwWindow   (bgfx     (view config)  │
    │ Backend     │   │               init/     ShadowRenderer │
    │             │   │               frame/    RenderResources│
    │ InputSystem │   │               shutdown) ShaderUniforms │
    │ InputState  │   │                         DrawCallBuild  │
    └─────────────┘   │                         System         │
                      └─────────────────────────────────────────┘
                               │
                        ┌──────▼──────┐
                        │    bgfx     │
                        │  (Metal on  │
                        │   macOS)    │
                        └─────────────┘
```

### Startup sequence

```
main()
  │
  ├─ createWindow({1280, 720, "Scene Demo"})
  │    └─ GlfwWindow: glfwInit → glfwCreateWindow
  │                   nativeWindowHandle():
  │                     [NSView setWantsLayer:YES]
  │                     [CAMetalLayer layer]
  │                     [NSView setLayer:metalLayer]
  │                     returns CAMetalLayer*
  │
  ├─ Renderer::init(RendererDesc)
  │    ├─ bgfx::renderFrame()   ← single-threaded mode gate
  │    ├─ bgfx::init(Metal, nwh=CAMetalLayer*)
  │    ├─ ShaderUniforms::init()
  │    └─ PostProcessSystem::init()
  │
  ├─ loadShadowProgram() / loadPbrProgram()   ← embedded shader bytecode
  ├─ RenderResources::addMesh(buildMesh(makeCubeMeshData()))
  ├─ bgfx::createTexture2D(1×1 white fallback)
  ├─ ShadowRenderer::init({resolution=2048, cascades=1})
  │    └─ creates D32F atlas texture + framebuffer
  │
  ├─ renderer.endFrame()   ← flush GPU resource uploads
  │
  ├─ Registry reg   ← ECS registry
  └─ for each ObjectDesc in kObjects:
       ├─ res.addMaterial(Material{albedo, roughness})
       └─ reg.createEntity()
            ├─ WorldTransformComponent{translate * scale matrix}
            ├─ MeshComponent{meshId}
            ├─ MaterialComponent{matId}
            ├─ VisibleTag{}
            └─ ShadowVisibleTag{cascadeMask=1}
```

### Per-frame loop

```
window->pollEvents()
glfwGetFramebufferSize()  ──► renderer.resize()   [on change]
inputSys.update(inputState)
cam.update(inputState, dt)

renderer.beginFrameDirect()
  └─ RenderPass(kViewOpaque).framebuffer(engine::rendering::kInvalidFramebuffer)
     RenderPass(kViewShadowBase).touch()

─────────────────────────────────────────────────────────────────
  VIEW 0   Shadow pass   (ShadowRenderer + DrawCallBuildSystem)
─────────────────────────────────────────────────────────────────
shadow.beginCascade(0, lightView, lightProj)
  └─ RenderPass(0)
       .framebuffer(shadowAtlasFb)   ← D32F 2048×2048
       .rect(0, 0, 2048, 2048)
       .clearDepth(1.0)
       .transform(lightView, lightProj)

drawCallSys.submitShadowDrawCalls(reg, res, shadowProg, 0)
  └─ for each entity with ShadowVisibleTag + WorldTransformComponent + MeshComponent:
       bgfx::setTransform / setVertexBuffer(stream 0) / setIndexBuffer
       bgfx::setState(WRITE_Z | DEPTH_TEST_LESS | CULL_CCW)
       bgfx::submit(kViewShadowBase=0, shadowProg)

─────────────────────────────────────────────────────────────────
  VIEW 9   Opaque pass   (PBR + PCF shadows)
─────────────────────────────────────────────────────────────────
RenderPass(kViewOpaque=9)
  .rect(0, 0, W, H)
  .clearColorAndDepth(0x87CEEBFF)   ← sky blue
  .transform(camView, camProj)

PbrFrameParams frame{lightData, shadowMatrixPtr, shadow.atlasTexture()}

drawCallSys.update(reg, res, pbrProg, renderer.uniforms(), frame)
  └─ for each entity with VisibleTag + WorldTransformComponent + MeshComponent + MaterialComponent:
       bgfx::setUniform(u_material, ...)       ← albedo + roughness (from MaterialComponent)
       bgfx::setUniform(u_dirLight, ...)       ← direction + colour×intensity  (from frame)
       bgfx::setUniform(u_shadowMatrix, ...)   ← world → shadow UV             (from frame)
       bgfx::setTexture(0, s_albedo, white)
       bgfx::setTexture(2, s_orm,    white)
       bgfx::setTexture(5, s_shadowMap, shadowAtlas)
       bgfx::setState(BGFX_STATE_DEFAULT)
       bgfx::submit(kViewOpaque=9, pbrProg)

─────────────────────────────────────────────────────────────────
  Debug text overlay   (bgfx full-backbuffer overlay)
─────────────────────────────────────────────────────────────────
bgfx::dbgTextClear / dbgTextPrintf   ← FPS, camera, controls

renderer.endFrame()   ← bgfx::frame()
```

### Shadow matrix derivation

The shadow matrix transforms a world-space position into shadow-map UV
coordinates ready to pass to `shadow2D()` in the fragment shader:

```
shadowCoord = shadowMatrix * vec4(worldPos, 1.0)

shadowMatrix = atlasTranslate * atlasScale * biasMatrix * lightProj * lightView

  lightView    — lookAt from light position toward scene origin
  lightProj    — orthographic ±14 units, 100-unit depth range
  biasMatrix   — NDC XY [-1,1] → UV [0,1];  Z unchanged (depth already [0,1])
  atlasScale   — scale UV into the cascade tile (1×1 for single cascade)
  atlasTranslate — offset UV to the cascade tile origin
```

The fragment shader (fs_pbr.sc) runs 2×2 PCF using `shadow2D()` (hardware
comparison sampling enabled by `BGFX_SAMPLER_COMPARE_LEQUAL` on the atlas
texture).

### Key bgfx view layout

| View ID | Constant | Purpose |
|---|---|---|
| 0 | `kViewShadowBase` | Shadow depth pass → D32F atlas |
| 9 | `kViewOpaque` | PBR forward pass → backbuffer |

View state is **last-write-wins** per frame in bgfx.  The shadow pass sets
view 0's rect to 2048×2048 via `RenderPass`; nothing in the frame may call
`setViewRect(0, ...)` after that or it would shrink the shadow viewport and
corrupt the depth atlas.

### bgfx per-draw state reset

bgfx resets all per-draw state (`setUniform`, `setTexture`, `setTransform`,
`setVertexBuffer`, `setState`) after every `submit()`. `DrawCallBuildSystem`
re-sets `u_dirLight` and `u_shadowMatrix` before every submit — they are not
frame-level constants despite being the same value for all draws.

### Input pipeline

```
GLFWwindow callbacks
  key / mouse button / cursor position
        │
        ▼
  GlfwInputBackend          (mutex-protected write buffer)
        │  collectEvents()
        ▼
  InputSystem::update()     (computes pressed / held / released transitions)
        │
        ▼
  InputState                (per-frame snapshot, read by Camera::update)
```

Mouse capture uses `GLFW_CURSOR_DISABLED` + optional `GLFW_RAW_MOUSE_MOTION`.
A `skipMouseFrame` flag suppresses the delta spike on the first captured frame.

## File layout

```
apps/scene_demo/
  main.mm          — entry point (ObjC++ for macOS; no ObjC code in use)
  README.md        — this file

engine/ecs/
  Registry.h/.cpp              — entity lifecycle + component storage
  SparseSet.h                  — per-component dense storage
  View.h                       — multi-component iteration

engine/platform/
  Window.h                     — IWindow interface + createWindow() factory
  desktop/GlfwWindow.h/.cpp    — GLFW backend; attaches CAMetalLayer on macOS

engine/rendering/
  Renderer.h/.cpp              — bgfx lifecycle (init/frame/resize/shutdown)
  RenderPass.h/.cpp            — fluent view-config builder
  ShadowRenderer.h/.cpp        — shadow atlas + per-cascade framebuffers
  RenderResources.h/.cpp       — mesh + material + texture registry
  ShaderUniforms.h/.cpp        — all bgfx uniform handles, created once
  ShaderLoader.h/.cpp          — loads embedded shader bytecode
  EcsComponents.h              — VisibleTag, ShadowVisibleTag, WorldTransformComponent,
                                 MeshComponent, MaterialComponent, etc.
  Material.h                   — PBR material parameters
  ViewIds.h                    — named view ID constants
  systems/DrawCallBuildSystem.h/.cpp  — ECS-driven draw call submission

engine/input/
  IInputBackend.h              — platform seam
  InputSystem.h/.cpp           — event → state transitions
  InputState.h/.cpp            — per-frame key/mouse/touch snapshot
  desktop/GlfwInputBackend.h/.cpp

engine/shaders/
  vs_shadow.sc / fs_shadow.sc  — depth-only shadow pass
  vs_pbr.sc    / fs_pbr.sc     — PBR forward pass with PCF shadow sampling
```

## Build

```bash
cmake -B build && cmake --build build --target scene_demo
./build/scene_demo
```
