# Skeletal Animation Architecture for the Nimbus Engine

## docs/ANIMATION_ARCHITECTURE.md

---

## 1. Overview

Skeletal animation adds the ability to deform meshes at runtime using a bone hierarchy (skeleton). The system introduces four core concepts:

- **Skeleton**: A hierarchy of joints (bones) with parent indices and inverse bind matrices that define the rest pose.
- **Animation Clip**: Keyframe data per joint -- position, rotation, and scale channels with timestamps -- describing a single animation (walk, run, idle).
- **Pose**: The evaluated state of all joints at a specific point in time -- an array of local TRS transforms.
- **Skinning**: The GPU process of blending bone matrices per vertex using bone indices and weights to deform the mesh.

The architecture follows the engine's existing patterns: sparse-set ECS components for animation state, a dedicated `AnimationSystem` that runs each frame before `TransformSystem`, CPU-side pose evaluation with GPU-side vertex skinning via `u_model[]` (bgfx's built-in bone matrix array), and glTF 2.0 as the sole import path using the already-integrated cgltf parser.

---

## 2. Data Structures

All CPU-side animation data lives in `engine/animation/`. These are plain data types with no GPU dependencies.

### 2.1 Skeleton

```cpp
// engine/animation/Skeleton.h
namespace engine::animation
{

struct Joint                              // offset  size
{
    math::Mat4 inverseBindMatrix{1.0f};   //  0      64  mesh space → bone-local space
    int32_t parentIndex = -1;             // 64       4  -1 = root joint (no parent)
    uint32_t nameHash = 0;               // 68       4  FNV-1a hash of joint name
    uint8_t _pad[8] = {};               // 72       8  trailing alignment padding
};  // total: 80 bytes (aligned to 16 for Mat4)
static_assert(sizeof(Joint) == 80);

struct Skeleton
{
    std::vector<Joint> joints;  // ordered such that parent always precedes child

    [[nodiscard]] uint32_t jointCount() const noexcept
    {
        return static_cast<uint32_t>(joints.size());
    }

    // Find joint by name hash. Returns index or -1 if not found.
    // Linear scan — fine for <128 joints, called at setup time (not per-frame).
    [[nodiscard]] int32_t findJoint(uint32_t hash) const noexcept
    {
        for (uint32_t i = 0; i < joints.size(); ++i)
            if (joints[i].nameHash == hash) return static_cast<int32_t>(i);
        return -1;
    }
};

}  // namespace engine::animation
```

**Joint layout rationale:**
- No `std::string` — avoids per-joint heap allocation. Joint names are stored as a 32-bit FNV-1a hash, which is sufficient for gameplay lookups (attach weapon to hand bone, IK targets, ragdoll mapping). The original string names can be recovered from the source glTF if needed for editor/debug tooling.
- 80 bytes per joint: 64B inverse bind matrix + 4B parent index + 4B name hash + 8B trailing padding (imposed by Mat4's 16-byte alignment). The hot loop (bone matrix computation) reads `inverseBindMatrix` and `parentIndex`; `nameHash` sits in the same cache line as `parentIndex` so it's free to include.
- `std::vector<Joint>` for storage: joints are loaded once from glTF and never modified. Single heap allocation at load time, not per-frame.

The `joints` array is topologically sorted: parent indices always reference earlier elements. This guarantees a single forward pass computes all world-space joint transforms without recursion.

### 2.2 AnimationClip

```cpp
// engine/animation/AnimationClip.h
namespace engine::animation
{

// One keyframe: timestamp + value.
template <typename T>
struct Keyframe
{
    float time;  // seconds
    T value;
};

// Per-joint channel data. Empty vectors mean the joint is not animated on that channel.
struct JointChannel
{
    uint32_t jointIndex;
    std::vector<Keyframe<math::Vec3>> positions;
    std::vector<Keyframe<math::Quat>> rotations;
    std::vector<Keyframe<math::Vec3>> scales;
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;           // total length in seconds
    std::vector<JointChannel> channels;
};

}  // namespace engine::animation
```

Keyframe arrays within each channel are sorted by timestamp. Sampling uses binary search (`std::upper_bound`) to find the enclosing pair, then interpolates: `glm::mix` for position/scale, `glm::slerp` for rotation.

### 2.3 Pose

```cpp
// engine/animation/Pose.h
namespace engine::animation
{

struct JointPose
{
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity
    math::Vec3 scale{1.0f};
};

struct Pose
{
    std::vector<JointPose> jointPoses;  // parallel to Skeleton::joints
};

}  // namespace engine::animation
```

---

## 3. ECS Components

Follow the existing convention in `engine/rendering/EcsComponents.h`: largest-alignment-first layout, explicit padding, `static_assert` on sizeof.

### 3.1 SkeletonComponent

```cpp
// Added to EcsComponents.h (or a new engine/animation/AnimationComponents.h)
struct SkeletonComponent
{
    uint32_t skeletonId;  // index into a shared skeleton asset table
};
static_assert(sizeof(SkeletonComponent) == 4);
```

References a `Skeleton` stored in a shared resource table (analogous to `RenderResources` for meshes). Multiple entities can share the same skeleton definition.

### 3.2 AnimatorComponent

```cpp
struct AnimatorComponent                // offset  size
{
    uint32_t clipId;                    //  0       4  -- current clip index
    uint32_t nextClipId;                //  4       4  -- blend target clip (UINT32_MAX = none)
    float playbackTime;                 //  8       4  -- current time in seconds
    float speed;                        // 12       4  -- playback rate multiplier
    float blendFactor;                  // 16       4  -- 0.0 = current, 1.0 = fully blended to next
    float blendDuration;               // 20       4  -- total crossfade duration
    float blendElapsed;                // 24       4  -- elapsed crossfade time
    uint8_t flags;                      // 28       1  -- bit 0: looping, bit 1: playing, bit 2: blending
    uint8_t _pad[3];                    // 29       3
};  // total: 32 bytes
static_assert(sizeof(AnimatorComponent) == 32);
```

### 3.3 SkinComponent

```cpp
struct SkinComponent
{
    uint32_t boneMatrixOffset;   // offset into the per-frame bone matrix buffer
    uint32_t boneCount;          // number of bones for this skin
};
static_assert(sizeof(SkinComponent) == 8);
```

The `boneMatrixOffset` indexes into a frame-local array of `mat4` values that `AnimationSystem` writes and the renderer reads. This indirection allows the renderer to upload exactly the needed bone matrices without knowing about the animation state.

---

## 4. AnimationSystem

A new system in `engine/animation/AnimationSystem.h`. In the DAG, it declares `Reads = TypeList<SkeletonComponent>` and `Writes = TypeList<AnimatorComponent, SkinComponent>`. It must execute before `TransformSystem` and before `DrawCallBuildSystem`.

### 4.1 Per-Frame Update

```
AnimationSystem::update(Registry& reg, float deltaTime, AnimationResources& animRes)
{
    for each entity with (SkeletonComponent, AnimatorComponent, SkinComponent):
        1. Advance playbackTime by deltaTime * speed
        2. Handle looping (wrap) or clamping
        3. Sample the current AnimationClip at playbackTime → Pose A
        4. If blending: sample next clip at playbackTime → Pose B,
           advance blendElapsed, compute blendFactor = blendElapsed / blendDuration
           Pose = lerp(A, B, blendFactor) per joint (mix position/scale, slerp rotation)
           If blendFactor >= 1.0: promote next clip to current, clear blend state
        5. Compute final bone matrices:
           - Forward pass over joints (parent-first ordering guaranteed by Skeleton):
             worldTransform[i] = worldTransform[parent[i]] * localTRS(pose[i])
           - finalMatrix[i] = worldTransform[i] * inverseBindMatrix[i]
        6. Write finalMatrix[0..boneCount-1] into the shared bone matrix buffer
           at boneMatrixOffset
}
```

### 4.2 Bone Matrix Buffer

A per-frame `std::vector<math::Mat4>` owned by `AnimationSystem` (or an `AnimationResources` object). Each skinned entity reserves `boneCount` contiguous slots. The renderer reads this buffer when submitting draw calls for skinned meshes.

### 4.3 Clip Blending

Crossfade blending is handled at the pose level:
- Position/scale: `glm::mix(a, b, t)`
- Rotation: `glm::slerp(a, b, t)`

This is sufficient for walk-to-run transitions, idle-to-action blends, and similar gameplay needs. Additive blending and animation layers are deferred to a future phase.

---

## 5. GPU Skinning

### 5.1 bgfx Bone Matrix Upload

bgfx provides built-in skeletal animation support through `u_model[BGFX_CONFIG_MAX_BONES]`. The existing `vs_pbr.sc` already references this array but only uses `u_model[0]` for the world transform. For skinned meshes, `bgfx::setTransform` accepts a pointer and a count:

```cpp
// Upload bone matrices for a skinned draw call.
// bgfx stores them in u_model[0..count-1].
bgfx::setTransform(boneMatrices, boneCount);
```

`BGFX_CONFIG_MAX_BONES` defaults to 32 in bgfx. This must be increased to 128 via a compile definition (`-DBGFX_CONFIG_MAX_BONES=128` in CMakeLists.txt for the bgfx target). Each `u_model[i]` is a `mat4` (4 vec4 uniforms), so 128 bones = 512 vec4 uniforms, which fits within bgfx's default `BGFX_CONFIG_MAX_UNIFORMS = 512`.

**Important**: When `bgfx::setTransform` is called with count > 1, bgfx uploads the matrices into the `u_model[]` array. The vertex shader then indexes into this array using bone indices from the vertex attributes. The world transform of the skinned entity is baked into the bone matrices themselves (each final bone matrix = entityWorldTransform * jointWorldTransform * inverseBindMatrix).

### 5.2 Vertex Shader Modifications

A new shader variant `vs_pbr_skinned.sc` (or a `#define SKINNED` branch in `vs_pbr.sc`):

```glsl
$input a_position, a_normal, a_tangent, a_texcoord0, a_indices, a_weight

// ... existing includes and functions ...

void main()
{
#if SKINNED
    // Compute skinned position.
    ivec4 indices = ivec4(a_indices);
    vec4 weights = a_weight;

    mat4 skinMatrix =
        weights.x * u_model[indices.x] +
        weights.y * u_model[indices.y] +
        weights.z * u_model[indices.z] +
        weights.w * u_model[indices.w];

    vec4 worldPos = mul(skinMatrix, vec4(a_position, 1.0));
    v_worldPos = worldPos.xyz;
    gl_Position = mul(u_viewProj, worldPos);

    mat3 m3 = mtxFromCols3(skinMatrix[0].xyz, skinMatrix[1].xyz, skinMatrix[2].xyz);
#else
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPos = worldPos.xyz;
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

    mat3 m3 = mtxFromCols3(u_model[0][0].xyz, u_model[0][1].xyz, u_model[0][2].xyz);
#endif

    // ... rest of normal/tangent transform and varyings unchanged ...
}
```

The `SKINNED` variant requires `u_viewProj` instead of `u_modelViewProj` because the model transform is already applied per-bone. bgfx provides `u_viewProj` as a built-in.

### 5.3 Varying Definition

Add to `varying.def.sc` (or a separate `varying_skinned.def.sc`):

```
vec4 a_indices  : BLENDINDICES;
vec4 a_weight   : BLENDWEIGHT;
```

bgfx maps `BLENDINDICES` and `BLENDWEIGHT` to the appropriate platform semantics.

---

## 6. Vertex Format Changes

### 6.1 New Skinning Vertex Layout

Add a third vertex stream (Stream 2) for skinning data, keeping the existing two streams unchanged. This follows the engine's philosophy of only binding what each pass needs -- depth-only passes never bind skinning data.

```cpp
// In VertexLayouts.h
[[nodiscard]] inline bgfx::VertexLayout skinningLayout()
{
    bgfx::VertexLayout l;
    l.begin()
        .add(bgfx::Attrib::Indices,  4, bgfx::AttribType::Uint8, /*normalized=*/false)
        .add(bgfx::Attrib::Weight,   4, bgfx::AttribType::Uint8, /*normalized=*/true)
        .end();
    return l;  // stride: 8 bytes per vertex
}
```

Bone indices: 4x `uint8` (supports up to 256 bones per skeleton, sufficient for the 128-bone max). Bone weights: 4x `uint8` normalized (0-255 mapped to 0.0-1.0, weights must sum to 1.0). Total cost: 8 bytes per vertex for skinned meshes only.

### 6.2 MeshData Extension

```cpp
// In MeshBuilder.h, add to MeshData:
    // Stream 2 — skinning (optional, empty for static meshes)
    // boneIndices: 4 uint8 per vertex
    std::vector<uint8_t> boneIndices;
    // boneWeights: 4 uint8 per vertex (normalized: 255 = 1.0)
    std::vector<uint8_t> boneWeights;
```

### 6.3 Mesh Extension

```cpp
// In Mesh.h, add:
    bgfx::VertexBufferHandle skinningVbh = BGFX_INVALID_HANDLE;
```

The `buildMesh` function is extended: if `boneIndices` and `boneWeights` are non-empty, create and assign `skinningVbh`. The `destroy()` method must also handle `skinningVbh`.

---

## 7. glTF Integration

cgltf already parses `skins`, `animations`, and joint references. The engine currently ignores them (the `convertPrimitive` function skips `cgltf_attribute_type_joints` and `cgltf_attribute_type_weights`).

### 7.1 GltfLoader Extensions

In `GltfLoader.cpp::convertPrimitive()`, add extraction of `JOINTS_0` and `WEIGHTS_0` attributes:

```cpp
case cgltf_attribute_type_joints:
    if (attr.index == 0)
        jointsAcc = attr.data;
    break;
case cgltf_attribute_type_weights:
    if (attr.index == 0)
        weightsAcc = attr.data;
    break;
```

Read joints as uint8 (cgltf handles type conversion), read weights as float then quantize to uint8.

Add new extraction functions after the mesh loop in `GltfLoader::decode()`:

**Skeleton extraction** (`data->skins`):
- For each `cgltf_skin`: read `joints[]` (node pointers to joint indices), `inverse_bind_matrices` accessor, and `skeleton` root node.
- Build the `Skeleton` joint array in topological order (parent before child). glTF does not guarantee this ordering, so a topological sort is needed.
- Store as `CpuSkeletonData` in `CpuSceneData`.

**Animation extraction** (`data->animations`):
- For each `cgltf_animation`: iterate `channels`, each has a `target` (node + path: translation/rotation/scale) and a `sampler` (input=timestamps, output=values, interpolation mode).
- Read timestamps via `readFloatAccessor(sampler->input)`.
- Read values via `readFloatAccessor(sampler->output)` -- Vec3 for translation/scale, Quat for rotation (glTF stores quaternions as xyzw).
- Map the target node to a joint index via the skin's joint array.
- Store as `CpuAnimationClipData` in `CpuSceneData`.

### 7.2 CPU Asset Data Extensions

```cpp
// In CpuAssetData.h, add to CpuSceneData:

struct CpuJointData
{
    math::Mat4 inverseBindMatrix{1.0f};
    int32_t parentIndex = -1;
    std::string name;  // kept as string during loading; hashed to uint32_t at upload
};

struct CpuSkeletonData
{
    std::vector<CpuJointData> joints;
};

struct CpuAnimationClipData
{
    std::string name;
    float duration = 0.0f;
    // Channels stored as parallel arrays for cache efficiency
    struct Channel
    {
        uint32_t jointIndex;
        std::vector<float> positionTimes;
        std::vector<math::Vec3> positionValues;
        std::vector<float> rotationTimes;
        std::vector<math::Quat> rotationValues;
        std::vector<float> scaleTimes;
        std::vector<math::Vec3> scaleValues;
    };
    std::vector<Channel> channels;
};

// Add to CpuSceneData:
    std::vector<CpuSkeletonData> skeletons;
    std::vector<CpuAnimationClipData> animations;
    // Per-mesh: which skin (skeleton) index applies. -1 = no skin.
    // Parallel to meshes vector.
    std::vector<int32_t> meshSkinIndices;
```

### 7.3 GltfAsset Extensions

```cpp
// In GltfAsset.h, add:
    std::vector<animation::Skeleton> skeletons;
    std::vector<animation::AnimationClip> animations;
```

The `AssetManager::processUploads()` path converts `CpuSkeletonData` to `animation::Skeleton` and `CpuAnimationClipData` to `animation::AnimationClip`. No GPU upload needed for these -- they are CPU-only data.

---

## 8. Animation Asset Pipeline

### 8.1 Loading Path

```
GLB file
  → cgltf_parse (existing)
  → GltfLoader::decode
      → convertPrimitive (existing mesh + new JOINTS_0/WEIGHTS_0)
      → extractSkeletons (new: skins → CpuSkeletonData)
      → extractAnimations (new: animations → CpuAnimationClipData)
  → CpuSceneData (with skeletons, animations, meshSkinIndices)
  → AssetManager::processUploads
      → buildMesh (extended: creates skinningVbh for skinned meshes)
      → copy skeleton/animation data into GltfAsset
  → GltfSceneSpawner::spawn
      → attach SkeletonComponent, AnimatorComponent, SkinComponent
        to entities with skinned meshes
```

### 8.2 Interpolation

- **Position/Scale**: Linear interpolation (`glm::mix`). glTF `LINEAR` maps directly.
- **Rotation**: Spherical linear interpolation (`glm::slerp`). Handles shortest-path automatically.
- **Step interpolation**: glTF `STEP` mode returns the previous keyframe value with no interpolation.
- **Cubic spline**: glTF `CUBICSPLINE` is deferred to a future phase. Fall back to `LINEAR` for now and log a warning.

### 8.3 Keyframe Lookup

Binary search via `std::upper_bound` on the timestamp array, then interpolate between `[i-1]` and `[i]`. At the boundaries: before first keyframe returns the first value; after last keyframe returns the last value (clamped) or wraps to the first (looping).

---

## 9. DrawCallBuildSystem Integration

`DrawCallBuildSystem` must detect skinned entities and use the skinned shader + bone matrix upload path.

For entities with `SkinComponent`:
1. Look up the bone matrices from `AnimationSystem`'s per-frame buffer using `SkinComponent::boneMatrixOffset` and `boneCount`.
2. Call `bgfx::setTransform(boneMatrices, boneCount)` instead of the single-matrix `bgfx::setTransform(&wtc.matrix)`.
3. Bind vertex stream 2 (`skinningVbh`) via `bgfx::setVertexBuffer(2, mesh.skinningVbh)`.
4. Submit with the skinned shader program instead of the standard PBR program.

A new overload or conditional branch in `submitMeshDraw` handles this.

For shadow passes (`submitShadowDrawCalls`): skinned meshes also need bone matrices uploaded. A simpler `vs_shadow_skinned.sc` shader applies the same skinning math but outputs only `gl_Position`.

---

## 10. File Layout

```
engine/animation/
    Skeleton.h             -- Joint, Skeleton structs
    AnimationClip.h        -- Keyframe<T>, JointChannel, AnimationClip
    Pose.h                 -- JointPose, Pose
    AnimationSampler.h     -- sampleClip(), blendPoses() free functions
    AnimationSampler.cpp
    AnimationSystem.h      -- AnimationSystem class
    AnimationSystem.cpp
    AnimationComponents.h  -- SkeletonComponent, AnimatorComponent, SkinComponent
    AnimationResources.h   -- shared skeleton/clip storage (analogous to RenderResources)
    AnimationResources.cpp
```

New/modified shader files:
```
engine/shaders/
    vs_pbr_skinned.sc      -- PBR vertex shader with skinning
    vs_shadow_skinned.sc   -- Shadow vertex shader with skinning
    varying_skinned.def.sc -- varying def including a_indices, a_weight
```

---

## 11. Performance Considerations

### 11.1 Bone Limits

- **Maximum bones per skeleton**: 128. Fits in bgfx's `u_model[]` array with `BGFX_CONFIG_MAX_BONES=128` (512 vec4 uniforms). Typical humanoid characters use 20-70 bones; 128 covers complex creatures and vehicles.
- **Compile-time define**: Add `-DBGFX_CONFIG_MAX_BONES=128` to the bgfx target in CMakeLists.txt.

### 11.2 Bone Matrix Upload

Each skinned draw call uploads `boneCount * 64 bytes` to the GPU via `bgfx::setTransform`. For a 64-bone character this is 4 KB per draw call. At 50 skinned characters, total per-frame upload is ~200 KB -- well within budget for both desktop and mobile.

No SSBO or texture-buffer alternative is needed at this scale. If the engine later needs hundreds of skinned characters, bone matrices can be packed into a texture buffer (similar to the existing light data texture approach in `NOTES.md`).

### 11.3 Animation Update Frequency

All animation sampling runs at the application frame rate. For distant or occluded characters, a future LOD system could reduce update frequency (e.g., every 2nd or 4th frame), but this is not part of the initial implementation.

### 11.4 Memory

- Skeleton: ~80 bytes per joint (64B inverse bind matrix + 4B parent + name). 128 joints = ~10 KB per skeleton. Shared across all instances.
- AnimationClip: depends on keyframe density. A 2-second walk cycle at 30 fps with 64 bones, 3 channels (pos=Vec3, rot=Quat, scale=Vec3) = 64 * 60 * (12+16+12+4*3) = ~200 KB. Typical clips are smaller because most joints only have rotation channels.
- Pose: 40 bytes per joint * 128 = 5 KB per active entity. At 50 skinned entities = 250 KB.

### 11.5 Cache Efficiency

The `Skeleton::joints` array is topologically sorted, so the forward pass that computes world-space joint transforms accesses memory sequentially. Pose data is a flat array, also sequential. The hot loop (sample + compute bone matrices) touches ~10 KB per entity -- fits in L1.

---

## 12. Test Plan

### 12.1 Unit Tests (engine_tests, no GPU)

1. **TestSkeleton**: Construct a 4-joint chain skeleton (root -> spine -> arm -> hand). Verify `jointCount()`, parent indices, topological order.

2. **TestAnimationSampler**: Create a simple clip with known keyframes. Sample at exact keyframe times (expect exact values), between keyframes (expect interpolated values), before first keyframe (clamp), after last keyframe (clamp and wrap for looping).

3. **TestPoseBlend**: Two poses for the same skeleton. Blend at t=0.0 (pose A), t=1.0 (pose B), t=0.5 (midpoint). Verify position `mix`, rotation `slerp`, scale `mix`.

4. **TestBoneMatrixComputation**: 3-joint chain with known local transforms and inverse bind matrices. Compute final bone matrices. Verify against hand-calculated expected values.

5. **TestGltfSkeletonExtraction**: Load a GLB with a skinned mesh (e.g., a simple rigged cube). Verify skeleton joint count, parent indices, inverse bind matrices match expected values. Verify `JOINTS_0` and `WEIGHTS_0` are extracted into `MeshData::boneIndices` and `boneWeights`.

6. **TestGltfAnimationExtraction**: Load a GLB with an animation. Verify clip name, duration, channel count, keyframe timestamps and values.

7. **TestAnimatorComponent**: Verify playback time advancement, looping wrap, speed multiplier, blend state transitions.

### 12.2 Screenshot Tests (engine_screenshot_tests, requires GPU)

8. **TestSsSkinnedMesh**: Load a rigged GLB, set a known animation time, render one frame. Compare against golden image. Verifies the full pipeline: glTF load -> skeleton/skin -> bone matrices -> skinned vertex shader -> correct rendered output.

### 12.3 Manual Validation

9. **Animation demo app** (`apps/animation_demo`): Load a rigged character GLB (e.g., the Khronos `RiggedSimple` or `Fox` sample model). Play an animation with looping, show bone count and FPS overlay via ImGui. Test crossfade blending between two clips.

---

## 13. Implementation Sequence

| Phase | Description | Dependencies |
|-------|-------------|--------------|
| A | Data structures: `Skeleton`, `AnimationClip`, `Pose`, `AnimationSampler` | None |
| B | ECS components: `SkeletonComponent`, `AnimatorComponent`, `SkinComponent` | Phase A |
| C | `AnimationSystem`: pose sampling, blending, bone matrix computation | Phases A, B |
| D | glTF integration: extend `GltfLoader` to extract skins and animations | Phases A, cgltf |
| E | Vertex format: add `boneIndices`/`boneWeights` to `MeshData`, `skinningLayout()`, `Mesh::skinningVbh` | Phase D |
| F | GPU skinning: `vs_pbr_skinned.sc`, `vs_shadow_skinned.sc`, `varying_skinned.def.sc`, CMake shader compilation | Phase E |
| G | Renderer integration: `DrawCallBuildSystem` skinned path, `BGFX_CONFIG_MAX_BONES=128` | Phases C, E, F |
| H | `GltfSceneSpawner` extension: attach animation components to skinned entities | Phases B, D, G |
| I | Tests and demo app | All above |

Phases A-C are pure CPU with no rendering dependency and can be developed and tested immediately. Phase D extends existing code (GltfLoader) with additive changes. Phases E-G touch the rendering pipeline. Phase H wires everything together.

---

### Critical Files for Implementation
- /Users/shayanj/claude/engine/engine/assets/GltfLoader.cpp
- /Users/shayanj/claude/engine/engine/rendering/VertexLayouts.h
- /Users/shayanj/claude/engine/engine/shaders/vs_pbr.sc
- /Users/shayanj/claude/engine/engine/rendering/systems/DrawCallBuildSystem.cpp
- /Users/shayanj/claude/engine/engine/rendering/MeshBuilder.h