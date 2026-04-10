#include "editor/panels/ConsolePanel.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdio>

#include "editor/EditorLog.h"
#include "engine/memory/InlinedVector.h"

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

    // Collect the last kMaxVisibleRows entries into a stack-allocated ring
    // (no per-frame heap allocation). The lambda overwrites at writeIdx %
    // capacity so we naturally keep only the most recent kMaxVisibleRows.
    struct DisplayEntry
    {
        LogLevel level;
        const char* message;
    };

    DisplayEntry buf[kMaxVisibleRows]{};
    size_t writeIdx = 0;
    size_t total = 0;
    EditorLog::instance().forEach(
        [&](const EditorLog::Entry& entry)
        {
            buf[writeIdx % kMaxVisibleRows] = {entry.level, entry.message};
            ++writeIdx;
            ++total;
        });

    // Replay the ring in chronological order. If total < capacity, replay
    // [0, total). Otherwise the oldest entry is at writeIdx % capacity.
    const size_t shown = std::min<size_t>(total, kMaxVisibleRows);
    const size_t startInRing = (total <= kMaxVisibleRows) ? 0 : (writeIdx % kMaxVisibleRows);

    uint16_t row = headerRow + 1;
    for (size_t k = 0; k < shown; ++k)
    {
        const DisplayEntry& e = buf[(startInRing + k) % kMaxVisibleRows];

        // Color based on log level.
        uint8_t color = 0x07;  // grey (Info)
        const char* prefix = "[I]";
        switch (e.level)
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

        bgfx::dbgTextPrintf(kStartCol, row++, color, "%s %s", prefix, e.message);
    }
}

}  // namespace engine::editor
