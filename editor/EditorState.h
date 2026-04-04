#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "engine/ecs/Entity.h"
#include "engine/memory/InlinedVector.h"
#include "engine/rendering/EcsComponents.h"

namespace engine::editor
{

// ---------------------------------------------------------------------------
// EditorPlayState -- play/pause/stop state machine.
// ---------------------------------------------------------------------------

enum class EditorPlayState : uint8_t
{
    Editing,  // Normal editor mode.
    Playing,  // Game systems running.
    Paused,   // Game systems frozen, rendering continues.
};

// ---------------------------------------------------------------------------
// EditorState -- shared state for all editor panels and systems.
//
// Owns the current entity selection, play state, and notifies listeners.
// ---------------------------------------------------------------------------

class EditorState
{
public:
    using SelectionCallback = std::function<void()>;

    // --- Selection -----------------------------------------------------------

    void select(ecs::EntityID entity)
    {
        selection_.clear();
        if (entity != ecs::INVALID_ENTITY)
        {
            selection_.push_back(entity);
        }
        notifySelectionChanged();
    }

    void addToSelection(ecs::EntityID entity)
    {
        if (entity == ecs::INVALID_ENTITY)
            return;
        for (size_t i = 0; i < selection_.size(); ++i)
        {
            if (selection_[i] == entity)
                return;  // already selected
        }
        selection_.push_back(entity);
        notifySelectionChanged();
    }

    void removeFromSelection(ecs::EntityID entity)
    {
        for (auto it = selection_.begin(); it != selection_.end(); ++it)
        {
            if (*it == entity)
            {
                selection_.erase(it);
                notifySelectionChanged();
                return;
            }
        }
    }

    void clearSelection()
    {
        if (!selection_.empty())
        {
            selection_.clear();
            notifySelectionChanged();
        }
    }

    [[nodiscard]] bool isSelected(ecs::EntityID entity) const
    {
        for (size_t i = 0; i < selection_.size(); ++i)
        {
            if (selection_[i] == entity)
                return true;
        }
        return false;
    }

    [[nodiscard]] ecs::EntityID primarySelection() const
    {
        return selection_.empty() ? ecs::INVALID_ENTITY : selection_[0];
    }

    [[nodiscard]] const memory::InlinedVector<ecs::EntityID, 16>& selection() const
    {
        return selection_;
    }

    // --- Play state ---------------------------------------------------------

    [[nodiscard]] EditorPlayState playState() const
    {
        return playState_;
    }

    void play()
    {
        if (playState_ == EditorPlayState::Editing)
        {
            playState_ = EditorPlayState::Playing;
        }
        else if (playState_ == EditorPlayState::Paused)
        {
            playState_ = EditorPlayState::Playing;
        }
    }

    void pause()
    {
        if (playState_ == EditorPlayState::Playing)
        {
            playState_ = EditorPlayState::Paused;
        }
    }

    void stop()
    {
        playState_ = EditorPlayState::Editing;
    }

    // --- Transform snapshot (for play-mode state restore) --------------------

    struct TransformSnapshot
    {
        ecs::EntityID entity;
        rendering::TransformComponent transform;
    };

    void saveSnapshot(const std::vector<TransformSnapshot>& snapshot)
    {
        transformSnapshot_ = snapshot;
    }

    [[nodiscard]] const std::vector<TransformSnapshot>& transformSnapshot() const
    {
        return transformSnapshot_;
    }

    // --- Callbacks -----------------------------------------------------------

    void setSelectionChangedCallback(SelectionCallback cb)
    {
        onSelectionChanged_ = std::move(cb);
    }

private:
    void notifySelectionChanged()
    {
        if (onSelectionChanged_)
        {
            onSelectionChanged_();
        }
    }

    memory::InlinedVector<ecs::EntityID, 16> selection_;
    SelectionCallback onSelectionChanged_;

    EditorPlayState playState_ = EditorPlayState::Editing;
    std::vector<TransformSnapshot> transformSnapshot_;
};

}  // namespace engine::editor
