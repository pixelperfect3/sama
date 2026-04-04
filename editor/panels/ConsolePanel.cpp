#include "editor/panels/ConsolePanel.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "editor/EditorLog.h"

namespace engine::editor
{

ConsolePanel::ConsolePanel() {}

void ConsolePanel::init() {}

void ConsolePanel::shutdown() {}

void ConsolePanel::update(float /*dt*/) {}

void ConsolePanel::render()
{
    if (!isVisible())
        return;

    // Render at the bottom of the screen.
    // bgfx debug text is 160 columns x 60 rows at 1920x960 (8x16 per char).
    // We render from row (60 - kMaxVisibleRows - 2) downward.
    constexpr uint16_t kBottomRow = 58;
    constexpr uint16_t kStartCol = 1;
    uint16_t headerRow = kBottomRow - kMaxVisibleRows - 1;

    bgfx::dbgTextPrintf(kStartCol, headerRow, 0x0f, "--- Console (~=toggle) ---");

    // Collect entries.
    struct DisplayEntry
    {
        LogLevel level;
        const char* message;
    };

    std::vector<DisplayEntry> entries;
    EditorLog::instance().forEach([&](const EditorLog::Entry& entry)
                                  { entries.push_back({entry.level, entry.message}); });

    // Show the last kMaxVisibleRows entries.
    size_t startIdx = 0;
    if (entries.size() > kMaxVisibleRows)
    {
        startIdx = entries.size() - kMaxVisibleRows;
    }

    uint16_t row = headerRow + 1;
    for (size_t i = startIdx; i < entries.size(); ++i)
    {
        // Color based on log level.
        uint8_t color = 0x07;  // grey (Info)
        const char* prefix = "[I]";
        switch (entries[i].level)
        {
            case LogLevel::Warning:
                color = 0x0e;  // yellow
                prefix = "[W]";
                break;
            case LogLevel::Error:
                color = 0x0c;  // red
                prefix = "[E]";
                break;
            default:
                break;
        }

        bgfx::dbgTextPrintf(kStartCol, row++, color, "%s %s", prefix, entries[i].message);
    }
}

}  // namespace engine::editor
