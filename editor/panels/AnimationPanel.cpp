#include "editor/panels/AnimationPanel.h"

#include "editor/EditorState.h"
#include "editor/platform/cocoa/CocoaAnimationView.h"
#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/ecs/Registry.h"

namespace engine::editor
{

AnimationPanel::AnimationPanel(ecs::Registry& registry, EditorState& editorState,
                               animation::AnimationResources& animResources,
                               CocoaAnimationView* view)
    : registry_(registry), state_(editorState), animResources_(animResources), view_(view)
{
}

void AnimationPanel::init()
{
    forceRefresh_ = true;
}

void AnimationPanel::shutdown()
{
    view_ = nullptr;
}

void AnimationPanel::update(float /*dt*/)
{
    if (!view_)
        return;

    using namespace engine::animation;

    ecs::EntityID entity = state_.primarySelection();

    // Resolve animator / skeleton for the current selection.
    const AnimatorComponent* anim = nullptr;
    const SkeletonComponent* skel = nullptr;
    if (entity != ecs::INVALID_ENTITY)
    {
        anim = registry_.get<AnimatorComponent>(entity);
        skel = registry_.get<SkeletonComponent>(entity);
    }

    const bool hasAnimation = (anim != nullptr && skel != nullptr);

    // Short-circuit: if nothing relevant changed since the last push, skip.
    const uint32_t clipId = anim ? anim->clipId : 0xFFFFFFFFu;
    const float playbackTime = anim ? anim->playbackTime : 0.0f;
    const uint8_t flags = anim ? anim->flags : 0;

    if (!forceRefresh_ && entity == lastEntity_ && clipId == lastClipId_ &&
        playbackTime == lastPlaybackTime_ && flags == lastFlags_)
    {
        return;
    }

    lastEntity_ = entity;
    lastClipId_ = clipId;
    lastPlaybackTime_ = playbackTime;
    lastFlags_ = flags;
    forceRefresh_ = false;

    AnimationViewState s;
    s.hasAnimation = hasAnimation;

    if (hasAnimation)
    {
        // Enumerate every clip in AnimationResources (phase 1: no filtering
        // by skeleton). The dropdown shows all known clips so the user can
        // re-assign quickly while authoring.
        const uint32_t clipCount = animResources_.clipCount();
        s.clipNames.reserve(clipCount);
        for (uint32_t i = 0; i < clipCount; ++i)
        {
            const AnimationClip* c = animResources_.getClip(i);
            if (c && !c->name.empty())
                s.clipNames.push_back(c->name);
            else
                s.clipNames.push_back("clip " + std::to_string(i));
        }

        s.currentClipIndex = (clipId < clipCount) ? static_cast<int>(clipId) : -1;

        const AnimationClip* currentClip = animResources_.getClip(clipId);
        s.duration = currentClip ? currentClip->duration : 0.0f;
        s.currentTime = playbackTime;
        s.speed = anim->speed;
        s.playing = (flags & AnimatorComponent::kFlagPlaying) != 0;
        s.looping = (flags & AnimatorComponent::kFlagLooping) != 0;
        s.assetLabel = "entity";  // glTF asset label not tracked yet in Phase 1
    }

    view_->setState(s);
}

}  // namespace engine::editor
