#pragma once

#include <cstdint>

#include "editor/panels/IEditorPanel.h"
#include "engine/ecs/Entity.h"

namespace engine::ecs
{
class Registry;
}

namespace engine::animation
{
class AnimationResources;
}

namespace engine::editor
{

class EditorState;
class CocoaAnimationView;

// ---------------------------------------------------------------------------
// AnimationPanel -- platform-independent panel that tracks the editor's
// primary selection and pushes an AnimationViewState snapshot to the native
// CocoaAnimationView whenever the observed data changes.
//
// Pimpl-free: the cocoa view pointer is supplied at construction time.
// The panel uses a dirty-flag cache (lastEntity / lastClipId / lastTime /
// lastFlags) so it only touches the UI when something actually changed.
// ---------------------------------------------------------------------------

class AnimationPanel final : public IEditorPanel
{
public:
    AnimationPanel(ecs::Registry& registry, EditorState& editorState,
                   animation::AnimationResources& animResources, CocoaAnimationView* view);
    ~AnimationPanel() override = default;

    AnimationPanel(const AnimationPanel&) = delete;
    AnimationPanel& operator=(const AnimationPanel&) = delete;

    const char* panelName() const override
    {
        return "Animation";
    }

    void init() override;
    void shutdown() override;

    void update(float dt) override;
    void render() override {}

    // Force a state refresh on the next update() (e.g. after selection change).
    void markDirty()
    {
        forceRefresh_ = true;
    }

private:
    ecs::Registry& registry_;
    EditorState& state_;
    animation::AnimationResources& animResources_;
    CocoaAnimationView* view_ = nullptr;

    // Dirty-flag cache.
    ecs::EntityID lastEntity_ = ecs::INVALID_ENTITY;
    uint32_t lastClipId_ = 0xFFFFFFFFu;
    float lastPlaybackTime_ = -1.0f;
    uint8_t lastFlags_ = 0xFFu;
    bool forceRefresh_ = true;

    // State machine editing selection state.
    int selectedStateIndex_ = -1;
    int selectedTransitionIndex_ = -1;

public:
    void setSelectedState(int index)
    {
        selectedStateIndex_ = index;
        selectedTransitionIndex_ = -1;
        forceRefresh_ = true;
    }
    void setSelectedTransition(int stateIndex, int transIndex)
    {
        selectedStateIndex_ = stateIndex;
        selectedTransitionIndex_ = transIndex;
        forceRefresh_ = true;
    }
};

}  // namespace engine::editor
