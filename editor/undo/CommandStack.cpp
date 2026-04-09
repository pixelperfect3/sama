#include "editor/undo/CommandStack.h"

#include "editor/undo/ICommand.h"

namespace engine::editor
{

CommandStack::CommandStack(size_t maxSize) : maxSize_(maxSize) {}

CommandStack::~CommandStack() = default;

void CommandStack::execute(std::unique_ptr<ICommand> cmd)
{
    cmd->execute();
    undoStack_.push_back(std::move(cmd));

    // Clear the redo stack — branching invalidates future history.
    redoStack_.clear();

    // Enforce max stack size.
    while (undoStack_.size() > maxSize_)
    {
        undoStack_.erase(undoStack_.begin());
    }
}

void CommandStack::undo()
{
    if (undoStack_.empty())
        return;

    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();

    cmd->undo();
    redoStack_.push_back(std::move(cmd));
}

void CommandStack::redo()
{
    if (redoStack_.empty())
        return;

    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();

    cmd->execute();
    undoStack_.push_back(std::move(cmd));
}

bool CommandStack::canUndo() const
{
    return !undoStack_.empty();
}

bool CommandStack::canRedo() const
{
    return !redoStack_.empty();
}

const char* CommandStack::undoDescription() const
{
    if (undoStack_.empty())
        return "";
    return undoStack_.back()->description();
}

const char* CommandStack::redoDescription() const
{
    if (redoStack_.empty())
        return "";
    return redoStack_.back()->description();
}

void CommandStack::setMaxSize(size_t maxSize)
{
    maxSize_ = maxSize;
    while (undoStack_.size() > maxSize_)
    {
        undoStack_.erase(undoStack_.begin());
    }
}

size_t CommandStack::maxSize() const
{
    return maxSize_;
}

}  // namespace engine::editor
