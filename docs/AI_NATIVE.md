# Sama Engine -- AI-Native Usage Guide

This document is designed for AI coding tools (Claude Code, Codex, Cursor, etc.) to
quickly and correctly build games with the Sama engine. Every code snippet uses the
real API and compiles against the current codebase.

---

## 1. Philosophy

### Why "AI-native" matters

Developers increasingly build games through conversation with AI tools. An AI-native
engine means:

- A developer can describe what they want ("add a bouncing red cube with physics")
  and the AI produces correct, working code on the first try.
- The API has predictable patterns: once an AI learns how to create one entity, it
  can create any entity the same way.
- Mistakes are caught fast: the build fails immediately with a clear error, or the
  entity simply does not render (with a known reason listed in this document).

### Design principles

1. **Predictable patterns.** Every subsystem follows the same component-system model.
   There are no hidden singletons or implicit global state.
2. **Self-documenting APIs.** Function names say what they do. Components are plain
   data structs with no hidden constructors.
3. **Fast feedback.** The engine builds in seconds. Screenshot tests catch visual
   regressions. Unit tests cover every subsystem.
4. **Minimal boilerplate.** The `IGame` + `GameRunner` pattern reduces a new game to
   ~80 lines. The raw `Engine` + frame loop pattern is also fully supported.

---

## 2. API Cheat Sheet

### Entities & Components

#### Create an entity with transform + mesh + material

```cpp
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"

using namespace engine::ecs;
using namespace engine::rendering;

// Assumes `reg` is a Registry& and `resources` is a RenderResources&.
MeshData cubeData = makeCubeMeshData();
Mesh cubeMesh = buildMesh(cubeData);
uint32_t meshId = resources.addMesh(std::move(cubeMesh));

Material mat;
mat.albedo = {1.0f, 0.2f, 0.2f, 1.0f};  // red
mat.roughness = 0.5f;
mat.metallic = 0.0f;
uint32_t matId = resources.addMaterial(mat);

EntityID cube = reg.createEntity();

TransformComponent tc{};
tc.position = {0.0f, 1.0f, 0.0f};
tc.rotation = glm::quat(1, 0, 0, 0);  // identity
tc.scale = {1.0f, 1.0f, 1.0f};
tc.flags = 1;  // dirty bit -- triggers world matrix recompute
reg.emplace<TransformComponent>(cube, tc);
reg.emplace<WorldTransformComponent>(cube);          // REQUIRED for rendering
reg.emplace<MeshComponent>(cube, MeshComponent{meshId});
reg.emplace<MaterialComponent>(cube, MaterialComponent{matId});
reg.emplace<VisibleTag>(cube);                        // REQUIRED to be drawn
reg.emplace<ShadowVisibleTag>(cube, ShadowVisibleTag{0xFF});  // cast shadows
```

#### Add / remove / query components

```cpp
// Add a component
reg.emplace<PointLightComponent>(entity, PointLightComponent{
    .color = {1.0f, 1.0f, 1.0f},
    .intensity = 5.0f,
    .radius = 20.0f,
});

// Get a component (returns nullptr if missing)
auto* tc = reg.get<TransformComponent>(entity);
if (tc)
{
    tc->position.y += 1.0f;
    tc->flags |= 1;  // mark dirty
}

// Check if entity has a component
bool hasMesh = reg.has<MeshComponent>(entity);

// Remove a component
reg.remove<VisibleTag>(entity);

// Destroy an entity entirely
reg.destroyEntity(entity);
```

#### Query entities (views)

```cpp
// Iterate all entities that have both TransformComponent and MeshComponent
reg.view<TransformComponent, MeshComponent>().each(
    [](EntityID id, TransformComponent& tc, MeshComponent& mc)
    {
        // process each matching entity
    });

// Iterate all live entities (any components)
reg.forEachEntity([](EntityID id)
{
    // called once per live entity
});
```

#### Create parent-child hierarchy

```cpp
#include "engine/scene/SceneGraph.h"
#include "engine/scene/HierarchyComponents.h"

// Create parent and child entities (both need TransformComponent).
EntityID parent = reg.createEntity();
// ... emplace TransformComponent, WorldTransformComponent, etc.

EntityID child = reg.createEntity();
// ... emplace TransformComponent, WorldTransformComponent, etc.

// Attach child to parent. TransformSystem will compose local -> world matrices.
engine::scene::setParent(reg, child, parent);

// Detach (make root again)
engine::scene::detach(reg, child);

// Destroy parent and all descendants recursively
engine::scene::destroyHierarchy(reg, parent);

// Query hierarchy
EntityID p = engine::scene::getParent(reg, child);
const auto* kids = engine::scene::getChildren(reg, parent);
```

---

### Rendering

#### Create a material (PBR)

```cpp
#include "engine/rendering/Material.h"

Material mat;
mat.albedo = {0.8f, 0.4f, 0.1f, 1.0f};  // .xyz = color, .w = opacity
mat.roughness = 0.6f;                     // 0 = mirror, 1 = diffuse
mat.metallic = 0.0f;                      // 0 = dielectric, 1 = metal
mat.emissiveScale = 0.0f;                 // multiplied by albedo for glow

// Texture IDs (0 = no texture, use scalar values as-is)
mat.albedoMapId = 0;
mat.normalMapId = 0;
mat.ormMapId = 0;       // ORM: R=occlusion, G=roughness, B=metallic
mat.emissiveMapId = 0;
mat.occlusionMapId = 0;

uint32_t matId = resources.addMaterial(mat);
```

#### Build a mesh

```cpp
#include "engine/rendering/MeshBuilder.h"

// Built-in unit cube (+-0.5 on each axis, with normals/tangents/UVs)
MeshData cubeData = makeCubeMeshData();
Mesh cubeMesh = buildMesh(cubeData);
uint32_t meshId = resources.addMesh(std::move(cubeMesh));
```

#### Set up directional lighting

Lighting data is passed per-frame via `PbrFrameParams`. There is no "light entity"
for directional lights in the draw call path -- the data is a flat float array.

```cpp
#include "engine/rendering/systems/DrawCallBuildSystem.h"

glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
float intensity = 6.0f;

// lightData layout: {dir.x, dir.y, dir.z, 0, col.r*i, col.g*i, col.b*i, 0}
float lightData[8] = {
    lightDir.x, lightDir.y, lightDir.z, 0.0f,
    1.0f * intensity, 0.95f * intensity, 0.85f * intensity, 0.0f
};
```

#### Set up IBL (image-based lighting)

```cpp
#include "engine/rendering/IblResources.h"

IblResources ibl;
ibl.generateDefault();  // procedural sky -- no asset files needed

// Later, in the frame loop, when building PbrFrameParams:
if (ibl.isValid())
{
    frame.iblEnabled = true;
    frame.maxMipLevels = 7.0f;
    frame.irradiance = ibl.irradiance();
    frame.prefiltered = ibl.prefiltered();
    frame.brdfLut = ibl.brdfLut();
}

// On shutdown:
ibl.shutdown();
```

#### Set up shadow mapping

```cpp
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"

// Engine creates the shadow renderer at init (configured via EngineDesc).
// In the frame loop:

glm::vec3 lightPos = lightDir * 20.0f;
glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
glm::mat4 lightProj = glm::ortho(-10.f, 10.f, -10.f, 10.f, 0.1f, 50.f);

eng.shadow().beginCascade(0, lightView, lightProj);
drawCallSys.submitShadowDrawCalls(reg, eng.resources(), eng.shadowProgram(), 0);

// For PbrFrameParams:
glm::mat4 shadowMat = eng.shadow().shadowMatrix(0);
PbrFrameParams frame{
    lightData,
    glm::value_ptr(shadowMat),
    eng.shadow().atlasTexture(),
    eng.fbWidth(), eng.fbHeight(),
    0.05f, 100.0f
};
```

#### Full render pass (opaque PBR)

```cpp
eng.renderer().beginFrameDirect();

// Shadow pass (see above)
eng.shadow().beginCascade(0, lightView, lightProj);
drawCallSys.submitShadowDrawCalls(reg, eng.resources(), eng.shadowProgram(), 0);

// Opaque pass
glm::mat4 view = cam.view();
glm::mat4 proj = glm::perspective(glm::radians(45.f),
    (float)eng.fbWidth() / (float)eng.fbHeight(), 0.05f, 100.f);

RenderPass(kViewOpaque)
    .rect(0, 0, eng.fbWidth(), eng.fbHeight())
    .clearColorAndDepth(0x1A1A2EFF)
    .transform(view, proj);

glm::mat4 shadowMat = eng.shadow().shadowMatrix(0);
PbrFrameParams frame{
    lightData, glm::value_ptr(shadowMat), eng.shadow().atlasTexture(),
    eng.fbWidth(), eng.fbHeight(), 0.05f, 100.f
};
glm::vec3 camPos = cam.position();
frame.camPos[0] = camPos.x;
frame.camPos[1] = camPos.y;
frame.camPos[2] = camPos.z;

if (ibl.isValid())
{
    frame.iblEnabled = true;
    frame.maxMipLevels = 7.0f;
    frame.irradiance = ibl.irradiance();
    frame.prefiltered = ibl.prefiltered();
    frame.brdfLut = ibl.brdfLut();
}

drawCallSys.update(reg, eng.resources(), eng.pbrProgram(), eng.uniforms(), frame);
```

---

### Assets

#### Load a glTF model (async)

```cpp
#include "engine/assets/AssetManager.h"
#include "engine/assets/GltfAsset.h"
#include "engine/assets/GltfLoader.h"
#include "engine/assets/GltfSceneSpawner.h"
#include "engine/assets/StdFileSystem.h"
#include "engine/assets/TextureLoader.h"
#include "engine/threading/ThreadPool.h"

// Setup (once, before the frame loop)
ThreadPool threadPool(2);
StdFileSystem fileSystem(".");
AssetManager assets(threadPool, fileSystem);
assets.registerLoader(std::make_unique<TextureLoader>());
assets.registerLoader(std::make_unique<GltfLoader>());

// Kick off async load (returns immediately)
auto handle = assets.load<GltfAsset>("MyModel.glb");
bool spawned = false;

// In the frame loop:
assets.processUploads();  // MUST call every frame before rendering

if (!spawned && assets.state(handle) == AssetState::Ready)
{
    const GltfAsset* model = assets.get<GltfAsset>(handle);
    GltfSceneSpawner::spawn(*model, reg, eng.resources());
    spawned = true;
}
else if (assets.state(handle) == AssetState::Failed)
{
    fprintf(stderr, "Load failed: %s\n", assets.error(handle).c_str());
}

// On shutdown:
assets.release(handle);
```

#### Spawn glTF with animation support

```cpp
#include "engine/animation/AnimationResources.h"

AnimationResources animRes;

// Use the overload that takes AnimationResources:
GltfSceneSpawner::spawn(*model, reg, eng.resources(), animRes);
```

#### Asset state machine

```
load() called  -->  AssetState::Pending
worker starts  -->  AssetState::Loading
processUploads -->  AssetState::Ready   (success)
                    AssetState::Failed  (error)
```

---

### Physics

#### Create rigid bodies

```cpp
#include "engine/physics/JoltPhysicsEngine.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsSystem.h"

// Init (once)
JoltPhysicsEngine physics;
physics.init();
PhysicsSystem physicsSys;

// Create a static floor
EntityID floor = reg.createEntity();
{
    TransformComponent tc{};
    tc.position = {0, 0, 0};
    tc.rotation = glm::quat(1, 0, 0, 0);
    tc.scale = {10, 0.2f, 10};
    tc.flags = 1;
    reg.emplace<TransformComponent>(floor, tc);
    reg.emplace<WorldTransformComponent>(floor);

    RigidBodyComponent rb;
    rb.mass = 0.0f;  // mass 0 = static
    rb.type = BodyType::Kinematic;
    rb.friction = 0.8f;
    rb.restitution = 0.2f;
    reg.emplace<RigidBodyComponent>(floor, rb);

    ColliderComponent col;
    col.shape = ColliderShape::Box;
    col.halfExtents = {5.0f, 0.1f, 5.0f};
    reg.emplace<ColliderComponent>(floor, col);
}

// Create a dynamic cube
EntityID cube = reg.createEntity();
{
    TransformComponent tc{};
    tc.position = {0, 5, 0};
    tc.rotation = glm::quat(1, 0, 0, 0);
    tc.scale = {0.5f, 0.5f, 0.5f};
    tc.flags = 1;
    reg.emplace<TransformComponent>(cube, tc);
    reg.emplace<WorldTransformComponent>(cube);
    reg.emplace<MeshComponent>(cube, MeshComponent{meshId});
    reg.emplace<MaterialComponent>(cube, MaterialComponent{matId});
    reg.emplace<VisibleTag>(cube);

    RigidBodyComponent rb;
    rb.mass = 1.0f;
    rb.type = BodyType::Dynamic;
    rb.friction = 0.5f;
    rb.restitution = 0.3f;
    reg.emplace<RigidBodyComponent>(cube, rb);

    ColliderComponent col;
    col.shape = ColliderShape::Box;
    col.halfExtents = {0.25f, 0.25f, 0.25f};
    reg.emplace<ColliderComponent>(cube, col);
}

// In the frame loop:
physicsSys.update(reg, physics, dt);
// PhysicsSystem automatically:
//   1. Creates Jolt bodies for new RigidBodyComponent entities
//   2. Syncs kinematic body positions from ECS -> Jolt
//   3. Steps the simulation
//   4. Copies Jolt transforms back to ECS TransformComponents
```

#### Apply forces and impulses

```cpp
auto* rb = reg.get<RigidBodyComponent>(entity);
if (rb && rb->bodyID != ~0u)
{
    physics.applyForce(rb->bodyID, {0.0f, 100.0f, 0.0f});     // continuous
    physics.applyImpulse(rb->bodyID, {10.0f, 0.0f, 0.0f});    // instant
    physics.setLinearVelocity(rb->bodyID, {0, 0, 0});          // direct set
    physics.setAngularVelocity(rb->bodyID, {0, 0, 0});         // direct set
}
```

#### Raycast

```cpp
RayHit hit;
if (physics.rayCastClosest({0, 10, 0}, {0, -1, 0}, 100.0f, hit))
{
    // hit.point   -- world-space hit position
    // hit.normal  -- surface normal at hit
    // hit.entity  -- EntityID of hit body
    // hit.fraction -- [0,1] along the ray
}

// All hits along a ray
std::vector<RayHit> hits = physics.rayCastAll(origin, direction, maxDist);
```

#### Collision callbacks

```cpp
// After physics.step() (called internally by PhysicsSystem::update):
for (const auto& event : physics.getContactBeginEvents())
{
    // event.entityA, event.entityB -- the two colliding entities
    // event.contactPoint, event.contactNormal, event.penetrationDepth
}
for (const auto& event : physics.getContactEndEvents())
{
    // Contact ended between event.entityA and event.entityB
}
```

#### Body types

| Type | Mass | Behavior |
|------|------|----------|
| `BodyType::Static` | 0 | Never moves (floors, walls) |
| `BodyType::Dynamic` | >0 | Moved by physics simulation |
| `BodyType::Kinematic` | 0 | Moved by game code, pushes dynamic bodies |

#### Collider shapes

| Shape | Fields used |
|-------|-------------|
| `ColliderShape::Box` | `halfExtents` |
| `ColliderShape::Sphere` | `radius` |
| `ColliderShape::Capsule` | `radius`, `halfExtents.y` (half-height) |
| `ColliderShape::Mesh` | Static only, triangle mesh |

---

### Audio

#### Play a sound (fire-and-forget)

```cpp
#include "engine/audio/SoLoudAudioEngine.h"

SoLoudAudioEngine audio;
audio.init();

// Load a clip from raw bytes (e.g., read from file)
uint32_t clipId = audio.loadClip(wavData, wavSize);

// Play (2D, non-spatial)
SoundHandle h = audio.play(clipId, SoundCategory::SFX, 1.0f, false);
```

#### Play 3D spatial audio

```cpp
// Set listener position and orientation (typically from camera)
audio.setListenerPosition(cam.position());
audio.setListenerOrientation(
    glm::normalize(cam.target - cam.position()),  // forward
    glm::vec3(0, 1, 0)                            // up
);

// Play at a world position
SoundHandle h = audio.play3D(clipId, {5.0f, 0.0f, 0.0f}, SoundCategory::SFX, 1.0f, true);

// Update spatialization each frame
audio.setPosition(h, newWorldPos);
audio.update3dAudio();
```

#### Volume control by category

```cpp
audio.setCategoryVolume(SoundCategory::SFX, 0.8f);
audio.setCategoryVolume(SoundCategory::Music, 0.5f);
audio.setCategoryVolume(SoundCategory::UI, 1.0f);
audio.setCategoryVolume(SoundCategory::Ambient, 0.6f);
audio.setMasterVolume(1.0f);
```

#### Sound categories

| Category | Use for |
|----------|---------|
| `SoundCategory::SFX` | Explosions, footsteps, impacts |
| `SoundCategory::Music` | Background music |
| `SoundCategory::UI` | Button clicks, menu sounds |
| `SoundCategory::Ambient` | Wind, rain, environment |

---

### Animation

#### Play an animation clip

```cpp
#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/AnimationSystem.h"

AnimationSystem animSys;
AnimationResources animRes;

// After spawning a glTF with animation support:
GltfSceneSpawner::spawn(*model, reg, eng.resources(), animRes);

// Start playback on all animated entities
reg.view<AnimatorComponent>().each(
    [](EntityID, AnimatorComponent& ac)
    {
        ac.flags |= AnimatorComponent::kFlagPlaying;
        ac.flags |= AnimatorComponent::kFlagLooping;
        ac.speed = 1.0f;
    });

// In the frame loop:
animSys.update(reg, dt, animRes, eng.frameArena().resource());
```

#### Pause / stop / loop

```cpp
reg.view<AnimatorComponent>().each(
    [](EntityID, AnimatorComponent& ac)
    {
        // Pause
        ac.flags &= ~AnimatorComponent::kFlagPlaying;

        // Resume
        ac.flags |= AnimatorComponent::kFlagPlaying;

        // Stop (reset to beginning)
        ac.playbackTime = 0.0f;
        ac.flags &= ~AnimatorComponent::kFlagPlaying;

        // Toggle looping
        ac.flags ^= AnimatorComponent::kFlagLooping;

        // Set playback speed (2x, 0.5x, etc.)
        ac.speed = 2.0f;
    });
```

#### AnimatorComponent flags

| Flag | Value | Meaning |
|------|-------|---------|
| `kFlagLooping` | `0x01` | Loop when reaching clip end |
| `kFlagPlaying` | `0x02` | Currently advancing playback |
| `kFlagBlending` | `0x04` | Crossfading between two clips |

#### Set up IK chains

```cpp
#include "engine/animation/IkComponents.h"
#include "engine/animation/IkSystem.h"

IkSystem ikSys;

// Attach IK to an animated entity
IkChainDef chain;
chain.rootJoint = 0;           // joint index in skeleton
chain.midJoint = 1;
chain.endEffectorJoint = 2;
chain.solverType = IkSolverType::TwoBone;
chain.weight = 1.0f;           // 0 = FK only, 1 = full IK
chain.poleVector = {0, 0, 1};

IkChainsComponent ikChains;
ikChains.chains.push_back(chain);
reg.emplace<IkChainsComponent>(entity, ikChains);

IkTarget target;
target.position = {1.0f, 0.0f, 0.5f};
IkTargetsComponent ikTargets;
ikTargets.targets.push_back(target);
reg.emplace<IkTargetsComponent>(entity, ikTargets);

// In the frame loop (replaces simple animSys.update):
animSys.updatePoses(reg, dt, animRes, eng.frameArena().resource());
ikSys.update(reg, animRes, eng.frameArena().resource());
animSys.computeBoneMatrices(reg, animRes, eng.frameArena().resource());
```

#### Rendering skinned meshes

```cpp
// After animSys.update() or computeBoneMatrices():
const engine::math::Mat4* boneBuffer = animSys.boneBuffer();
if (boneBuffer)
{
    // Skinned PBR pass
    drawCallSys.updateSkinned(reg, eng.resources(), eng.skinnedPbrProgram(),
                               eng.uniforms(), frame, boneBuffer);

    // Skinned shadow pass
    drawCallSys.submitSkinnedShadowDrawCalls(reg, eng.resources(),
        eng.skinnedShadowProgram(), 0, boneBuffer);
}
```

---

### Scene Management

#### Save / load a scene to JSON

```cpp
#include "engine/scene/SceneSerializer.h"

SceneSerializer serializer;
serializer.registerEngineComponents();

// Save
serializer.saveScene(reg, eng.resources(), "my_scene.json");

// Load
serializer.loadScene("my_scene.json", reg, eng.resources(), assets);
```

#### SceneManager (runtime scene switching)

```cpp
#include "engine/scene/SceneManager.h"

SceneManager scenes(reg, eng, assets);

// Load a scene
SceneHandle handle = scenes.loadScene("levels/level1.json");

// Mark entities that survive scene transitions
scenes.markPersistent(playerEntity);

// Switch scenes (unloads current, loads new)
scenes.unloadScene();
scenes.loadScene("levels/level2.json");

// Hot-reload during development
scenes.reloadScene();
```

#### Name entities (for debugging / serialization)

```cpp
#include "engine/scene/NameComponent.h"

reg.emplace<NameComponent>(entity, NameComponent{"Player"});
```

---

### Input

#### Check key state

```cpp
#include "engine/input/InputState.h"
#include "engine/input/Key.h"

const auto& input = eng.inputState();

// Three-state model per frame:
if (input.isKeyPressed(Key::Space))   { /* just pressed this frame */ }
if (input.isKeyHeld(Key::W))          { /* currently held down */ }
if (input.isKeyReleased(Key::Escape)) { /* just released this frame */ }
```

#### Mouse state

```cpp
double mx = input.mouseX();
double my = input.mouseY();
double dx = input.mouseDeltaX();
double dy = input.mouseDeltaY();

if (input.isMouseButtonPressed(MouseButton::Left))  { /* click */ }
if (input.isMouseButtonHeld(MouseButton::Right))     { /* drag */ }
```

#### Action mapping

```cpp
#include "engine/input/ActionMap.h"

ActionMap actions;
actions.bindKey(Key::Space, "jump");
actions.bindKey(Key::E, "interact");
actions.bindAxis("horizontal", Key::A, Key::D);  // A=-1, D=+1

// Query via action names
if (input.isActionPressed("jump", actions))   { /* jump */ }
float h = input.axisValue("horizontal", actions);  // -1, 0, or +1
```

---

### Camera

#### OrbitCamera

```cpp
#include "engine/core/OrbitCamera.h"

engine::core::OrbitCamera cam;
cam.distance = 10.0f;
cam.yaw = 0.0f;
cam.pitch = 20.0f;
cam.target = {0, 0, 0};

// Orbit on mouse drag
cam.orbit(mouseDeltaX, -mouseDeltaY, 0.18f);

// Scroll zoom
cam.zoom(scrollDelta, 1.0f, /*minDist=*/1.0f, /*maxDist=*/50.0f);

// WASD target movement (relative to camera yaw)
cam.moveTarget(input, dt, /*speed=*/3.0f);

// Get matrices for rendering
glm::mat4 view = cam.view();
glm::vec3 pos = cam.position();
glm::mat4 proj = glm::perspective(glm::radians(45.f),
    (float)eng.fbWidth() / (float)eng.fbHeight(), 0.05f, 100.f);
```

---

## 3. Common Patterns

### Component struct pattern

Every component is a plain data struct. No constructors with logic, no virtual
methods. Fields are ordered largest-alignment-first, with explicit padding and
`static_assert` on `sizeof`.

```cpp
struct MyComponent       // offset  size
{
    math::Vec3 velocity; //  0      12
    float speed;         // 12       4
    uint8_t flags;       // 16       1
    uint8_t _pad[3];     // 17       3
};  // total: 20 bytes
static_assert(sizeof(MyComponent) == 20);
```

### System update pattern

Systems are stateless classes with an `update` method that takes `Registry&` and
iterates views.

```cpp
class MySystem
{
public:
    void update(ecs::Registry& reg, float dt)
    {
        reg.view<TransformComponent, MyComponent>().each(
            [&](EntityID id, TransformComponent& tc, MyComponent& mc)
            {
                tc.position += mc.velocity * dt;
                tc.flags |= 1;  // mark dirty
            });
    }
};
```

### Asset loading pattern

All asset types follow the same lifecycle:

```
1. assets.registerLoader(...)    // once at startup
2. handle = assets.load<T>(path) // kick off async load (returns immediately)
3. assets.processUploads()       // call every frame (drains upload queue)
4. assets.state(handle)          // poll: Pending -> Loading -> Ready/Failed
5. assets.get<T>(handle)         // access data (only when Ready)
6. assets.release(handle)        // decrement ref count (on shutdown)
```

### Demo app pattern (raw frame loop)

```
Engine eng;
eng.init(desc);

// Create subsystems, load assets, set up ECS entities

while (eng.beginFrame(dt))
{
    assets.processUploads();     // drain async asset queue
    physicsSys.update(reg, physics, dt);  // step physics
    animSys.update(reg, dt, animRes, arena);  // update animations
    transformSys.update(reg);    // recompute world matrices

    eng.renderer().beginFrameDirect();
    // shadow pass, opaque pass, draw calls
    eng.endFrame();
}

// Release assets, shutdown subsystems
```

### IGame pattern (recommended for new games)

```cpp
#include "engine/game/IGame.h"
#include "engine/game/GameRunner.h"

class MyGame : public engine::game::IGame
{
public:
    void onInit(Engine& engine, Registry& registry) override
    {
        // Create entities, load scenes
    }

    void onUpdate(Engine& engine, Registry& registry, float dt) override
    {
        // Input, camera, game logic
    }

    void onFixedUpdate(Engine& engine, Registry& registry, float fixedDt) override
    {
        // Physics-rate gameplay (called at 60Hz)
    }

    void onRender(Engine& engine) override
    {
        // Custom render passes, HUD
    }

    void onShutdown(Engine& engine, Registry& registry) override
    {
        // Cleanup
    }
};

int main()
{
    MyGame game;
    engine::game::GameRunner runner(game);
    return runner.run(engine::core::EngineDesc{
        .windowWidth = 1280,
        .windowHeight = 720,
        .windowTitle = "My Game"
    });
}
```

---

## 4. Minimal Game Template

A complete, working, single-file game. Copy this and modify.

```cpp
// minimal_game.mm -- a lit, rotating cube with orbit camera

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Engine.h"
#include "engine/core/OrbitCamera.h"
#include "engine/ecs/Registry.h"
#include "engine/input/Key.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/IblResources.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/scene/TransformSystem.h"

using namespace engine::core;
using namespace engine::ecs;
using namespace engine::rendering;

int main()
{
    // -- Engine init ----------------------------------------------------------
    Engine eng;
    EngineDesc desc;
    desc.windowTitle = "Minimal Game";
    if (!eng.init(desc))
        return 1;

    // -- IBL ------------------------------------------------------------------
    IblResources ibl;
    ibl.generateDefault();

    // -- Create cube mesh + material -----------------------------------------
    uint32_t meshId = eng.resources().addMesh(buildMesh(makeCubeMeshData()));

    Material mat;
    mat.albedo = {0.2f, 0.6f, 1.0f, 1.0f};
    mat.roughness = 0.4f;
    mat.metallic = 0.0f;
    uint32_t matId = eng.resources().addMaterial(mat);

    // -- ECS: create a cube entity -------------------------------------------
    Registry reg;
    DrawCallBuildSystem drawCallSys;
    engine::scene::TransformSystem transformSys;

    EntityID cube = reg.createEntity();
    {
        TransformComponent tc{};
        tc.position = {0, 0, 0};
        tc.rotation = glm::quat(1, 0, 0, 0);
        tc.scale = {1, 1, 1};
        tc.flags = 1;
        reg.emplace<TransformComponent>(cube, tc);
        reg.emplace<WorldTransformComponent>(cube);
        reg.emplace<MeshComponent>(cube, MeshComponent{meshId});
        reg.emplace<MaterialComponent>(cube, MaterialComponent{matId});
        reg.emplace<VisibleTag>(cube);
        reg.emplace<ShadowVisibleTag>(cube, ShadowVisibleTag{0xFF});
    }

    // -- Lighting -------------------------------------------------------------
    glm::vec3 lightDir = glm::normalize(glm::vec3(1, 2, 1));
    float intensity = 6.0f;
    float lightData[8] = {
        lightDir.x, lightDir.y, lightDir.z, 0,
        intensity, 0.95f * intensity, 0.85f * intensity, 0
    };
    glm::vec3 lightPos = lightDir * 15.f;
    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 lightProj = glm::ortho(-5.f, 5.f, -5.f, 5.f, 0.1f, 40.f);

    // -- Camera ---------------------------------------------------------------
    OrbitCamera cam;
    cam.distance = 4.0f;
    cam.pitch = 20.0f;

    // -- Frame loop -----------------------------------------------------------
    float dt = 0.f;
    float angle = 0.f;

    while (eng.beginFrame(dt))
    {
        if (eng.fbWidth() == 0 || eng.fbHeight() == 0)
            continue;

        // Rotate cube
        angle += dt * 0.5f;
        auto* tc = reg.get<TransformComponent>(cube);
        tc->rotation = glm::quat(glm::vec3(0, angle, 0));
        tc->flags |= 1;

        // Camera input
        const auto& input = eng.inputState();
        cam.moveTarget(input, dt);
        transformSys.update(reg);

        // Render
        eng.renderer().beginFrameDirect();

        eng.shadow().beginCascade(0, lightView, lightProj);
        drawCallSys.submitShadowDrawCalls(reg, eng.resources(), eng.shadowProgram(), 0);

        float fbW = (float)eng.fbWidth();
        float fbH = (float)eng.fbHeight();
        glm::mat4 view = cam.view();
        glm::mat4 proj = glm::perspective(glm::radians(45.f), fbW / fbH, 0.05f, 100.f);

        RenderPass(kViewOpaque)
            .rect(0, 0, eng.fbWidth(), eng.fbHeight())
            .clearColorAndDepth(0x1A1A2EFF)
            .transform(view, proj);

        glm::mat4 shadowMat = eng.shadow().shadowMatrix(0);
        PbrFrameParams frame{
            lightData, glm::value_ptr(shadowMat), eng.shadow().atlasTexture(),
            eng.fbWidth(), eng.fbHeight(), 0.05f, 100.f
        };
        glm::vec3 camPos = cam.position();
        frame.camPos[0] = camPos.x;
        frame.camPos[1] = camPos.y;
        frame.camPos[2] = camPos.z;

        if (ibl.isValid())
        {
            frame.iblEnabled = true;
            frame.maxMipLevels = 7.0f;
            frame.irradiance = ibl.irradiance();
            frame.prefiltered = ibl.prefiltered();
            frame.brdfLut = ibl.brdfLut();
        }

        drawCallSys.update(reg, eng.resources(), eng.pbrProgram(), eng.uniforms(), frame);

        eng.endFrame();
    }

    ibl.shutdown();
    return 0;
}
```

---

## 5. Component Reference

### Rendering Components (`engine::rendering`)

| Component | Fields | Purpose |
|-----------|--------|---------|
| `TransformComponent` | `position` (Vec3), `rotation` (Quat), `scale` (Vec3), `flags` (uint8) | Local-space TRS. Set `flags \| 1` when modified. |
| `WorldTransformComponent` | `matrix` (Mat4) | Cached world-space matrix. Written by TransformSystem. |
| `MeshComponent` | `mesh` (uint32) | Index into RenderResources mesh table. |
| `MaterialComponent` | `material` (uint32) | Index into RenderResources material table. |
| `VisibleTag` | (empty) | Entity is visible to the main camera. Required to render. |
| `ShadowVisibleTag` | `cascadeMask` (uint8) | Bit N = visible to shadow cascade N. |
| `CameraComponent` | `fovY`, `nearPlane`, `farPlane`, `aspectRatio`, `type`, `viewLayer` | Camera parameters (Perspective or Orthographic). |
| `DirectionalLightComponent` | `direction` (Vec3), `color` (Vec3), `intensity` (float), `flags` (uint8) | Sun light. Bit 0 of flags = castShadows. |
| `PointLightComponent` | `color` (Vec3), `intensity` (float), `radius` (float) | Omnidirectional point light. |
| `SpotLightComponent` | `direction` (Vec3), `color` (Vec3), `intensity`, `cosInnerAngle`, `cosOuterAngle`, `radius` | Spot light. Angles are precomputed cosines. |
| `InstancedMeshComponent` | `mesh`, `material`, `instanceGroupId` (all uint32) | GPU-instanced mesh rendering. |

### Scene Components (`engine::scene`)

| Component | Fields | Purpose |
|-----------|--------|---------|
| `HierarchyComponent` | `parent` (EntityID) | Points to parent entity. |
| `ChildrenComponent` | `children` (InlinedVector) | List of child EntityIDs. |
| `NameComponent` | `name` (string) | Display name for debugging/serialization. |

### Physics Components (`engine::physics`)

| Component | Fields | Purpose |
|-----------|--------|---------|
| `RigidBodyComponent` | `bodyID`, `mass`, `linearDamping`, `angularDamping`, `friction`, `restitution`, `type` (BodyType), `layer` | Physics body config. `bodyID` set by PhysicsSystem. |
| `ColliderComponent` | `offset` (Vec3), `halfExtents` (Vec3), `radius` (float), `shape` (ColliderShape) | Collision shape definition. |
| `PhysicsBodyCreatedTag` | (empty) | Marks that PhysicsSystem has created the Jolt body. |

### Animation Components (`engine::animation`)

| Component | Fields | Purpose |
|-----------|--------|---------|
| `SkeletonComponent` | `skeletonId` (uint32) | Index into AnimationResources skeleton table. |
| `AnimatorComponent` | `clipId`, `nextClipId`, `playbackTime`, `speed`, `blendFactor`, `blendDuration`, `blendElapsed`, `flags` | Animation playback state. |
| `SkinComponent` | `boneMatrixOffset`, `boneCount` (both uint32) | Offset into per-frame bone matrix buffer. |
| `PoseComponent` | `pose` (Pose*) | Arena-allocated FK pose, valid for one frame. |
| `IkChainsComponent` | `chains` (InlinedVector of IkChainDef) | IK chain definitions (up to 4 inline). |
| `IkTargetsComponent` | `targets` (InlinedVector of IkTarget) | World-space IK targets (parallel to chains). |

### Audio Components (`engine::audio`)

| Component | Fields | Purpose |
|-----------|--------|---------|
| `AudioSourceComponent` | `clipId`, `busHandle`, `volume`, `minDistance`, `maxDistance`, `pitch`, `category` (SoundCategory), `flags` | Sound emitter. Flags: bit0=loop, bit1=spatial, bit2=autoPlay, bit3=playing. |
| `AudioListenerComponent` | `priority` (uint8) | Marks entity as the audio listener. Highest priority wins. |

---

## 6. Common Pitfalls

### Missing WorldTransformComponent

**Symptom:** Entity exists but does not render.

Every entity with a `TransformComponent` also needs a `WorldTransformComponent`.
The `TransformSystem` writes world matrices into `WorldTransformComponent`. The draw
call system reads `WorldTransformComponent` to set the model matrix. If it is missing,
the entity has no world-space position and is skipped.

```cpp
// WRONG: missing WorldTransformComponent
reg.emplace<TransformComponent>(e, tc);
reg.emplace<MeshComponent>(e, MeshComponent{meshId});

// CORRECT:
reg.emplace<TransformComponent>(e, tc);
reg.emplace<WorldTransformComponent>(e);  // <-- add this
reg.emplace<MeshComponent>(e, MeshComponent{meshId});
```

### Forgetting to call processUploads()

**Symptom:** Loaded assets never become Ready. `assets.state()` stays at Loading
forever.

`AssetManager::processUploads()` must be called once per frame on the main thread.
It drains the upload queue and creates bgfx GPU handles. Without it, decoded assets
sit in the queue indefinitely.

```cpp
// In the frame loop, BEFORE checking asset state:
assets.processUploads();
```

### Not setting the dirty flag

**Symptom:** You modify `TransformComponent::position` but the entity does not move.

`TransformSystem` only recomputes the world matrix for entities whose dirty flag is
set. After modifying any field of `TransformComponent`, set `flags |= 1`.

```cpp
auto* tc = reg.get<TransformComponent>(entity);
tc->position.y += 1.0f;
tc->flags |= 1;  // <-- REQUIRED
```

### Missing VisibleTag

**Symptom:** Entity has mesh, material, and transform but does not appear.

`VisibleTag` is required for `DrawCallBuildSystem` to include the entity in draw calls.
Without it, the entity is invisible.

```cpp
reg.emplace<VisibleTag>(entity);
```

### HiDPI / content scale confusion

**Symptom:** Mouse coordinates are offset, or the viewport renders at wrong size.

`eng.fbWidth()` / `eng.fbHeight()` return physical framebuffer pixels (e.g., 2560x1440
on a Retina display with 1280x720 logical size). Mouse coordinates from GLFW are in
logical coordinates. Use `eng.contentScaleX()` / `eng.contentScaleY()` to convert.

### Collider half-extents vs entity scale

**Symptom:** Physics collider is the wrong size relative to the visual mesh.

`ColliderComponent::halfExtents` defines the physics shape size independently of
`TransformComponent::scale`. If your entity has `scale = {2, 2, 2}` and uses the
unit cube mesh (+-0.5), set `halfExtents = {1.0, 1.0, 1.0}` to match.

### Not releasing asset handles

**Symptom:** GPU memory leak.

Always call `assets.release(handle)` on shutdown. The handle is ref-counted; release
decrements it and schedules GPU handle destruction when it reaches zero.

---

## 7. Error Messages Guide

| Symptom | Cause | Fix |
|---------|-------|-----|
| Pink/magenta rectangles | Missing texture or shader binding | Ensure all PBR texture slots are bound. Use `resources.whiteTexture()` as default for unset texture IDs. |
| Entity not rendering | Missing `VisibleTag`, `WorldTransformComponent`, or `MeshComponent` | Add all required components (see Section 2, "Create an entity"). |
| Physics body not moving | Dirty flag not set after teleporting via `setBodyPosition` | PhysicsSystem writes back transforms with dirty flag; if you manually set position, also set `tc->flags \|= 1`. |
| Asset stays in Loading state | `processUploads()` not called, or wrong file path | Call `assets.processUploads()` every frame. Check `assets.error(handle)` for path errors. |
| "No loader registered" error | Forgot to register the loader for the file extension | Call `assets.registerLoader(std::make_unique<GltfLoader>())` and/or `TextureLoader` before loading. |
| Animation not playing | `kFlagPlaying` not set on `AnimatorComponent` | Set `ac.flags \|= AnimatorComponent::kFlagPlaying`. |
| Skinned mesh not visible | Missing `updateSkinned` call in draw pass | Call `drawCallSys.updateSkinned(...)` after `drawCallSys.update(...)` with the bone buffer from `animSys.boneBuffer()`. |
| Shadow not appearing | Shadow pass not submitted, or shadow atlas not bound | Call `eng.shadow().beginCascade(...)` + `submitShadowDrawCalls(...)` before the opaque pass. Pass `eng.shadow().atlasTexture()` in `PbrFrameParams`. |
| Crash on shutdown | AssetManager destroyed after bgfx context | Ensure AssetManager is constructed after Renderer (LIFO destruction order). Release all handles before shutdown. |

---

## 8. CLAUDE.md Integration

Suggested additions to `CLAUDE.md` to help AI tools:

```markdown
## AI Tool Reference

- **API cheat sheet:** See `docs/AI_NATIVE.md` for copy-pasteable code for every
  common operation (entities, rendering, physics, audio, animation).
- **New game template:** Copy the minimal game from `docs/AI_NATIVE.md` Section 4
  as a starting point for any new app.
- **Common pitfalls:** Before debugging a rendering or physics issue, check the
  "Common Pitfalls" section in `docs/AI_NATIVE.md` Section 6.
- **Component reference:** Full table of all component types with fields is in
  `docs/AI_NATIVE.md` Section 5.
```
