#pragma once

#include <cstdint>
#include <functional>

#include "engine/ecs/Entity.h"
#include "engine/memory/InlinedVector.h"

namespace engine::editor
{

// ---------------------------------------------------------------------------
// EditorState -- shared state for all editor panels and systems.
//
// Owns the current entity selection and notifies listeners on change.
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
};

}  // namespace engine::editor
