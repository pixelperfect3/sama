#pragma once

// ----------------------------------------------------------------------------
// BuildPhaseParser — extracts `[N/M] label` phase markers from a single line
// of `android/build_apk.sh` stdout so the editor can drive its build status
// bar.  The script emits markers like:
//
//     [0/7] Setting up...
//     [1/7] Compiling shaders to SPIRV for Android...
//     ...
//     [7/7] Signing APK...
//
// We accept a slightly relaxed grammar (any digit / digit denominator, any
// label tail) so bumps to the script's phase count don't silently break the
// status bar.  The only inputs that match are lines that *start* with `[N/M]`
// (after stripping leading whitespace and optional ANSI SGR colour codes).
// Unknown / non-marker lines return std::nullopt.
//
// Header-only: pure string code, called from EditorApp.cpp on every stdout
// line of the build process.  Lifted into its own header so it can be unit
// tested without dragging the rest of EditorApp into the test binary.
// ----------------------------------------------------------------------------

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace engine::editor
{

struct BuildPhase
{
    int step = -1;       // Numerator before the slash, e.g. 1 for "[1/7]".
    int totalSteps = 0;  // Denominator after the slash, e.g. 7 for "[1/7]".
    std::string label;   // Trimmed text after the closing bracket and space.
};

namespace detail
{

// Skip an ANSI CSI sequence starting at `i` (i.e. an `\033[...m` SGR code).
// Returns the index of the next character past the trailing letter.  If the
// sequence is malformed (no ESC, no terminator), returns `i` unchanged.
inline std::size_t skipAnsiCsi(std::string_view s, std::size_t i)
{
    if (i >= s.size() || s[i] != '\033')
        return i;
    if (i + 1 >= s.size() || s[i + 1] != '[')
        return i;
    // Walk forward until the SGR terminator (a letter in @-~).  This is
    // permissive on purpose — we just want to step past colour codes.
    std::size_t j = i + 2;
    while (j < s.size())
    {
        const char c = s[j];
        if ((c >= '@' && c <= '~'))
            return j + 1;
        ++j;
    }
    return i;  // Malformed: leave it alone so the caller can bail.
}

}  // namespace detail

// Parse one line of build_apk.sh output.  Returns an engaged optional iff
// the (possibly indented / colourised) line begins with a `[N/M]` marker.
// `M` is allowed to be any positive integer so we don't have to bump this
// parser when build_apk.sh grows an extra phase.
inline std::optional<BuildPhase> parseBuildPhase(std::string_view line)
{
    // 1. Skip leading whitespace.
    std::size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
        ++i;

    // 2. Skip an optional ANSI CSI prefix (e.g. "\033[32m").
    i = detail::skipAnsiCsi(line, i);

    // 3. Must start with '['.
    if (i >= line.size() || line[i] != '[')
        return std::nullopt;
    ++i;

    // 4. Numerator — at least one digit.
    const std::size_t numStart = i;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
        ++i;
    if (i == numStart)
        return std::nullopt;
    const std::string numStr(line.substr(numStart, i - numStart));

    // 5. Slash separator.
    if (i >= line.size() || line[i] != '/')
        return std::nullopt;
    ++i;

    // 6. Denominator — at least one digit.
    const std::size_t denStart = i;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
        ++i;
    if (i == denStart)
        return std::nullopt;
    const std::string denStr(line.substr(denStart, i - denStart));

    // 7. Closing bracket.
    if (i >= line.size() || line[i] != ']')
        return std::nullopt;
    ++i;

    // 8. Reject impossible totals (zero denominator) so callers don't have to
    //    guard division.  We deliberately do NOT clamp step <= totalSteps —
    //    a future script bug producing "[8/7]" should still surface in the
    //    status bar so the user notices the regression.
    BuildPhase out;
    out.step = std::stoi(numStr);
    out.totalSteps = std::stoi(denStr);
    if (out.totalSteps <= 0)
        return std::nullopt;

    // 9. Skip any whitespace + a trailing ANSI reset before the label.
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
        ++i;
    i = detail::skipAnsiCsi(line, i);

    // 10. Capture the label until end of line / trailing newline / trailing
    //     ANSI reset.  Strip trailing whitespace so tests see the bare text.
    std::size_t end = line.size();
    while (end > i && (line[end - 1] == '\n' || line[end - 1] == '\r' || line[end - 1] == ' ' ||
                       line[end - 1] == '\t'))
    {
        --end;
    }
    // Drop a trailing ANSI reset if present.
    if (end > i)
    {
        // Find the last ESC; if it begins an SGR that consumes the rest of
        // the trimmed range, exclude it from the label.
        for (std::size_t k = i; k < end; ++k)
        {
            if (line[k] == '\033' && k + 1 < end && line[k + 1] == '[')
            {
                end = k;
                break;
            }
        }
    }
    out.label = std::string(line.substr(i, end - i));
    return out;
}

}  // namespace engine::editor
