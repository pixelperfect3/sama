# Sama Engine -- Agent & API Reference

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

#### Create a transparent material

```cpp
Material mat;
mat.albedo = {0.2f, 0.8f, 1.0f, 0.5f};  // .w = 0.5 → 50% opacity
mat.roughness = 0.1f;                     // smooth glass-like
mat.metallic = 0.0f;
mat.transparent = 1;  // render in transparent pass (kViewTransparent, view 10)

uint32_t matId = resources.addMaterial(mat);
```

**Important:** transparent entities render to `kViewTransparent` (view 10) which
you must configure with the same view/proj matrices and viewport rect as the
opaque pass. Pattern:

```cpp
RenderPass(kViewOpaque)
    .rect(0, 0, W, H)
    .clearColorAndDepth(0x1A1A2EFF)
    .transform(view, proj);

// Mirror the same transform for transparent pass
RenderPass(kViewTransparent)
    .rect(0, 0, W, H)
    .transform(view, proj);  // no clear — depth-tests against opaque pass
```

Transparent materials use alpha blending (`BGFX_STATE_BLEND_ALPHA`) and do NOT
sort back-to-front. For large transparent objects that self-intersect, use
depth-sorted submission (TODO: automatic sorting not yet implemented).

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
| `kFlagSampleOnce` | `0x08` | Sample one frame while paused (editor scrub); auto-cleared |

#### Animation events

```cpp
#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationEventQueue.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/Hash.h"

// Add events to a clip (sorted insertion by time).
AnimationClip* clip = animRes.getClipMut(clipId);
clip->addEvent(0.3f, "footstep_left");
clip->addEvent(0.8f, "footstep_right");

// Option 1: Poll the event queue each frame (per-entity).
reg.view<AnimationEventQueue>().each(
    [](EntityID entity, AnimationEventQueue& queue)
    {
        uint32_t footstepHash = engine::animation::fnv1a("footstep_left");
        if (queue.has(footstepHash))
        {
            // Play footstep sound, spawn dust particle, etc.
        }
        queue.clear();  // consume events
    });

// Option 2: Set a global callback (fires synchronously during update).
animSys.setEventCallback(
    [](ecs::EntityID entity, const AnimationEvent& event)
    {
        printf("Entity %llu fired event: %s at t=%.3f\n",
               entity, event.name.c_str(), event.time);
    });
```

#### Animation state machine

```cpp
#include "engine/animation/AnimStateMachine.h"
#include "engine/animation/AnimStateMachineSystem.h"

// Build a shared state machine definition (once at setup).
AnimStateMachine sm;
uint32_t idle = sm.addState("Idle", idleClipId, /*loop=*/true);
uint32_t walk = sm.addState("Walk", walkClipId, /*loop=*/true);
uint32_t attack = sm.addState("Attack", attackClipId, /*loop=*/false, /*speed=*/1.5f);

// Idle -> Walk when "speed" > 0.5
sm.addTransition(idle, walk, /*blendDuration=*/0.2f,
                 "speed", TransitionCondition::Compare::Greater, 0.5f);

// Walk -> Idle when "speed" < 0.1
sm.addTransition(walk, idle, 0.2f,
                 "speed", TransitionCondition::Compare::Less, 0.1f);

// Idle -> Attack when "attack" is true
sm.addTransition(idle, attack, 0.1f,
                 "attack", TransitionCondition::Compare::BoolTrue);

// Attack -> Idle after 80% of clip plays
sm.addTransitionWithExitTime(attack, idle, 0.2f, /*exitTime=*/0.8f);

// Attach to an entity.
AnimStateMachineComponent smComp;
smComp.machine = &sm;  // shared, not owned
smComp.currentState = sm.defaultState;
reg.emplace<AnimStateMachineComponent>(entity, smComp);

// In the frame loop, BEFORE animSys.update():
AnimStateMachineSystem smSys;
smSys.update(reg, dt, animRes);

// Set parameters from game logic:
auto* smc = reg.get<AnimStateMachineComponent>(entity);
smc->setFloat("speed", playerSpeed);
smc->setBool("attack", attackButtonPressed);
```

#### Mutable clip access (AnimationResources)

```cpp
// Get a mutable pointer to a clip (e.g., to add events after loading).
AnimationClip* clip = animRes.getClipMut(clipId);
if (clip)
{
    clip->addEvent(0.5f, "impact");
}
```

#### Animation serialization (sidecar files)

```cpp
#include "engine/animation/AnimationSerializer.h"

// Save all clip events to a sidecar JSON file.
// Format: { "clips": [ { "name": "Walk", "events": [...] }, ... ] }
engine::animation::saveEvents(animRes, "assets/models/Character.events.json");

// Load events from a sidecar and apply to matching clips by name.
engine::animation::loadEvents(animRes, "assets/models/Character.events.json");

// Save a state machine definition to JSON.
// Clip IDs are resolved to clip names for portability.
engine::animation::saveStateMachine(sm, animRes,
    "assets/models/Character.statemachine.json");

// Load a state machine definition from JSON.
// Clip names are resolved back to clip IDs via AnimationResources.
AnimStateMachine sm;
engine::animation::loadStateMachine(sm, animRes,
    "assets/models/Character.statemachine.json");
```

**Sidecar file naming convention:** For a model at `path/Model.glb`, sidecar files are `path/Model.events.json` and `path/Model.statemachine.json`. The editor auto-loads these on import.

**Events JSON example:**
```json
{
    "clips": [
        {
            "name": "Walk",
            "events": [
                { "name": "footstep_left", "time": 0.3 },
                { "name": "footstep_right", "time": 0.8 }
            ]
        }
    ]
}
```

**State machine JSON example:**
```json
{
    "defaultState": 0,
    "states": [
        {
            "name": "Idle",
            "clip": "Idle",
            "speed": 1.0,
            "loop": true,
            "transitions": [
                {
                    "target": 1,
                    "blendDuration": 0.2,
                    "exitTime": 0.0,
                    "hasExitTime": false,
                    "conditions": [
                        { "param": "speed", "compare": "greater", "threshold": 0.5 }
                    ]
                }
            ]
        }
    ]
}
```

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

#### Gyroscope / accelerometer

```cpp
#include "engine/input/InputState.h"

const auto& input = eng.inputState();

if (input.gyro().available)
{
    // Angular velocity in radians/sec
    float pitch = input.gyro().pitchRate;  // rotation around X
    float yaw   = input.gyro().yawRate;    // rotation around Y
    float roll  = input.gyro().rollRate;   // rotation around Z

    // Gravity vector (normalized, from accelerometer)
    float gx = input.gyro().gravityX;
    float gy = input.gyro().gravityY;
    float gz = input.gyro().gravityZ;

    // Example: integrate gyro into camera look
    cameraYaw   += yaw * sensitivity * dt;
    cameraPitch += pitch * sensitivity * dt;
}
```

`GyroState` is populated by platform backends: Android via `ASensorManager` +
`ASENSOR_TYPE_GYROSCOPE`, iOS via `CMMotionManager`. On desktop (GLFW),
`available` is always `false`.

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

### UI / 2D Text

The retained-mode UI system lives in `engine::ui`. A `UiCanvas` owns a tree
of `UiNode` widgets (panels, text, buttons, sliders, progress bars). Every
frame you call `canvas.update()` to recompute layout, then submit the
resulting `UiDrawList` through a `UiRenderer` on a dedicated bgfx view.

#### Minimal text rendering

```cpp
#include "engine/ui/UiCanvas.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiRenderer.h"
#include "engine/ui/MsdfFont.h"

// Once at startup:
engine::ui::MsdfFont font;
font.loadFromFile("assets/fonts/JetBrainsMono-msdf.json",
                  "assets/fonts/JetBrainsMono-msdf.png");

engine::ui::UiRenderer ui;
ui.init();

// Per frame:
engine::ui::UiDrawList dl;
dl.drawText({12.f, 12.f}, "Hello, world",
            {1.f, 1.f, 1.f, 1.f}, &font, 18.f);

ui.render(dl, viewId, fbW, fbH);  // view setup handled internally

// At shutdown:
font.shutdown();
ui.shutdown();
```

`drawText`'s `position` is the **top-left of the line in y-down screen
pixels**. `fontSize` is in pixels; the renderer scales each glyph quad by
`fontSize / font.nominalSize()`. Pass `nullptr` for the font argument to
use `engine::ui::defaultFont()` (a 96-glyph 8×8 ASCII bitmap baked into the
binary — useful as a no-asset fallback).

#### DebugHud (quick stats overlay)

For debug/status text without setting up fonts or draw lists, use
`engine::ui::DebugHud`. It uses character-cell coordinates (col, row)
matching the old `bgfx::dbgTextPrintf` convention, and works on Android.

```cpp
#include "engine/ui/DebugHud.h"

engine::ui::DebugHud hud;
hud.init();

// Per frame:
hud.begin(engine.fbWidth(), engine.fbHeight());
hud.printf(1, 1, "FPS: %.1f", 1.0f / dt);
hud.printf(1, 2, 0xFF00FFFF, "Entities: %d", count);  // yellow
hud.end();

// At shutdown:
hud.shutdown();
```

Each cell is 8×16 pixels. Color is packed RGBA (`0xRRGGBBAA`), default white.

#### Picking a font backend

All three backends implement `engine::ui::IFont` and are interchangeable
from the renderer's POV. Pick by GPU tier or by what you need:

| Backend | Asset | Looks like | Use when |
|---|---|---|---|
| `BitmapFont::createDebugFont()` | none (embedded) | 8×8 fixed-size, low quality | Bootstrapping, no asset toolchain |
| `BitmapFont::loadFromFile(.fnt, .png)` | BMFont pair | Sharp at the size it was generated | Single fixed UI scale |
| `MsdfFont::loadFromFile(.json, .png)` | msdf-atlas-gen output | Sharp at any draw size | Default for UI; recommended |
| `SlugFont::loadFromFile(.ttf, sizePx)` | TTF | Vector-perfect at any size, rotation, perspective | Desktop / VR / hi-DPI text |

`engine::ui::SlugFont` requires FreeType at build time (`SAMA_HAS_FREETYPE`)
and ships with `assets/fonts/JetBrainsMono-Regular.ttf` as the default
source font. UiRenderer dispatches to a Slug-aware code path
(`renderSlugText`) that submits one draw per glyph and writes per-vertex
font-space corners into TEXCOORD0 so the slug fragment shader can do its
ray-casting. v1 has hard-edged coverage (no AA yet); see
`docs/SLUG_NEXT_STEPS.md` for the polish backlog.

#### Generating an MSDF atlas

The default `JetBrainsMono-msdf.{png,json}` is committed under
`assets/fonts/`. To generate one for a different font:

```sh
# Build msdf-atlas-gen v1.3 from source — there's no Homebrew formula.
git clone --recursive https://github.com/Chlumsky/msdf-atlas-gen.git /tmp/m
cd /tmp/m && git checkout v1.3 && git submodule update --init --recursive
cmake -B build -DMSDF_ATLAS_BUILD_STANDALONE=ON -DMSDF_ATLAS_USE_SKIA=OFF \
    -DMSDFGEN_USE_VCPKG=OFF -DMSDFGEN_USE_SKIA=OFF -DMSDFGEN_INSTALL=OFF
cmake --build build -j$(sysctl -n hw.ncpu)

# From the engine repo root:
/tmp/m/build/bin/msdf-atlas-gen \
    -font assets/fonts/MyFont-Regular.ttf \
    -size 32 -pxrange 4 \
    -format png -type msdf \
    -imageout assets/fonts/MyFont-msdf.png \
    -json     assets/fonts/MyFont-msdf.json
```

Match `pxRange` between this command and the shader (`u_msdfParams.x` is set
from `MsdfFont::distanceRange_`, which is read from the JSON's `atlas.distanceRange`
field). 32 px nominal size + 4 px range gives sharp edges from ~10 to ~80 px on screen.

#### Widgets

Widgets live under `engine/ui/widgets/`. They subclass `UiNode`, hold their
own style data, and emit draw commands via `onDraw(UiDrawList&)`.

```cpp
#include "engine/ui/widgets/UiPanel.h"
#include "engine/ui/widgets/UiText.h"
#include "engine/ui/widgets/UiButton.h"

engine::ui::UiCanvas canvas(fbW, fbH);

auto* bg = canvas.createNode<engine::ui::UiPanel>("bg");
bg->anchor = {{0.f, 0.f}, {1.f, 1.f}};       // stretch to fill canvas
bg->offsetMin = {16.f, 16.f};
bg->offsetMax = {-16.f, -16.f};
bg->color = {0.1f, 0.1f, 0.15f, 1.f};

auto* title = canvas.createNode<engine::ui::UiText>("title");
title->anchor = {{0.f, 0.f}, {1.f, 0.f}};    // stretch top edge
title->offsetMin = {0.f, 8.f};
title->offsetMax = {0.f, 36.f};
title->text = "MENU";
title->fontSize = 24.f;
title->font = &font;                          // optional; null = default font
title->align = engine::ui::TextAlign::Center;

auto* play = canvas.createNode<engine::ui::UiButton>("play");
play->anchor = {{0.5f, 0.5f}, {0.5f, 0.5f}};  // pinned to canvas center
play->offsetMin = {-60.f, -16.f};
play->offsetMax = {60.f, 16.f};
play->label = "Play";
play->fontSize = 16.f;
play->onClick = [](engine::ui::UiNode&) { /* start the game */ };

canvas.root()->addChild(bg);
bg->addChild(title);
bg->addChild(play);

canvas.update();   // recomputes layout from anchors+offsets
ui.render(canvas.drawList(), viewId, fbW, fbH);
```

The `anchor` field is a Unity-style `RectTransform`: each corner is a
fraction of the parent (0 = parent's left/top, 1 = parent's right/bottom).
`offsetMin` and `offsetMax` are pixel offsets added on top of those anchor
points. `{{0,0},{1,1}} + {16,16}/{-16,-16}` gives "fill parent with a 16-px
margin"; `{{0.5,0.5},{0.5,0.5}}` gives "centered on parent".

For mouse hit-testing and click events, dispatch synthesized `UiEvent`s
into `canvas.dispatchEvent(...)` or call `node->onEvent(...)` directly.
The button widget already handles `MouseEnter` / `MouseDown` / `MouseUp` to
flip its `state_` between Normal / Hovered / Pressed.

#### HUD overlay on top of a 3D scene

The standard pattern for game HUDs and the editor's own status overlay:
allocate a dedicated bgfx view past the post-process range, set its clear
to `BGFX_CLEAR_NONE` so it composites over whatever the scene pass drew,
and submit a `UiDrawList` from `UiRenderer::render`. The `kViewImGui`
view (id 15) is reserved for editor overlays; use `kViewGameUi` (id 48)
for in-game HUDs. Both run after all post-processing so text isn't
affected by bloom / FXAA / tonemapping.

```cpp
// Once at startup:
engine::ui::MsdfFont hudFont;
hudFont.loadFromFile("assets/fonts/JetBrainsMono-msdf.json",
                     "assets/fonts/JetBrainsMono-msdf.png");
engine::ui::UiDrawList hudDrawList;
engine::ui::UiRenderer ui;
ui.init();

// Per frame, after the 3D scene pass and any post-processing:
hudDrawList.clear();

// Build the HUD as a flat list of text commands. drawText takes the
// top-left position in y-down screen pixels.
char fpsBuf[64];
std::snprintf(fpsBuf, sizeof(fpsBuf), "%.1f fps  |  %.2f ms",
              1.f / dt, dt * 1000.f);
hudDrawList.drawText({12.f, 8.f},  fpsBuf,    {1.f, 1.f, 1.f, 1.f},
                     &hudFont, 16.f);
hudDrawList.drawText({12.f, 28.f}, "WASD=move  Space=jump",
                     {0.75f, 0.75f, 0.75f, 1.f}, &hudFont, 13.f);

// Set up the view: same framebuffer as the scene pass, no clear, so
// the HUD composites on top of whatever was rendered there.
const bgfx::ViewId hudView = engine::rendering::kViewGameUi;
bgfx::setViewName(hudView, "HUD");
bgfx::setViewRect(hudView, 0, 0, fbW, fbH);
bgfx::setViewClear(hudView, BGFX_CLEAR_NONE);
bgfx::touch(hudView);
ui.render(hudDrawList, hudView, fbW, fbH);

// At shutdown:
hudFont.shutdown();
ui.shutdown();
```

The rectangle pass and the text pass inside `UiRenderer::render` both
disable depth test and write — the HUD always sits on top of the scene
regardless of where the camera is looking. `editor/EditorApp.cpp` uses
this exact pattern to render its FPS / mode label / "PLAYING" status
strip in JetBrains Mono MSDF, with a fallback to `bgfx::dbgTextPrintf`
if the HUD font fails to load.

#### Swapping font backends at runtime

`UiText`/`UiButton` widgets and `drawText` calls hold an `IFont*` so
you can swap the active font at any time without rebuilding the canvas.
The `apps/ui_test` app uses this to cycle MSDF → Bitmap → Slug on the F
key:

```cpp
engine::ui::BitmapFont bitmap;  bitmap.createDebugFont();
engine::ui::MsdfFont   msdf;    msdf.loadFromFile(jsonPath, pngPath);
engine::ui::SlugFont   slug;    slug.loadFromFile(ttfPath, 24.f);

engine::ui::IFont* current = &msdf;  // start with MSDF

// On F press: cycle and re-apply to every text widget in the canvas.
if (input.isKeyPressed(engine::input::Key::F))
{
    current = (current == &msdf)    ? static_cast<engine::ui::IFont*>(&bitmap)
            : (current == &bitmap)  ? static_cast<engine::ui::IFont*>(&slug)
                                    : static_cast<engine::ui::IFont*>(&msdf);

    // Walk the canvas and rewrite every text widget's font pointer.
    auto walk = [&](auto& self, engine::ui::UiNode* node) -> void
    {
        if (auto* t = dynamic_cast<engine::ui::UiText*>(node))   t->font = current;
        if (auto* b = dynamic_cast<engine::ui::UiButton*>(node)) b->font = current;
        for (auto* c : node->children()) self(self, c);
    };
    walk(walk, canvas.root());
}
```

Standalone `drawText` commands take the font as a parameter, so you
just pass `current` directly and pick up the new backend on the next
frame.

---

### Asset Pipeline CLI (`sama-asset-tool`)

Process assets for Android/iOS builds with tier-based quality settings.

```bash
# Process all assets for Android mid-tier
sama-asset-tool --input assets/ --output build/android/mid/assets/ --target android --tier mid

# Preview what would be processed (no file writes)
sama-asset-tool --input assets/ --output out/ --tier low --dry-run --verbose

# Show help
sama-asset-tool --help
```

**Tiers:**

| Tier | Max texture size | ASTC block |
|------|-----------------|------------|
| `low` | 512px | 8x8 |
| `mid` | 1024px | 6x6 |
| `high` | 2048px | 4x4 |

**Output:** Processed assets + `manifest.json` listing all entries with type, format,
dimensions, and source/output paths.

**Note:** ASTC encoding is currently stubbed (the `astc-codec` library is decode-only).
Textures are copied to the output directory. Full ASTC compression requires the
`astcenc` CLI tool.

Source: `tools/asset_tool/` (AssetProcessor, TextureProcessor, ShaderProcessor).

### APK Build (`android/build_apk.sh`)

Build a signed Android APK from the command line (Gradle-free).

```bash
# Default: mid tier, arm64-v8a, release, debug-signed
./android/build_apk.sh

# High tier, debug build, auto-install to connected device
./android/build_apk.sh --tier high --debug --install

# Custom app name, package, and output
./android/build_apk.sh --app-name "My Game" --package com.mygame.app --output MyGame.apk

# Release signing
./android/build_apk.sh --keystore path/to/release.jks
```

**Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `--tier <low\|mid\|high>` | `mid` | Asset quality tier |
| `--abi <arm64-v8a\|...>` | `arm64-v8a` | Target ABI |
| `--debug` | release | Debug build type |
| `--keystore <path>` | debug keystore | Keystore for signing |
| `--output <path>` | `build/android/Game.apk` | Output APK path |
| `--install` | off | Install via adb after build |
| `--app-name <name>` | `"Sama Game"` | Application display name |
| `--package <id>` | `com.sama.game` | Android package identifier |

**Prerequisites:** `ANDROID_NDK`, `ANDROID_SDK_ROOT` (or `ANDROID_HOME`), build-tools
(aapt2, zipalign, apksigner), platform (android.jar), Java 17+.

A debug keystore is auto-created at `$HOME/.android/debug.keystore` on first build.

Source: `android/build_apk.sh`, `android/build_android.sh`, `android/create_debug_keystore.sh`.

### AAB Build for Play Store (`android/build_aab.sh`)

Build an Android App Bundle for Play Store distribution.

```bash
# Build AAB with both ABIs (arm64-v8a + armeabi-v7a)
./android/build_aab.sh --tier high --keystore release.jks --package com.mygame.app --output MyGame.aab

# arm64-v8a only (faster build, covers modern devices)
./android/build_aab.sh --tier mid --skip-armeabi --output MyGame.aab

# Unsigned AAB (sign later before Play Store upload)
./android/build_aab.sh --tier mid

# Test locally
bundletool build-apks --bundle=MyGame.aab --output=Game.apks --local-testing
bundletool install-apks --apks=Game.apks
```

**Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `--tier <low\|mid\|high>` | `mid` | Asset quality tier |
| `--keystore <path>` | unsigned | Keystore for signing |
| `--ks-pass <password>` | prompted | Keystore password |
| `--ks-alias <alias>` | `sama` | Key alias in keystore |
| `--key-pass <password>` | prompted | Key password |
| `--output <path>` | `build/android/Game.aab` | Output AAB path |
| `--app-name <name>` | `"Sama Game"` | Application display name |
| `--package <id>` | `com.sama.game` | Android package identifier |
| `--skip-armeabi` | off | Skip armeabi-v7a build (arm64 only) |

**Prerequisites:** `bundletool` (`brew install bundletool`), `jarsigner` (JDK),
`ANDROID_NDK`, `ANDROID_SDK_ROOT`, `aapt2`, `cmake`.

Source: `android/build_aab.sh`.

### TierConfig & Tier System

Configure device-tier quality presets in `ProjectConfig`. Each tier bundles asset
quality and render quality into a single profile.

#### ProjectConfig with tiers

```cpp
#include "engine/game/ProjectConfig.h"

using namespace engine::game;

// Load config with tier definitions from JSON
ProjectConfig config;
config.loadFromFile("project.json");

// Or set tiers programmatically
config.activeTier = "low";

// Get the resolved TierConfig (checks user tiers, then built-in defaults)
TierConfig tier = config.getActiveTier();
// tier.maxTextureSize, tier.shadowMapSize, tier.enableIBL, etc.

// Convert tier to RenderSettings for the renderer
auto rs = ProjectConfig::tierToRenderSettings(tier);
// rs.shadows.directionalRes, rs.lighting.iblEnabled, etc.

// Get built-in default tiers
auto defaults = defaultTiers();  // returns {"low", "mid", "high"}
```

#### JSON format

```json
{
    "activeTier": "mid",
    "tiers": {
        "low": {
            "maxTextureSize": 512,
            "textureCompression": "astc_8x8",
            "shadowMapSize": 512,
            "shadowCascades": 1,
            "maxBones": 64,
            "enableIBL": false,
            "enableSSAO": false,
            "enableBloom": false,
            "enableFXAA": true,
            "depthPrepass": false,
            "renderScale": 0.75,
            "targetFPS": 30
        }
    }
}
```

Partial definitions are supported -- unspecified fields keep `TierConfig` defaults
(equivalent to "mid").

#### TierConfig fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `maxTextureSize` | int | 1024 | Max texture dimension for this tier |
| `textureCompression` | string | "astc_6x6" | ASTC block size |
| `shadowMapSize` | int | 1024 | Shadow map resolution |
| `shadowCascades` | int | 2 | Number of CSM cascades |
| `maxBones` | int | 128 | Max skeleton bones |
| `enableIBL` | bool | true | Image-based lighting |
| `enableSSAO` | bool | false | Screen-space ambient occlusion |
| `enableBloom` | bool | true | Bloom post-process |
| `enableFXAA` | bool | true | Anti-aliasing |
| `depthPrepass` | bool | false | Depth prepass (false for mobile TBDR) |
| `renderScale` | float | 1.0 | Render resolution scale |
| `targetFPS` | int | 30 | Target frame rate |

Source: `engine/game/ProjectConfig.h/.cpp`

### TierAssetResolver

Resolve asset paths with per-tier overrides. Checks `<base>/<tier>/<relative>`
first, falls back to `<base>/<relative>`.

```cpp
#include "engine/assets/TierAssetResolver.h"

// Example: load tier-specific texture if available
std::string path = engine::assets::resolveAssetPath(
    "assets",           // base path
    "textures/sky.ktx", // relative path
    "low"               // tier name
);
// Returns "assets/low/textures/sky.ktx" if it exists,
// otherwise "assets/textures/sky.ktx"
```

Source: `engine/assets/TierAssetResolver.h/.cpp`

---

### Android game entry point (`samaCreateGame`)

On Android, games provide a factory function instead of `main()`:

```cpp
#include "engine/game/IGame.h"
#include "MyGame.h"

// Extern linkage -- the engine's AndroidApp.cpp calls this.
engine::game::IGame* samaCreateGame()
{
    return new MyGame();  // engine takes ownership, deletes on exit
}
```

The engine's `AndroidApp.cpp` bootstraps everything: creates a `GameRunner`,
calls `runAndroid(app)`, and deletes the game instance on exit. Your `IGame`
implementation is the same code as desktop -- no `#ifdef` needed.

Source: `engine/platform/android/AndroidApp.cpp`, `engine/game/GameRunner.h`

---

### Animation Editor (Graph View)

The editor's animation panel includes a visual state machine node graph
(`StateMachineGraphView`, `editor/platform/cocoa/StateMachineGraphView.h/.mm`).
When an entity with `AnimStateMachineComponent` is selected:

- State nodes render as rounded rectangles (120x50px) with name + clip label
- Green border = active state, blue = selected, draggable repositioning
- Bezier curve transition arrows with condition labels
- Click to select, double-click to force-set state, right-click for context menus
- Scroll wheel zoom (0.5x-2.0x), Option-drag to pan
- Auto-layout on first display (200x100 grid spacing)
- Fully synced with the list-based parameter panel

The animation panel is wrapped in `NSScrollView` for overflow content.

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
    transformSys.update(reg);    // recompute world matrices (BEFORE animation)
    smSys.update(reg, dt, animRes);       // state machine (BEFORE animation)
    animSys.update(reg, dt, animRes, arena);  // update animations (AFTER transform)

    eng.renderer().beginFrameDirect();
    // shadow pass, opaque pass, draw calls
    eng.endFrame();
}

// Release assets, shutdown subsystems
```

### HUD overlay pattern

Every game needs an in-game HUD eventually. The minimal recipe is:

```cpp
// Members on your IGame impl:
engine::ui::MsdfFont   hudFont_;
engine::ui::UiDrawList hudDrawList_;
engine::ui::UiRenderer ui_;

// onInit:
hudFont_.loadFromFile("assets/fonts/JetBrainsMono-msdf.json",
                      "assets/fonts/JetBrainsMono-msdf.png");
ui_.init();

// onRender, after the 3D scene pass:
hudDrawList_.clear();
hudDrawList_.drawText({12.f, 8.f}, "Score: 1000",
                      {1.f, 1.f, 1.f, 1.f}, &hudFont_, 18.f);

bgfx::setViewName(engine::rendering::kViewGameUi, "HUD");
bgfx::setViewRect(engine::rendering::kViewGameUi, 0, 0, fbW, fbH);
bgfx::setViewClear(engine::rendering::kViewGameUi, BGFX_CLEAR_NONE);
bgfx::touch(engine::rendering::kViewGameUi);
ui_.render(hudDrawList_, engine::rendering::kViewGameUi, fbW, fbH);

// onShutdown:
hudFont_.shutdown();
ui_.shutdown();
```

The HUD view runs after all post-processing so text isn't blurred by
bloom/FXAA. `BGFX_CLEAR_NONE` means it composites on top of the scene.
`UiRenderer::render` writes one rect-pass draw call plus one draw call
per unique font program (typically just one). Zero per-frame heap
allocations after init.

For a richer HUD with widgets (panels, buttons, progress bars, sliders)
see the "UI / 2D Text" section above and `apps/ui_test/UiTestApp.cpp`
for a worked example.

### IGame pattern (recommended for new games -- works on desktop and Android)

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

// Desktop entry point:
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

**Android entry point** (in a separate `_android.cpp` file):

```cpp
#include "engine/game/IGame.h"
#include "MyGame.h"

// The engine calls this to get your game instance on Android.
// AndroidApp.cpp handles the GameRunner lifecycle.
engine::game::IGame* samaCreateGame()
{
    return new MyGame();
}
```

The `IGame` class is 100% platform-agnostic -- the same implementation runs
on both desktop and Android. Only the entry point differs. See
[docs/ANDROID_SUPPORT.md](ANDROID_SUPPORT.md) for the full cross-platform
pattern.

### GameRunner API

```cpp
// Desktop
GameRunner runner(game);
runner.setFixedRate(60);                      // optional: default 60Hz
int exitCode = runner.run("project.json");    // init + loop + shutdown
int exitCode = runner.run(EngineDesc{...});   // or pass EngineDesc directly

// Android (called by AndroidApp.cpp -- you rarely call this directly)
int exitCode = runner.runAndroid(app, "project.json");
int exitCode = runner.runAndroid(app, EngineDesc{...});
```
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
| `AnimatorComponent` | `clipId`, `nextClipId`, `playbackTime`, `prevPlaybackTime`, `speed`, `blendFactor`, `blendDuration`, `blendElapsed`, `flags` | Animation playback state (36 bytes). `prevPlaybackTime` tracks time at frame start for event detection. |
| `SkinComponent` | `boneMatrixOffset`, `boneCount` (both uint32) | Offset into per-frame bone matrix buffer. |
| `AnimationEventQueue` | `events` (vector of AnimationEventRecord) | Per-entity queue of animation events that fired this frame. Game code polls and clears. |
| `AnimStateMachineComponent` | `machine` (const AnimStateMachine*), `currentState`, `params` (unordered_map) | Per-entity state machine runtime state. Set parameters via `setFloat()`/`setBool()`. The shared `AnimStateMachine` has a `paramNames` map (hash -> string) for editor display. |
| `PoseComponent` | `pose` (Pose*) | Arena-allocated FK pose, valid for one frame. |
| `IkChainsComponent` | `chains` (InlinedVector of IkChainDef) | IK chain definitions (up to 4 inline). |
| `IkTargetsComponent` | `targets` (InlinedVector of IkTarget) | World-space IK targets (parallel to chains). |

### Audio Components (`engine::audio`)

| Component | Fields | Purpose |
|-----------|--------|---------|
| `AudioSourceComponent` | `clipId`, `busHandle`, `volume`, `minDistance`, `maxDistance`, `pitch`, `category` (SoundCategory), `flags` | Sound emitter. Flags: bit0=loop, bit1=spatial, bit2=autoPlay, bit3=playing. |
| `AudioListenerComponent` | `priority` (uint8) | Marks entity as the audio listener. Highest priority wins. |

### Input Data (`engine::input`)

| Struct | Fields | Purpose |
|--------|--------|---------|
| `GyroState` | `pitchRate`, `yawRate`, `rollRate` (float, rad/s), `gravityX/Y/Z` (float, normalized), `available` (bool) | Gyroscope angular velocity and accelerometer gravity vector. Access via `inputState.gyro()`. `available` is false on desktop (GLFW). |

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

On Android, content scale is derived from display density (160 dpi = 1.0x baseline).
The same `eng.contentScaleX()` / `eng.contentScaleY()` API works on both platforms.

### Using `eng.window()` in cross-platform code

**Symptom:** Compile error on Android -- `window()` is not available.

`Engine::window()` is desktop-only (guarded by `#ifndef __ANDROID__`). If your game
needs the window for cursor capture or scroll callbacks, guard those calls:

```cpp
#ifndef __ANDROID__
    auto& win = engine.window();
    // desktop-only window operations
#endif
```

Most games do not need `window()` -- use `eng.inputState()` instead, which works on
both platforms.

### Calling bgfx directly from game code

**Symptom:** Code doesn't work on Android, or duplicates engine boilerplate.

Game code should never call `bgfx::` functions directly. Use engine abstractions:

| Instead of | Use |
|---|---|
| `bgfx::setViewClear(0, ...)` | `engine.setClearColor(rgba)` |
| `bgfx::setViewRect` + `bgfx::touch` + `uiRenderer.render()` | Just `uiRenderer.render(drawList, viewId, w, h)` — view setup is internal |
| `bgfx::dbgTextPrintf()` | `engine::ui::DebugHud` (works on Android too) |
| `bgfx::createTexture2D` (white fallback) | `engine.resources().whiteTex` |

### Collider half-extents vs entity scale

**Symptom:** Physics collider is the wrong size relative to the visual mesh.

`ColliderComponent::halfExtents` defines the physics shape size independently of
`TransformComponent::scale`. If your entity has `scale = {2, 2, 2}` and uses the
unit cube mesh (+-0.5), set `halfExtents = {1.0, 1.0, 1.0}` to match.

### Not releasing asset handles

**Symptom:** GPU memory leak.

Always call `assets.release(handle)` on shutdown. The handle is ref-counted; release
decrements it and schedules GPU handle destruction when it reaches zero.

### Animation system ordering

**Symptom:** Skinned mesh renders at the world origin instead of at the entity's position.

`TransformSystem` must run before `AnimationSystem` in the frame loop.
`AnimationSystem` reads `WorldTransformComponent::matrix` to bake the entity's world
transform into the bone matrices. If `TransformSystem` hasn't run yet, the world matrix
is stale or identity, and the skinned mesh renders at the origin.

```cpp
// CORRECT order:
transformSys.update(reg);                                    // first
animSys.update(reg, dt, animRes, eng.frameArena().resource());  // second
```

### glTF animations start paused

**Symptom:** Loaded glTF model with animations does not animate.

`GltfSceneSpawner` sets `kFlagLooping` but NOT `kFlagPlaying` on imported entities.
You must explicitly start playback:

```cpp
reg.view<AnimatorComponent>().each(
    [](EntityID, AnimatorComponent& ac)
    {
        ac.flags |= AnimatorComponent::kFlagPlaying;
    });
```

### Sidecar files must match the GLB base name

**Symptom:** Animation events or state machine not loaded when importing a glTF in the editor.

Sidecar files must be in the same directory as the `.glb` and share the same base name. For `assets/characters/Fox.glb`, the sidecar files must be `assets/characters/Fox.events.json` and `assets/characters/Fox.statemachine.json`. The editor strips the extension from the import path and appends `.events.json` / `.statemachine.json` -- if the names don't match, the files are silently ignored. Also note that sidecar files use clip names (not indices) as keys, so if you re-export a model and rename clips in Blender, the sidecar events will not match and will be skipped.

### UI font asset paths are cwd-relative

**Symptom:** `MsdfFont::loadFromFile()` returns false when the binary is launched from outside the repo root, even though the asset exists.

`loadFromFile` takes a plain path and resolves it against the current working directory, not the executable's location. If your app might be launched from anywhere (`./build/myapp` from `/tmp`, double-clicked from Finder, etc.), wrap the path in a small helper that walks upward from `_NSGetExecutablePath` until it finds the `assets/` directory. See the `findAsset` lambda in `apps/ui_test/UiTestApp.cpp::onInit` and `editor/EditorApp.cpp::init` for the canonical implementation.

### MSDF text appears at the wrong vertical position

**Symptom:** Lowercase letters float above the cap-height line, glyphs don't share a baseline.

`UiRenderer` interprets `cmd.position.y` as the **top of the line in y-down screen space**, and `GlyphMetrics::offset.y` as a y-down delta from there to the top of the glyph quad. `BitmapFont` matches this convention because BMFont's `yoffset` is exactly that. `MsdfFont` parses `planeBounds` from msdf-atlas-gen JSON which is **y-up baseline-relative** — `MsdfFont::loadFromFile` converts via `(ascender - planeBounds.top) * nominalSize`. If you write a new IFont backend, store offsets in the y-down line-relative convention or every glyph will be in the wrong place.

---

## 7. Error Messages Guide

| Symptom | Cause | Fix |
|---------|-------|-----|
| Pink/magenta rectangles | Missing texture or shader binding | Ensure all PBR texture slots are bound. Use `resources.whiteTexture()` as default for unset texture IDs. |
| Entity not rendering | Missing `VisibleTag`, `WorldTransformComponent`, or `MeshComponent` | Add all required components (see Section 2, "Create an entity"). |
| Physics body not moving | Dirty flag not set after teleporting via `setBodyPosition` | PhysicsSystem writes back transforms with dirty flag; if you manually set position, also set `tc->flags \|= 1`. |
| Asset stays in Loading state | `processUploads()` not called, or wrong file path | Call `assets.processUploads()` every frame. Check `assets.error(handle)` for path errors. |
| "No loader registered" error | Forgot to register the loader for the file extension | Call `assets.registerLoader(std::make_unique<GltfLoader>())` and/or `TextureLoader` before loading. |
| Animation not playing | `kFlagPlaying` not set on `AnimatorComponent` | Set `ac.flags \|= AnimatorComponent::kFlagPlaying`. Note: glTF imports start paused (only `kFlagLooping` is set). |
| Skinned mesh at world origin | `TransformSystem` runs after `AnimationSystem` | `TransformSystem` must run BEFORE `AnimationSystem` so `WorldTransformComponent` is current for bone matrix computation. |
| Skinned mesh not visible | Missing `updateSkinned` call in draw pass | Call `drawCallSys.updateSkinned(...)` after `drawCallSys.update(...)` with the bone buffer from `animSys.boneBuffer()`. |
| Animation events not firing | Events suppressed or queue not polled | Events only fire when `kFlagPlaying` is set (not during `kFlagSampleOnce`). Clear `AnimationEventQueue` after consuming. |
| State machine not transitioning | `AnimStateMachineSystem::update()` not called | Call `smSys.update(reg, dt, animRes)` BEFORE `animSys.update()` each frame. |
| Sidecar events/SM not loaded on import | File name mismatch or wrong directory | Sidecar files must be alongside the `.glb` with matching base name: `Model.events.json`, `Model.statemachine.json`. |
| Shadow not appearing | Shadow pass not submitted, or shadow atlas not bound | Call `eng.shadow().beginCascade(...)` + `submitShadowDrawCalls(...)` before the opaque pass. Pass `eng.shadow().atlasTexture()` in `PbrFrameParams`. |
| Crash on shutdown | AssetManager destroyed after bgfx context | Ensure AssetManager is constructed after Renderer (LIFO destruction order). Release all handles before shutdown. |

---

## 8. Android Game Development

### The `samaCreateGame()` Pattern

On Android, there is no `main()`. Instead, games define a factory function that the engine calls:

```cpp
// In your game's .cpp file (or a separate _android.cpp):
#include "engine/game/IGame.h"
#include "MyGame.h"

engine::game::IGame* samaCreateGame()
{
    return new MyGame();
}
```

The engine's `AndroidApp.cpp` calls this, wraps the result in a `GameRunner`, and runs the same frame loop as desktop. Your `IGame` class is identical on both platforms -- no `#ifdef` needed.

### Build Commands

```bash
# Build and install APK (default: arm64-v8a, mid tier, release)
./android/build_apk.sh --install

# Build with specific tier and debug mode
./android/build_apk.sh --tier high --debug --install

# Just build the native library (no APK packaging)
./android/build_android.sh arm64-v8a Release

# Manual CMake cross-compile
cmake -S . -B build/android/arm64-v8a \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release \
    -DSAMA_ANDROID=ON
cmake --build build/android/arm64-v8a -j$(sysctl -n hw.ncpu)
```

### Adding Your Game to the Android Build

1. Add your game's `.cpp` files to the `sama_android` target in `CMakeLists.txt`:
   ```cmake
   if(SAMA_ANDROID)
       target_sources(sama_android PRIVATE
           apps/my_game/MyGame.cpp
           apps/my_game/MyGame_android.cpp
       )
   endif()
   ```
2. Build: `./android/build_apk.sh --tier mid --install`

### Logcat Debugging

```bash
# Filter to Sama engine logs
adb logcat -s SamaEngine

# Filter to bgfx + Sama
adb logcat -s SamaEngine:V bgfx:V

# See all logs (verbose, useful for crash stacks)
adb logcat | grep -i "sama\|bgfx\|signal\|FATAL"

# Clear and start fresh
adb logcat -c && adb logcat -s SamaEngine
```

### Shader Compilation for Android

SPIRV shaders must be pre-compiled before building the APK:

```bash
# Compile all engine shaders to SPIRV (run from project root)
./android/compile_shaders.sh

# Output goes to shaders/spirv/*.bin
# build_apk.sh automatically includes these in assets/shaders/spirv/
```

The script uses bgfx's `shaderc` tool to cross-compile `.sc` shader source files to SPIRV binary format. These `.bin` files are loaded at runtime via `AndroidFileSystem` (backed by `AAssetManager`).

If you add new shaders, add them to `compile_shaders.sh` and re-run before building the APK.

### Android Limitations (Current State)

- **UiRenderer + BitmapFont text works.** SPIRV shaders load from APK assets and render correctly via the UiRenderer system.
- **PBR not yet ported:** The full PBR pipeline (shadows, IBL, SSAO) requires porting additional shaders to SPIRV. UiRenderer and text rendering work, but 3D scene rendering is not yet available.
- **No ImGui:** The engine does not initialize ImGui on Android. `imguiWantsMouse()` returns false.
- **`beginFrameDirect` only:** The renderer bypasses the post-process framebuffer on Android. Rendering goes directly to the swapchain.
- **bgfx swapchain patch required:** bgfx's `NUM_SWAPCHAIN_IMAGE=4` is too small for some Vulkan drivers. The engine patches to 8 via CMake. See `docs/ANDROID_SUPPORT.md`.

### Reference: AndroidTestGame

See `apps/android_test/AndroidTestGame.cpp` for the canonical cross-platform game that works on both desktop and Android. It demonstrates:
- HSV color cycling background (works without shaders)
- Touch input with multi-finger tracking
- Gyroscope/accelerometer tilt response
- Debug text overlay
- Both `main()` (desktop) and `samaCreateGame()` (Android) entry points in one file

---

## 9. CLAUDE.md Integration

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
