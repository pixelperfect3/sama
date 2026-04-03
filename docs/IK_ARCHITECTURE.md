# Inverse Kinematics Architecture for the Nimbus Engine

## docs/IK_ARCHITECTURE.md

---

## 1. Overview

Inverse Kinematics (IK) computes joint rotations that place an end effector (e.g. a hand, foot, or head) at a desired world-space position. Where Forward Kinematics (FK) propagates rotations from root to tip to produce a pose, IK works backward: given a target, it solves for the joint rotations that reach it.

IK matters for any engine that aims to produce believable character animation:
- **Foot placement** -- planting feet on uneven terrain instead of floating or clipping through slopes.
- **Look-at** -- orienting a character's head and spine toward a point of interest.
- **Hand reach** -- grabbing objects, touching surfaces, aiming weapons.
- **Procedural animation** -- spider legs, tentacles, robotic arms driven entirely by targets rather than canned clips.

### Where IK Fits in the Pipeline

IK is a **post-process on top of FK**. The existing `AnimationSystem` samples clips and produces a Pose (an array of local joint TRS transforms). The new `IkSystem` takes that FK pose, applies IK solvers to modify specific joint chains, and writes the corrected pose back before bone matrices are computed and uploaded to the GPU.

```
AnimationSystem::update()        -- FK: sample clips -> Pose
    |
IkSystem::update()               -- IK: modify Pose in-place for active chains
    |
Bone matrix computation          -- worldTransform[i] * inverseBindMatrix[i]
    |
GPU upload (bgfx::setTransform)  -- skinned vertex shader
```

This ordering means IK never conflicts with animation blending or clip playback. It simply refines the result. A blend weight on each IK chain controls how much the IK correction overrides the FK pose (0.0 = pure FK, 1.0 = pure IK), enabling smooth transitions when IK activates or deactivates.

---

## 2. Solvers

All solvers operate in **local joint space** -- they receive the FK pose (array of `JointPose` with local position, rotation, scale) and the skeleton hierarchy, compute world-space positions internally, solve for new rotations, and write back modified local rotations into the pose. This ensures the rest of the pipeline (bone matrix computation, GPU upload) remains unchanged.

### 2.1 Two-Bone IK (Analytical)

**Use cases:** Arms (shoulder-elbow-wrist), legs (hip-knee-ankle). The most common IK solver in games.

**Algorithm overview:**

Two-bone IK has a closed-form solution based on the law of cosines. Given three joints (root, mid, tip) and a target position:

1. Compute the distance from root to target (`d`), root-to-mid bone length (`a`), and mid-to-tip bone length (`b`).
2. If `d >= a + b`, the chain is fully extended -- point all joints toward the target.
3. If `d <= |a - b|`, the target is too close -- clamp to minimum reach.
4. Otherwise, use the law of cosines to find the angle at the mid joint:
   `cos(theta) = (a^2 + b^2 - d^2) / (2ab)`
5. The pole vector determines the plane in which the mid joint bends (e.g. the elbow bends "outward" not "inward"). Project the pole vector onto the plane perpendicular to the root-to-target axis to define the bend direction.
6. Compute the required rotations for the root and mid joints that place the tip at the target with the mid joint in the pole vector plane.

**Input parameters:**

```cpp
struct TwoBoneIkParams
{
    uint32_t rootJoint;         // joint index (e.g. upper arm / thigh)
    uint32_t midJoint;          // joint index (e.g. forearm / shin)
    uint32_t tipJoint;          // joint index (e.g. hand / foot) -- the end effector
    math::Vec3 targetPos;       // world-space target for the tip
    math::Vec3 poleVector;      // world-space point that controls bend direction
    float weight;               // 0.0 = FK, 1.0 = IK
};
```

**Output:** Modified `rotation` fields in `JointPose` for `rootJoint` and `midJoint`. The tip joint rotation can optionally be overridden to match a target orientation (e.g. keeping the foot flat on the ground).

**Complexity:** O(1) -- constant time, single evaluation, no iteration. This is the cheapest possible IK solver.

**Joint constraints:** Two-bone IK naturally respects the single-axis bend implied by the pole vector. Additional min/max angle limits on the mid joint (e.g. a knee cannot hyperextend past 180 degrees) can be applied as a post-solve clamp.

### 2.2 CCD (Cyclic Coordinate Descent)

**Use cases:** Spines, tails, tentacles, longer chains where an analytical solution does not exist.

**Algorithm overview:**

CCD is an iterative solver that adjusts one joint at a time, starting from the joint closest to the end effector and working toward the root:

1. For each iteration:
   a. For each joint in the chain, from tip-1 back to root:
      - Compute the vector from this joint's world position to the end effector's current world position.
      - Compute the vector from this joint's world position to the target.
      - Find the rotation that aligns the first vector to the second.
      - Apply that rotation to the joint's local rotation.
      - Recompute downstream world positions.
   b. Check if the end effector is within the convergence threshold of the target. If yes, stop early.
2. Repeat until converged or max iterations reached.

**Input parameters:**

```cpp
struct CcdIkParams
{
    memory::InlinedVector<uint32_t, 8> chainJoints;  // ordered root-to-tip
    uint32_t endEffector;       // tip joint index
    math::Vec3 targetPos;       // world-space target
    float weight;               // blend weight
    uint32_t maxIterations;     // typically 10-20
    float tolerance;            // convergence threshold in world units (e.g. 0.001)
    float dampingFactor;        // 0.0-1.0, limits rotation per step to prevent overshoot
};
```

**Output:** Modified `rotation` fields for all joints in the chain.

**Complexity:** O(n * k) where n = chain length, k = iterations. Typically n <= 8, k <= 15, so effectively constant for practical chains. Each iteration requires recomputing world-space positions for the chain (forward pass).

**Joint constraints:** After each single-joint rotation step, clamp the joint's Euler angles (or swing-twist decomposition) to per-joint min/max limits. This is where CCD can produce unnatural results if constraints are too tight -- the solver may oscillate near constraint boundaries. The damping factor helps mitigate this.

**Characteristics:** CCD tends to curl the chain -- joints near the tip move more than joints near the root. This looks natural for tails and tentacles but can appear odd for spines. The damping factor and per-joint weight multipliers can help distribute motion more evenly.

### 2.3 FABRIK (Forward And Backward Reaching Inverse Kinematics)

**Use cases:** Alternative to CCD for chains where more natural-looking results are desired. Good for spines, reaching arms, organic motion.

**Algorithm overview:**

FABRIK operates on joint positions rather than rotations, using a two-pass approach:

1. **Forward pass** (tip to root):
   a. Move the end effector to the target position.
   b. For each joint from tip-1 to root: move the joint to maintain its original distance from its child (the joint just processed). This "pulls" the chain toward the target.

2. **Backward pass** (root to tip):
   a. Move the root joint back to its original (FK) position (the root is anchored).
   b. For each joint from root+1 to tip: move the joint to maintain its original distance from its parent. This enforces bone length constraints.

3. Repeat forward+backward passes until the end effector is within tolerance of the target or max iterations reached.

4. **Convert positions back to rotations:** After convergence, compute the rotation for each joint that transforms its bone direction from the FK orientation to the solved orientation.

**Input parameters:**

```cpp
struct FabrikIkParams
{
    memory::InlinedVector<uint32_t, 8> chainJoints;  // ordered root-to-tip
    uint32_t endEffector;       // tip joint index
    math::Vec3 targetPos;       // world-space target
    float weight;               // blend weight
    uint32_t maxIterations;     // typically 5-10 (converges faster than CCD)
    float tolerance;            // convergence threshold in world units
};
```

**Output:** Modified `rotation` fields for all joints in the chain.

**Complexity:** O(n * k) where n = chain length, k = iterations. FABRIK typically converges in fewer iterations than CCD for the same chain length because each pass adjusts all joints simultaneously in a geometrically consistent way.

**Joint constraints:** Applied during the forward and backward passes by clamping each joint's position to a valid cone or hinge region relative to its parent. This is more intuitive than CCD's per-step rotation clamping because constraints operate in position space. However, joint constraints in FABRIK require converting back and forth between position and angular limits, which adds implementation complexity.

**Characteristics:** FABRIK tends to produce more evenly distributed joint motion compared to CCD's tip-heavy bias. It preserves bone lengths exactly (by construction). The position-based formulation makes it easy to understand and debug visually.

---

## 3. ECS Components

Following the existing conventions in `engine/animation/AnimationComponents.h`: largest-alignment-first layout, explicit padding, `static_assert` on sizeof.

### 3.1 IkChainComponent

Defines an IK chain on an entity. An entity can have multiple IK chains (e.g. left foot, right foot, spine look-at). Rather than a variable-length component, each `IkChainComponent` describes one chain, and entities with multiple chains use a small array component.

```cpp
// engine/animation/IkComponents.h
namespace engine::animation
{

enum class IkSolverType : uint8_t
{
    TwoBone = 0,
    Ccd     = 1,
    Fabrik  = 2,
};

struct IkChainDef                                // offset  size
{
    math::Vec3 poleVector{0.0f, 0.0f, 1.0f};    //  0      12  bend direction hint (world-space)
    float weight = 1.0f;                         // 12       4  FK/IK blend (0 = FK, 1 = IK)
    uint32_t rootJoint;                          // 16       4  first joint in chain
    uint32_t endEffectorJoint;                   // 20       4  last joint (tip)
    uint32_t midJoint;                           // 24       4  mid joint (TwoBone only, ignored otherwise)
    uint16_t maxIterations = 10;                 // 28       2  CCD/FABRIK max iterations
    IkSolverType solverType = IkSolverType::TwoBone; // 30   1
    uint8_t enabled = 1;                         // 31       1  quick toggle without removing component
};  // total: 32 bytes
static_assert(sizeof(IkChainDef) == 32);

struct IkChainsComponent
{
    memory::InlinedVector<IkChainDef, 4> chains;  // up to 4 chains inline, spills to heap
};

}  // namespace engine::animation
```

**Rationale:**
- `InlinedVector<IkChainDef, 4>` stores up to 4 chains (both feet + both hands, or feet + spine + head) without heap allocation. 4 * 32 = 128 bytes inline, which fits comfortably in two cache lines.
- `midJoint` is only used by Two-Bone IK. For CCD/FABRIK, the chain is defined implicitly by walking the skeleton hierarchy from `endEffectorJoint` up to `rootJoint`.
- `poleVector` is world-space by default. For characters that rotate, the system can transform it by the entity's world transform at solve time.

### 3.2 IkTargetComponent

World-space targets for each IK chain. Parallel to the chains in `IkChainsComponent`.

```cpp
struct IkTarget                              // offset  size
{
    math::Vec3 position{0.0f};               //  0      12  world-space target position
    math::Quat orientation{1, 0, 0, 0};      // 12      16  optional target orientation (e.g. foot flat)
    uint8_t hasOrientation = 0;              // 28       1  whether to apply orientation override
    uint8_t _pad[3] = {};                    // 29       3
};  // total: 32 bytes
static_assert(sizeof(IkTarget) == 32);

struct IkTargetsComponent
{
    memory::InlinedVector<IkTarget, 4> targets;  // parallel to IkChainsComponent::chains
};
```

**Rationale:**
- Separate from chain definitions because targets change every frame (driven by raycasts, look-at points, grabbed objects) while chain definitions are static setup data.
- The `orientation` field is optional -- foot IK uses it to keep the foot sole parallel to the ground surface; hand IK might use it to orient the palm toward a grip point.

### 3.3 Interaction with Existing Components

```
Entity (skinned character)
  +-- SkeletonComponent      (which skeleton asset)
  +-- AnimatorComponent       (FK clip playback state)
  +-- SkinComponent           (bone matrix offset for GPU upload)
  +-- IkChainsComponent       (chain definitions: solver type, joints, weights)
  +-- IkTargetsComponent      (per-frame target positions)
```

- `IkSystem` reads `SkeletonComponent` to access the skeleton hierarchy (parent indices, joint count).
- `IkSystem` reads `IkChainsComponent` and `IkTargetsComponent` for solve parameters.
- `IkSystem` writes to the Pose that `AnimationSystem` produced. This requires `AnimationSystem` to expose the intermediate Pose (before bone matrix computation) or for `IkSystem` to be integrated into `AnimationSystem::update()` between the pose sampling step and the bone matrix step.

### 3.4 Pose Handoff Between AnimationSystem and IkSystem

The current `AnimationSystem::update()` computes the FK pose and immediately converts it to bone matrices in a single pass. To insert IK between these steps, the system must expose the intermediate pose.

**Approach: Split AnimationSystem::update() into two phases.**

```cpp
// Phase 1: sample FK poses. Returns per-entity Pose data.
// Allocated from arena, valid until arena reset.
void AnimationSystem::updatePoses(Registry& reg, float dt, AnimationResources& animRes,
                                  std::pmr::memory_resource* arena);

// Phase 2: compute bone matrices from (potentially IK-modified) poses.
void AnimationSystem::computeBoneMatrices(Registry& reg, AnimationResources& animRes,
                                          std::pmr::memory_resource* arena);
```

The call sequence becomes:
```
animSys.updatePoses(reg, dt, animRes, arena);   // FK
ikSys.update(reg, animRes, arena);               // IK post-process
animSys.computeBoneMatrices(reg, animRes, arena); // bone matrices
```

The intermediate poses are stored in a per-entity `PoseComponent` (arena-allocated) or passed through a temporary buffer. Using a lightweight `PoseComponent` attached to each animated entity is the simplest approach -- `IkSystem` reads/writes it, and `computeBoneMatrices` consumes it.

```cpp
struct PoseComponent
{
    Pose* pose = nullptr;  // arena-allocated, valid for one frame
};
```

---

## 4. IkSystem

### 4.1 System Declaration

```cpp
// engine/animation/IkSystem.h
namespace engine::animation
{

class IkSystem
{
public:
    void update(ecs::Registry& reg, const AnimationResources& animRes,
                std::pmr::memory_resource* arena);
};

}  // namespace engine::animation
```

**DAG placement:**
- Reads: `SkeletonComponent`, `IkChainsComponent`, `IkTargetsComponent`, `PoseComponent`
- Writes: `PoseComponent` (modifies joint rotations in-place)
- Must run AFTER `AnimationSystem::updatePoses()` and BEFORE `AnimationSystem::computeBoneMatrices()`

### 4.2 Update Loop

```
IkSystem::update(reg, animRes, arena):
    for each entity with (SkeletonComponent, IkChainsComponent, IkTargetsComponent, PoseComponent):
        skeleton = animRes.getSkeleton(skelComp.skeletonId)
        pose = poseComp.pose

        // Compute world-space joint positions from the current (FK) pose.
        // Uses arena for temporary worldPositions array.
        worldPositions = computeWorldPositions(skeleton, pose, arena)

        for i in 0..chains.size():
            chain = chainsComp.chains[i]
            target = targetsComp.targets[i]

            if not chain.enabled or chain.weight <= 0.0:
                continue

            // Solve based on solver type.
            switch chain.solverType:
                case TwoBone:
                    solveTwoBone(skeleton, pose, worldPositions,
                                 chain.rootJoint, chain.midJoint, chain.endEffectorJoint,
                                 target.position, chain.poleVector)
                case Ccd:
                    buildChainFromHierarchy(skeleton, chain.rootJoint, chain.endEffectorJoint)
                    solveCcd(skeleton, pose, worldPositions, chainJoints,
                             target.position, chain.maxIterations, tolerance)
                case Fabrik:
                    buildChainFromHierarchy(skeleton, chain.rootJoint, chain.endEffectorJoint)
                    solveFabrik(skeleton, pose, worldPositions, chainJoints,
                                target.position, chain.maxIterations, tolerance)

            // Blend: interpolate between FK rotation and IK rotation per joint.
            if chain.weight < 1.0:
                for each modified joint:
                    pose.jointPoses[j].rotation = glm::slerp(fkRotation, ikRotation, chain.weight)

            // Optional: apply target orientation to end effector.
            if target.hasOrientation:
                pose.jointPoses[chain.endEffectorJoint].rotation = computeEndEffectorRotation(
                    skeleton, worldPositions, target.orientation)

        // Recompute world positions if multiple chains share joints.
        // (e.g. spine chain affects starting positions for arm chains)
```

### 4.3 Chain Construction for CCD/FABRIK

For CCD and FABRIK, the chain is not explicitly listed joint-by-joint -- only the root and end effector are specified. The chain is built at setup time (or cached) by walking the skeleton hierarchy from the end effector up to the root:

```
buildChainFromHierarchy(skeleton, rootJoint, endEffector) -> InlinedVector<uint32_t, 8>:
    chain = []
    current = endEffector
    while current != rootJoint and current >= 0:
        chain.push_front(current)
        current = skeleton.joints[current].parentIndex
    chain.push_front(rootJoint)
    return chain
```

This walk is O(chain length), typically 3-8 joints. It can be cached per-entity at spawn time in the `IkChainDef` if the skeleton does not change.

### 4.4 Multiple Chains and Ordering

When an entity has multiple IK chains, solve order matters if chains share joints:
- **Independent chains** (e.g. left foot and right foot) can be solved in any order.
- **Dependent chains** (e.g. spine look-at affects the starting position for an arm chain) must be solved root-to-tip: spine first, then arms.

The `IkChainsComponent::chains` array defines solve order implicitly -- chains are solved in array order. The user (or setup code) is responsible for ordering chains such that parent chains come before child chains.

### 4.5 Memory Usage

- **Per-frame temporaries** (world-space positions, intermediate chain data): allocated from `FrameArena`. For a 64-joint skeleton, world positions = 64 * 12 bytes = 768 bytes. Negligible.
- **Chain joint lists**: `InlinedVector<uint32_t, 8>` stores up to 8 joints inline (32 bytes). Chains longer than 8 joints spill to the heap, but such chains are rare in practice.
- **No persistent per-frame allocations**: all temporaries die with the arena reset.

---

## 5. Common Use Cases and Helpers

### 5.1 Foot Placement

The most common IK use case. Prevents feet from floating above or clipping through uneven terrain.

**Helper: `FootIkHelper`**

```cpp
namespace engine::animation
{

struct FootIkHelper
{
    // Per-foot configuration (set at spawn time).
    uint32_t hipJoint;          // e.g. "LeftUpLeg"
    uint32_t kneeJoint;         // e.g. "LeftLeg"
    uint32_t ankleJoint;        // e.g. "LeftFoot"
    float footHeight = 0.05f;   // distance from ankle joint to sole of foot

    // Compute the IK target for one foot.
    // Raycasts downward from the FK ankle position to find the ground.
    // Returns the target position (ground hit point + footHeight offset)
    // and the ground normal for orienting the foot.
    static IkTarget computeFootTarget(
        const math::Vec3& fkAnkleWorldPos,
        float footHeight,
        /* raycast function or physics query */);
};

// Adjust the pelvis (root) height so that the shorter leg still reaches the ground.
// Without this, one foot reaches its target but the other is pulled into the air.
void adjustPelvisHeight(Pose& pose, uint32_t pelvisJoint,
                        const math::Vec3& leftFootTarget,
                        const math::Vec3& rightFootTarget,
                        const math::Vec3& leftFkAnkle,
                        const math::Vec3& rightFkAnkle);

}  // namespace engine::animation
```

**Algorithm:**
1. For each foot, cast a ray downward from the FK ankle world position.
2. If the ray hits terrain, set the IK target to `hitPoint + surfaceNormal * footHeight`.
3. Set the foot orientation so the sole aligns with the surface normal.
4. Compute the difference in height between the two targets. Lower the pelvis by the larger offset so both feet can reach their targets.
5. IkSystem solves two Two-Bone IK chains (hip-knee-ankle) with the computed targets.

**Integration with physics:** The foot placement helper needs a raycast function. This can be provided via a callback or by querying the physics engine (JoltPhysicsEngine) directly. The helper itself does not depend on Jolt -- it takes a raycast result as input.

### 5.2 Look-At (Head/Spine Tracking)

Orient a character's head (and optionally spine) toward a world-space point of interest.

**Helper: `LookAtHelper`**

```cpp
struct LookAtHelper
{
    // Chain from spine base to head.
    // Typically 3-5 joints: spine1 -> spine2 -> neck -> head
    memory::InlinedVector<uint32_t, 6> chainJoints;

    // Per-joint weight distribution (how much each joint contributes).
    // e.g. spine1=0.1, spine2=0.2, neck=0.3, head=0.4
    // Sums to 1.0 (or close to it).
    memory::InlinedVector<float, 6> jointWeights;

    // Compute the IK target and set up a CCD or FABRIK chain.
    // The target is the look-at point; the solver distributes rotation
    // across the chain according to jointWeights.
    void setupChain(IkChainDef& outChain, IkTarget& outTarget,
                    const math::Vec3& lookAtPoint) const;
};
```

**Algorithm:**
1. The look-at point becomes the IK target position.
2. A CCD chain from spine base to head tip is created.
3. Per-joint weight distribution ensures the head rotates the most, with decreasing contribution down the spine. This prevents the character from bending entirely at the waist to look at something.
4. Horizontal and vertical angle limits prevent unnatural twisting (e.g. the head cannot rotate more than 80 degrees on any axis).

### 5.3 Hand Reach

Place a hand at a specific world position (grabbing an object, touching a wall, aiming).

**Algorithm:**
1. Two-Bone IK with shoulder-elbow-wrist.
2. The pole vector is set to keep the elbow pointing outward/backward (natural arm pose).
3. The wrist orientation can be overridden to match the grip orientation of the target object.
4. If the target is out of reach (distance > upper arm + forearm length), the arm extends fully toward the target. The blend weight can be reduced to soften this.

---

## 6. Testing Strategy

### 6.1 Unit Tests (Catch2, `engine_tests`)

All tests in `tests/animation/TestIkSolvers.cpp`.

**Two-Bone IK tests:**

1. **End effector reaches target within tolerance.** Construct a 3-joint chain (lengths 1.0, 1.0). Set target at (1.5, 0, 0). Solve. Verify end effector world position is within 0.001 of target.

2. **Pole vector controls bend direction.** Same chain, target at (1.0, 0, 0). Solve with pole vector at (0, 1, 0) -- mid joint should be above the root-target line. Solve again with pole vector at (0, -1, 0) -- mid joint should be below. Verify the mid joint world position is on the correct side.

3. **Unreachable target (fully extended).** Target at (3.0, 0, 0) for a chain with total reach of 2.0. Verify the chain extends fully toward the target and the end effector is at distance 2.0 along the target direction.

4. **Target at origin (degenerate case).** Target coincides with the root joint. Verify the solver does not produce NaN or infinite rotations.

**CCD tests:**

5. **Chain converges to target within max iterations.** 5-joint chain, target within reach. Verify end effector reaches target within tolerance after at most 15 iterations.

6. **Iteration count respected.** Set maxIterations = 1. Verify the solver does not exceed this count even if not converged.

**FABRIK tests:**

7. **Chain converges to target.** Same setup as CCD test. Verify convergence.

8. **Bone lengths preserved.** After solving, verify that the distance between each pair of adjacent joints matches the original FK bone lengths.

**Blend weight tests:**

9. **Weight 0.0 = pure FK.** Solve with weight 0.0. Verify all joint rotations are identical to the FK input.

10. **Weight 1.0 = pure IK.** Solve with weight 1.0. Verify the end effector reaches the target.

11. **Weight 0.5 = interpolated.** Verify that joint rotations are the slerp midpoint between FK and IK rotations.

**Joint constraint tests:**

12. **Mid joint angle stays within limits.** Two-bone IK with a knee joint limited to 0-150 degrees. Set a target that would require 170 degrees. Verify the angle is clamped to 150.

13. **CCD per-joint constraints.** Set per-joint angle limits on a CCD chain. Verify no joint exceeds its limits after solving.

### 6.2 Screenshot Tests (`engine_screenshot_tests`)

14. **IK-posed skeleton vs golden image.** Load a skinned model, set known IK targets, render one frame. Compare against a golden reference image. This verifies the full pipeline from IK solve through bone matrices to GPU skinning.

15. **Foot IK on uneven terrain.** Render a character standing on tilted platforms with foot IK enabled. Verify feet are planted (not floating or clipping) against a golden image.

---

## 7. IK Demo App

`apps/ik_demo/main.mm` -- follows the same structure as `apps/animation_demo/main.mm`.

### 7.1 Setup

```cpp
// Same boilerplate as animation_demo:
Engine eng;
EngineDesc desc;
desc.windowTitle = "IK Demo";
eng.init(desc);

// Asset loading
auto modelHandle = assets.load<GltfAsset>("BrainStem.glb");

// ECS systems
Registry reg;
DrawCallBuildSystem drawCallSys;
TransformSystem transformSys;
AnimationSystem animSys;
AnimationResources animRes;
IkSystem ikSys;

// Camera
OrbitCamera cam;
cam.distance = 5.0f;
cam.pitch = 15.0f;
cam.target = {0, 0.5f, 0};
```

### 7.2 Scene

- **Skinned character:** BrainStem.glb (or similar articulated model), playing an idle/walk animation on loop.
- **Terrain:** A ground plane with 3-4 tilted platform meshes at different heights and angles, created with `makeCubeMeshData()` and scaled/rotated to form uneven ground.
- **IK target marker:** A small sphere or cube rendered at the hand IK target position, draggable via ImGui.

### 7.3 Frame Loop

```
// Per frame:
animSys.updatePoses(reg, dt, animRes, arena);
ikSys.update(reg, animRes, arena);
animSys.computeBoneMatrices(reg, animRes, arena);
transformSys.update(reg);
// ... render as in animation_demo ...
```

### 7.4 ImGui Controls

```
ImGui window "IK Controls":
    [x] Enable Foot IK          -- checkbox, toggles foot chains enabled/disabled
    [x] Enable Hand IK          -- checkbox, toggles hand chain enabled/disabled
    [ ] Enable Look-At IK       -- checkbox, toggles spine/head chain

    IK Blend Weight  [====|===]  0.0 --- 1.0   -- slider, sets weight on all active chains

    Hand Target:
        X [===|===]  -2.0 --- 2.0   -- slider for hand IK target x
        Y [===|===]   0.0 --- 3.0   -- slider for hand IK target y
        Z [===|===]  -2.0 --- 2.0   -- slider for hand IK target z

    [ ] Show Joint Debug         -- checkbox, renders joint positions as small spheres
                                    and bone connections as lines (debug overlay)

    Solver Info:
        Active chains: 3
        Total iterations this frame: 12
        Solve time: 0.02 ms
```

### 7.5 Debug Visualization

When "Show Joint Debug" is enabled, render:
- Small cubes at each joint world position (using the existing cube mesh, scaled to 0.02).
- Lines connecting parent-child joints (using bgfx debug draw lines if available, or thin stretched cubes).
- IK target markers in a distinct color (yellow for active targets, grey for disabled).
- Pole vector indicators as a line from the mid joint toward the pole vector.

---

## 8. Implementation Phases

### Phase 1: Two-Bone IK Solver + IkSystem + Unit Tests

**Scope:**
- `engine/animation/IkSolvers.h` and `.cpp` -- `solveTwoBone()` free function
- `engine/animation/IkComponents.h` -- `IkChainDef`, `IkChainsComponent`, `IkTarget`, `IkTargetsComponent`, `PoseComponent`
- `engine/animation/IkSystem.h` and `.cpp` -- system update loop (Two-Bone only)
- Refactor `AnimationSystem::update()` into `updatePoses()` + `computeBoneMatrices()`
- `tests/animation/TestIkSolvers.cpp` -- unit tests 1-4, 9-11
- Update CMakeLists.txt to add new source files

**Deliverable:** Two-bone IK solving on a programmatic skeleton, verified by unit tests. No demo app yet.

### Phase 2: IK Demo App with Foot Placement

**Scope:**
- `apps/ik_demo/main.mm` -- demo app skeleton (model load, render, ImGui)
- `engine/animation/FootIkHelper.h` and `.cpp` -- foot target computation, pelvis adjustment
- Terrain geometry (tilted platforms)
- Wire up foot IK on BrainStem.glb (or suitable model with identifiable leg joints)
- ImGui controls for IK toggle, blend weight, debug visualization

**Deliverable:** Visual demo of foot IK planting feet on uneven terrain.

### Phase 3: CCD Solver + Look-At Helper

**Scope:**
- `solveCcd()` in `IkSolvers.h/.cpp`
- `engine/animation/LookAtHelper.h` -- look-at chain setup
- Unit tests 5-6
- Add look-at toggle to ik_demo (spine/neck/head chain targeting camera position or a fixed point)

**Deliverable:** CCD solver working for look-at chains in the demo.

### Phase 4: FABRIK Solver + Hand Reach

**Scope:**
- `solveFabrik()` in `IkSolvers.h/.cpp`
- Hand reach IK in ik_demo (draggable target via ImGui sliders)
- Unit tests 7-8

**Deliverable:** FABRIK solver working, hand IK with draggable target in the demo.

### Phase 5: Joint Constraints + Polish

**Scope:**
- Per-joint angle limits (min/max per axis, or swing-twist cone limits)
- `JointConstraint` struct stored alongside `IkChainDef` or in a separate component
- Apply constraints in all three solvers (post-solve clamp for TwoBone, per-step clamp for CCD, per-pass position clamp for FABRIK)
- Unit tests 12-13
- Screenshot tests 14-15
- Performance profiling and optimization pass

**Deliverable:** Fully constrained IK with visual and automated test verification.

---

## 9. Performance Considerations

### 9.1 Per-Frame Allocation

All IK temporaries (world-space positions, intermediate chain joint arrays, per-joint rotation backups for blending) are allocated from `FrameArena`. No heap allocations occur during IK solving.

Typical per-entity IK memory:
- World positions: 64 joints * 12 bytes = 768 bytes
- Chain joint arrays: 8 joints * 4 bytes = 32 bytes per chain
- FK rotation backup (for blending): 8 joints * 16 bytes = 128 bytes per chain
- Total per entity with 4 chains: ~1.3 KB

At 50 skinned characters: ~65 KB from the arena. Well within the default 1 MB arena budget.

### 9.2 Cache-Friendly Iteration

- Joint arrays in `Skeleton` are topologically sorted (parent-first). The forward pass to compute world-space positions is sequential memory access.
- `Pose::jointPoses` is an `InlinedVector<JointPose, 128>` -- contiguous memory, sequential access.
- IK chains are short (2-8 joints). The hot data for one chain solve fits entirely in L1 cache.

### 9.3 Cost Estimates

**Two-Bone IK:** ~50 floating-point operations (law of cosines + quaternion construction). Approximately 0.1 microseconds on modern hardware. Cost is negligible.

**CCD (8 joints, 10 iterations):** ~80 quaternion operations per iteration, 10 iterations = ~800 operations. Approximately 2-5 microseconds per chain.

**FABRIK (8 joints, 5 iterations):** ~40 vector operations per iteration, 5 iterations = ~200 operations plus rotation recovery (~8 quaternion constructions). Approximately 1-3 microseconds per chain.

**Budget at 60 fps (16.6 ms frame):**
- 50 entities * 4 chains * 5 us = 1 ms total IK solve time
- This is ~6% of the frame budget, which is acceptable for a character-heavy scene
- In practice, most entities will have 1-2 chains (feet only), and Two-Bone IK (the most common solver) is 50x cheaper than the iterative solvers

### 9.4 Future Optimization

If IK becomes a bottleneck:
- **LOD by distance:** Disable IK for distant characters or reduce iteration count.
- **Temporal coherence:** Use the previous frame's IK solution as the starting point for iterative solvers, reducing iterations needed for convergence.
- **SIMD:** Two-Bone IK's law-of-cosines math can be vectorized, though the per-chain overhead is already so low that the function call cost dominates.
- **Parallel solve:** Independent entities can be solved in parallel via the thread pool. The solve is embarrassingly parallel across entities. This would follow the same pattern as `FrustumCullSystem` (partition entities across workers).

---

## 10. Open Questions and Tradeoffs

### 10.1 Pose Storage: PoseComponent vs. Internal Buffer

**Option A (PoseComponent):** Attach a `PoseComponent` with an arena-allocated `Pose*` to each animated entity. `AnimationSystem` writes it, `IkSystem` reads/writes it, `AnimationSystem` reads it for bone matrices. Clean ECS separation but adds a component per entity.

**Option B (Internal buffer in AnimationSystem):** `AnimationSystem` stores an internal `std::pmr::vector<Pose>` indexed by entity. `IkSystem` accesses it via a reference. Tighter coupling but avoids the extra component.

**Recommendation:** Option A (PoseComponent). It follows the engine's ECS conventions, makes the data flow explicit in the DAG, and allows other systems (e.g. ragdoll, procedural animation) to also modify the pose without special-casing.

### 10.2 Joint Constraint Representation

**Swing-Twist decomposition** (cone + twist limit) is more physically intuitive than Euler angle limits, but harder to implement. Euler limits are simpler but suffer from gimbal lock.

**Recommendation:** Start with per-axis min/max angle limits (Euler) for Phase 5. If gimbal lock becomes a practical issue for specific joint configurations, add swing-twist as an alternative constraint type. Most game engines ship with Euler limits and they work fine for humanoid characters.

### 10.3 CCD vs. FABRIK Default for Chains

Both CCD and FABRIK solve the same problem. CCD is simpler to implement but tends to curl. FABRIK has better convergence but requires position-to-rotation conversion.

**Recommendation:** Implement both (Phases 3 and 4). Default to CCD for initial work, let users choose per-chain. Evaluate which produces better results for specific use cases (spines, tails) during the demo phase.

### 10.4 Interaction with Physics Ragdoll (Future)

A future ragdoll system would also need to modify joint rotations. The IK and ragdoll systems would need a priority/blending scheme:
- Animation (FK) -> IK post-process -> Ragdoll override (on death/hit)
- Or: Ragdoll produces a pose, IK refines it (powered ragdoll with IK targets)

This interaction is deferred but the PoseComponent approach (10.1) makes it straightforward -- any system can write to the pose in its DAG-scheduled order.

### 10.5 Root Motion and IK

If root motion is added in the future (animation clips that translate the character root), foot IK must account for the root motion offset when computing targets. The foot IK helper should use the post-root-motion FK ankle position as the raycast origin, not the pre-root-motion position.

### 10.6 Multi-Skeleton IK (Deferred)

IK across multiple skeletons (e.g. a character holding another character's hand) is not in scope. Each entity's IK is self-contained. Cross-entity constraints would require a constraint-based solver at a higher level.

---

### File Layout Summary

```
engine/animation/
    IkComponents.h             -- IkChainDef, IkChainsComponent, IkTarget, IkTargetsComponent,
                                  PoseComponent, IkSolverType enum
    IkSolvers.h                -- solveTwoBone(), solveCcd(), solveFabrik() free functions
    IkSolvers.cpp
    IkSystem.h                 -- IkSystem class
    IkSystem.cpp
    FootIkHelper.h             -- FootIkHelper, adjustPelvisHeight()
    FootIkHelper.cpp
    LookAtHelper.h             -- LookAtHelper

tests/animation/
    TestIkSolvers.cpp          -- unit tests for all solvers and blending

apps/ik_demo/
    main.mm                    -- IK demo application
```
