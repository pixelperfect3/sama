#pragma once

#include <memory_resource>
#include <vector>

#include "engine/animation/AnimationResources.h"
#include "engine/ecs/Registry.h"
#include "engine/math/Types.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// AnimationSystem -- per-frame animation update.
//
// Reads:  SkeletonComponent
// Writes: AnimatorComponent, SkinComponent
//
// Must execute before TransformSystem and DrawCallBuildSystem.
//
// The bone matrix buffer is allocated from the provided arena (FrameArena)
// and stays valid until the arena is reset at frame end.
// ---------------------------------------------------------------------------

class AnimationSystem
{
public:
    // Update all animated entities. Returns a pointer to the beginning of the
    // bone matrix buffer (valid until arena reset). The buffer is contiguous:
    // each entity's bone matrices are laid out at SkinComponent::boneMatrixOffset.
    void update(ecs::Registry& reg, float dt, AnimationResources& animRes,
                std::pmr::memory_resource* arena);

    // Phase 1: sample FK poses and store them in PoseComponent.
    // Call this, then run IkSystem::update(), then call computeBoneMatrices().
    void updatePoses(ecs::Registry& reg, float dt, AnimationResources& animRes,
                     std::pmr::memory_resource* arena);

    // Phase 2: compute bone matrices from (potentially IK-modified) poses.
    void computeBoneMatrices(ecs::Registry& reg, AnimationResources& animRes,
                             std::pmr::memory_resource* arena);

    // Access the bone matrix buffer written by the last update() call.
    // Valid until the arena is reset at frame end.
    [[nodiscard]] const math::Mat4* boneBuffer() const noexcept
    {
        return boneBuffer_;
    }

    [[nodiscard]] uint32_t boneBufferSize() const noexcept
    {
        return boneBufferSize_;
    }

private:
    const math::Mat4* boneBuffer_ = nullptr;
    uint32_t boneBufferSize_ = 0;
};

}  // namespace engine::animation
