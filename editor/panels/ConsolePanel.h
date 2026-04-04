#pragma once

#include <cstdint>

#include "editor/panels/IEditorPanel.h"

namespace engine::editor
{

// ---------------------------------------------------------------------------
// ConsolePanel -- displays log messages from EditorLog in bgfx debug text.
//
// Renders at the bottom of the screen.  Toggle with '~' (tilde/backtick).
// Messages are color-coded by level: Info=grey, Warning=yellow, Error=red.
// ---------------------------------------------------------------------------

class ConsolePanel final : public IEditorPanel
{
public:
    ConsolePanel();
    ~ConsolePanel() override = default;

    const char* panelName() const override
    {
        return "Console";
    }

    void init() override;
    void shutdown() override;
    void update(float dt) override;
    void render() override;

private:
    // Layout constants (in debug-text character cells).
    static constexpr uint16_t kMaxVisibleRows = 12;
};

}  // namespace engine::editor
