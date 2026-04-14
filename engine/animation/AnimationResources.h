#pragma once

#include <cstdint>
#include <vector>

#include "engine/animation/AnimationClip.h"
#include "engine/animation/Skeleton.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// AnimationResources -- shared storage for skeletons and animation clips.
//
// Analogous to RenderResources for meshes/materials. Multiple entities can
// reference the same skeleton/clip by ID.
// ---------------------------------------------------------------------------

class AnimationResources
{
public:
    [[nodiscard]] uint32_t addSkeleton(Skeleton skeleton)
    {
        uint32_t id = static_cast<uint32_t>(skeletons_.size());
        skeletons_.push_back(std::move(skeleton));
        return id;
    }

    [[nodiscard]] uint32_t addClip(AnimationClip clip)
    {
        uint32_t id = static_cast<uint32_t>(clips_.size());
        clips_.push_back(std::move(clip));
        return id;
    }

    [[nodiscard]] const Skeleton* getSkeleton(uint32_t id) const
    {
        if (id < skeletons_.size())
            return &skeletons_[id];
        return nullptr;
    }

    [[nodiscard]] const AnimationClip* getClip(uint32_t id) const
    {
        if (id < clips_.size())
            return &clips_[id];
        return nullptr;
    }

    [[nodiscard]] AnimationClip* getClipMut(uint32_t id)
    {
        if (id < clips_.size())
            return &clips_[id];
        return nullptr;
    }

    [[nodiscard]] uint32_t skeletonCount() const noexcept
    {
        return static_cast<uint32_t>(skeletons_.size());
    }

    [[nodiscard]] uint32_t clipCount() const noexcept
    {
        return static_cast<uint32_t>(clips_.size());
    }

private:
    std::vector<Skeleton> skeletons_;
    std::vector<AnimationClip> clips_;
};

}  // namespace engine::animation
