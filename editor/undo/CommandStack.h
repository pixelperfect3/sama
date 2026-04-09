#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace engine::editor
{

class ICommand;

// ---------------------------------------------------------------------------
// CommandStack -- manages undo/redo stacks of ICommand instances.
//
// execute() runs a command and pushes it onto the undo stack (clearing the
// redo stack).  undo()/redo() move commands between the two stacks.
// ---------------------------------------------------------------------------

class CommandStack
{
public:
    explicit CommandStack(size_t maxSize = 100);
    ~CommandStack();

    // Execute a command: runs execute(), pushes to undo stack, clears redo.
    void execute(std::unique_ptr<ICommand> cmd);

    // Pop from undo stack, call undo(), push to redo stack.
    void undo();

    // Pop from redo stack, call execute(), push to undo stack.
    void redo();

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    // Description of the command that would be undone/redone.
    [[nodiscard]] const char* undoDescription() const;
    [[nodiscard]] const char* redoDescription() const;

    // Clear both undo and redo stacks.
    void clear()
    {
        undoStack_.clear();
        redoStack_.clear();
    }

    // Max stack size (configurable).
    void setMaxSize(size_t maxSize);
    [[nodiscard]] size_t maxSize() const;

private:
    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
    size_t maxSize_;
};

}  // namespace engine::editor
