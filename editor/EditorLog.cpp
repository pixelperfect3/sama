#include "editor/EditorLog.h"

#include <cstdio>
#include <cstring>

namespace engine::editor
{

EditorLog& EditorLog::instance()
{
    static EditorLog s_instance;
    return s_instance;
}

void EditorLog::log(LogLevel level, const char* message)
{
    std::lock_guard<std::mutex> lock(mutex_);

    Entry& entry = entries_[head_];
    entry.level = level;
    strncpy(entry.message, message, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';

    head_ = (head_ + 1) % kMaxEntries;
    ++count_;
}

void EditorLog::info(const char* message)
{
    log(LogLevel::Info, message);
}

void EditorLog::warning(const char* message)
{
    log(LogLevel::Warning, message);
}

void EditorLog::error(const char* message)
{
    log(LogLevel::Error, message);
}

}  // namespace engine::editor
