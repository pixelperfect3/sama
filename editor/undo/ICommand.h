#pragma once

namespace engine::editor
{

// ---------------------------------------------------------------------------
// ICommand -- interface for undoable editor commands.
// ---------------------------------------------------------------------------

class ICommand
{
public:
    virtual ~ICommand() = default;

    // Execute (or re-execute) the command.
    virtual void execute() = 0;

    // Reverse the effect of the command.
    virtual void undo() = 0;

    // Human-readable description for the undo/redo HUD.
    virtual const char* description() const = 0;
};

}  // namespace engine::editor
