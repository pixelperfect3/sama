#pragma once

#include <cstdint>

namespace engine::animation
{

// ---------------------------------------------------------------------------
// ECS components for skeletal animation.
//
// Follow the same conventions as engine/rendering/EcsComponents.h:
// largest-alignment-first, explicit padding, static_assert on sizeof.
// ---------------------------------------------------------------------------

struct SkeletonComponent
{
    uint32_t skeletonId;  // index into AnimationResources skeleton table
};
static_assert(sizeof(SkeletonComponent) == 4);

struct AnimatorComponent  // offset  size
{
    static constexpr uint8_t kFlagLooping = 0x01;
    static constexpr uint8_t kFlagPlaying = 0x02;
    static constexpr uint8_t kFlagBlending = 0x04;
    static constexpr uint8_t kFlagSampleOnce = 0x08;  // editor scrub: sample one frame while paused

    uint32_t clipId;      //  0       4  -- current clip index
    uint32_t nextClipId;  //  4       4  -- blend target clip (UINT32_MAX = none)
    float playbackTime;   //  8       4  -- current time in seconds
    float speed;          // 12       4  -- playback rate multiplier
    float blendFactor;    // 16       4  -- 0.0 = current, 1.0 = fully blended
    float blendDuration;  // 20       4  -- total crossfade duration
    float blendElapsed;   // 24       4  -- elapsed crossfade time
    uint8_t flags;        // 28       1  -- bit 0: looping, bit 1: playing, bit 2: blending
    uint8_t _pad[3];      // 29       3
};  // total: 32 bytes
static_assert(sizeof(AnimatorComponent) == 32);

struct SkinComponent
{
    uint32_t boneMatrixOffset;  // offset into the per-frame bone matrix buffer
    uint32_t boneCount;         // number of bones for this skin
};
static_assert(sizeof(SkinComponent) == 8);

}  // namespace engine::animation
