// ----------------------------------------------------------------------------
// TestBuildPhaseParser — unit tests for the editor's parser of
// `android/build_apk.sh` phase markers.  The parser is a pure function
// living in `editor/BuildPhaseParser.h` (header-only, no dependencies on the
// rest of EditorApp) so it links into engine_tests without pulling in the
// Cocoa editor surface.
//
// Cases pin the contract that drives EditorApp's status bar:
//   - Recognise `[N/7]` markers exactly as emitted by build_apk.sh.
//   - Tolerate leading whitespace and ANSI SGR colour codes (the script
//     historically piped through `tput setaf`-style colourisers).
//   - Trim trailing newlines and ANSI resets so the label is presentable.
//   - Reject lines that do not start with a `[N/M]` marker.
//   - Permit `[8/7]`-style overflows (parser is permissive; the *editor* UI
//     decides how to render an out-of-range step) but reject zero
//     denominators (would force every caller to guard division).
// ----------------------------------------------------------------------------

#include <catch2/catch_test_macros.hpp>

#include "editor/BuildPhaseParser.h"

using engine::editor::BuildPhase;
using engine::editor::parseBuildPhase;

TEST_CASE("BuildPhaseParser: typical [1/7] marker", "[editor][build_parser]")
{
    auto p = parseBuildPhase("[1/7] Compiling shaders");
    REQUIRE(p.has_value());
    CHECK(p->step == 1);
    CHECK(p->totalSteps == 7);
    CHECK(p->label == "Compiling shaders");
}

TEST_CASE("BuildPhaseParser: final [7/7] marker", "[editor][build_parser]")
{
    auto p = parseBuildPhase("[7/7] Signing APK");
    REQUIRE(p.has_value());
    CHECK(p->step == 7);
    CHECK(p->totalSteps == 7);
    CHECK(p->label == "Signing APK");
}

TEST_CASE("BuildPhaseParser: zero-step [0/7] marker", "[editor][build_parser]")
{
    auto p = parseBuildPhase("[0/7] Setting up");
    REQUIRE(p.has_value());
    CHECK(p->step == 0);
    CHECK(p->totalSteps == 7);
    CHECK(p->label == "Setting up");
}

TEST_CASE("BuildPhaseParser: non-marker line returns nullopt", "[editor][build_parser]")
{
    CHECK_FALSE(parseBuildPhase("Random text").has_value());
    CHECK_FALSE(parseBuildPhase("").has_value());
    CHECK_FALSE(parseBuildPhase("compiling shaders...").has_value());
    // Looks marker-ish but missing the slash.
    CHECK_FALSE(parseBuildPhase("[1] something").has_value());
    // Letters where digits should be.
    CHECK_FALSE(parseBuildPhase("[a/7] nope").has_value());
    CHECK_FALSE(parseBuildPhase("[1/b] nope").has_value());
    // Missing closing bracket.
    CHECK_FALSE(parseBuildPhase("[1/7 unterminated").has_value());
}

TEST_CASE("BuildPhaseParser: out-of-range step is preserved (permissive)", "[editor][build_parser]")
{
    // Shouldn't happen in practice — but if build_apk.sh ever miscounts and
    // emits "[8/7]", the status bar should surface it instead of silently
    // dropping the line.  The UI is welcome to clamp / colour it red.
    auto p = parseBuildPhase("[8/7] something");
    REQUIRE(p.has_value());
    CHECK(p->step == 8);
    CHECK(p->totalSteps == 7);
    CHECK(p->label == "something");
}

TEST_CASE("BuildPhaseParser: zero denominator is rejected", "[editor][build_parser]")
{
    // Would force every caller to guard division when computing percent
    // complete — easier to just refuse the malformed input.
    CHECK_FALSE(parseBuildPhase("[1/0] degenerate").has_value());
}

TEST_CASE("BuildPhaseParser: leading whitespace is stripped", "[editor][build_parser]")
{
    auto p = parseBuildPhase("    [3/7] Signing");
    REQUIRE(p.has_value());
    CHECK(p->step == 3);
    CHECK(p->totalSteps == 7);
    CHECK(p->label == "Signing");

    auto q = parseBuildPhase("\t[2/7] tab indented");
    REQUIRE(q.has_value());
    CHECK(q->step == 2);
    CHECK(q->label == "tab indented");
}

TEST_CASE("BuildPhaseParser: ANSI colour code prefix is stripped", "[editor][build_parser]")
{
    // \033[32m = green SGR, \033[0m = reset.  Both must be skipped without
    // leaking ESC bytes into the parsed label.
    auto p = parseBuildPhase("\033[32m[1/7] Compiling shaders\033[0m");
    REQUIRE(p.has_value());
    CHECK(p->step == 1);
    CHECK(p->totalSteps == 7);
    CHECK(p->label == "Compiling shaders");
}

TEST_CASE("BuildPhaseParser: trailing newline is trimmed", "[editor][build_parser]")
{
    auto p = parseBuildPhase("[5/7] Linking native lib\n");
    REQUIRE(p.has_value());
    CHECK(p->step == 5);
    CHECK(p->label == "Linking native lib");

    auto crlf = parseBuildPhase("[5/7] Linking native lib\r\n");
    REQUIRE(crlf.has_value());
    CHECK(crlf->label == "Linking native lib");
}
