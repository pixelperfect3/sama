#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace engine::editor
{

// ---------------------------------------------------------------------------
// LogLevel -- severity levels for console messages.
// ---------------------------------------------------------------------------

enum class LogLevel : uint8_t
{
    Info,
    Warning,
    Error,
};

// ---------------------------------------------------------------------------
// EditorLog -- thread-safe ring buffer log for the editor console.
//
// Singleton-like: one global instance accessed via EditorLog::instance().
// All panels and systems can write to it via EditorLog::instance().log(...).
// ---------------------------------------------------------------------------

class EditorLog
{
public:
    struct Entry
    {
        LogLevel level = LogLevel::Info;
        char message[256] = {};
    };

    static constexpr size_t kMaxEntries = 100;

    // Global instance.
    static EditorLog& instance();

    // Add a log entry.  Thread-safe.
    void log(LogLevel level, const char* message);

    // Convenience methods.
    void info(const char* message);
    void warning(const char* message);
    void error(const char* message);

    // Read access.  Lock is held for the duration of the callback.
    // Callback signature: void(const Entry& entry)
    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = (count_ < kMaxEntries) ? count_ : kMaxEntries;
        // Read from oldest to newest.
        for (size_t i = 0; i < count; ++i)
        {
            size_t idx;
            if (count_ <= kMaxEntries)
            {
                idx = i;
            }
            else
            {
                idx = (head_ + i) % kMaxEntries;
            }
            fn(entries_[idx]);
        }
    }

    [[nodiscard]] size_t entryCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return (count_ < kMaxEntries) ? count_ : kMaxEntries;
    }

private:
    EditorLog() = default;

    mutable std::mutex mutex_;
    std::array<Entry, kMaxEntries> entries_;
    size_t head_ = 0;   // Next write position.
    size_t count_ = 0;  // Total entries written (may exceed kMaxEntries).
};

}  // namespace engine::editor
