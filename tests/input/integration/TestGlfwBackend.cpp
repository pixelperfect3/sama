// White-box integration tests for GlfwInputBackend.
// Creates a hidden GLFW window (no display required on macOS/Linux with a
// window manager; skipped gracefully on headless CI if glfwInit fails).
// Exercises callback routing by calling onKey/onMouseButton/onCursorPos
// directly — the same path GLFW uses internally.

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "engine/input/InputState.h"
#include "engine/input/InputSystem.h"
#include "engine/input/desktop/GlfwInputBackend.h"

using namespace engine::input;

// ---------------------------------------------------------------------------
// Key mapping tests (no window needed)
// ---------------------------------------------------------------------------

TEST_CASE("glfwKeyCodeToKey: letters map correctly", "[input][glfw]")
{
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_A) == Key::A);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_Z) == Key::Z);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_W) == Key::W);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_S) == Key::S);
}

TEST_CASE("glfwKeyCodeToKey: digits map correctly", "[input][glfw]")
{
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_0) == Key::Num0);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_9) == Key::Num9);
}

TEST_CASE("glfwKeyCodeToKey: function keys map correctly", "[input][glfw]")
{
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_F1) == Key::F1);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_F12) == Key::F12);
}

TEST_CASE("glfwKeyCodeToKey: navigation keys map correctly", "[input][glfw]")
{
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_SPACE) == Key::Space);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_ENTER) == Key::Enter);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_ESCAPE) == Key::Escape);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_LEFT) == Key::Left);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_RIGHT) == Key::Right);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_UP) == Key::Up);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_DOWN) == Key::Down);
}

TEST_CASE("glfwKeyCodeToKey: modifier keys map correctly", "[input][glfw]")
{
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_LEFT_SHIFT) == Key::LeftShift);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_LEFT_CONTROL) == Key::LeftCtrl);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_LEFT_ALT) == Key::LeftAlt);
}

TEST_CASE("glfwKeyCodeToKey: numpad keys map correctly", "[input][glfw]")
{
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_KP_0) == Key::Numpad0);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_KP_ENTER) == Key::NumpadEnter);
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_KP_ADD) == Key::NumpadAdd);
}

TEST_CASE("glfwKeyCodeToKey: unknown key returns nullopt", "[input][glfw]")
{
    REQUIRE(glfwKeyCodeToKey(GLFW_KEY_UNKNOWN) == std::nullopt);
    REQUIRE(glfwKeyCodeToKey(-999) == std::nullopt);
}

// ---------------------------------------------------------------------------
// Callback routing tests (requires GLFW window)
// ---------------------------------------------------------------------------

struct GlfwFixture
{
    GlfwFixture()
    {
        valid = (glfwInit() == GLFW_TRUE);
        if (!valid)
            return;

        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(1, 1, "test", nullptr, nullptr);
        if (!window)
        {
            glfwTerminate();
            valid = false;
        }
    }
    ~GlfwFixture()
    {
        if (window)
            glfwDestroyWindow(window);
        if (valid)
            glfwTerminate();
    }

    bool valid = false;
    GLFWwindow* window = nullptr;
};

TEST_CASE("GlfwInputBackend: key press/release delivered via onKey", "[input][glfw]")
{
    GlfwFixture fx;
    if (!fx.valid)
        SKIP("GLFW init failed (headless environment)");

    GlfwInputBackend backend(fx.window);
    InputSystem sys(backend);
    InputState state;

    // Simulate what GLFW would call via the key callback
    backend.onKey(GLFW_KEY_SPACE, GLFW_PRESS);
    sys.update(state);

    REQUIRE(state.isKeyPressed(Key::Space));
    REQUIRE(state.isKeyHeld(Key::Space));

    backend.onKey(GLFW_KEY_SPACE, GLFW_RELEASE);
    sys.update(state);

    REQUIRE(state.isKeyReleased(Key::Space));
    REQUIRE_FALSE(state.isKeyHeld(Key::Space));
}

TEST_CASE("GlfwInputBackend: key repeat is ignored", "[input][glfw]")
{
    GlfwFixture fx;
    if (!fx.valid)
        SKIP("GLFW init failed (headless environment)");

    GlfwInputBackend backend(fx.window);
    InputSystem sys(backend);
    InputState state;

    backend.onKey(GLFW_KEY_W, GLFW_PRESS);
    sys.update(state);  // frame 1: pressed

    backend.onKey(GLFW_KEY_W, GLFW_REPEAT);  // should be ignored
    sys.update(state);                       // frame 2: still held, NOT re-pressed

    REQUIRE_FALSE(state.isKeyPressed(Key::W));
    REQUIRE(state.isKeyHeld(Key::W));
}

TEST_CASE("GlfwInputBackend: mouse button delivered via onMouseButton", "[input][glfw]")
{
    GlfwFixture fx;
    if (!fx.valid)
        SKIP("GLFW init failed (headless environment)");

    GlfwInputBackend backend(fx.window);
    InputSystem sys(backend);
    InputState state;

    backend.onMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    sys.update(state);

    REQUIRE(state.isMouseButtonPressed(MouseButton::Left));

    backend.onMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    sys.update(state);

    REQUIRE(state.isMouseButtonReleased(MouseButton::Left));
}

TEST_CASE("GlfwInputBackend: cursor position delivered via onCursorPos", "[input][glfw]")
{
    GlfwFixture fx;
    if (!fx.valid)
        SKIP("GLFW init failed (headless environment)");

    GlfwInputBackend backend(fx.window);
    InputSystem sys(backend);
    InputState state;

    sys.update(state);  // first frame — baseline

    backend.onCursorPos(300.0, 150.0);
    sys.update(state);

    REQUIRE(state.mouseX() == 300.0);
    REQUIRE(state.mouseY() == 150.0);
}

TEST_CASE("GlfwInputBackend: unknown GLFW key is silently ignored", "[input][glfw]")
{
    GlfwFixture fx;
    if (!fx.valid)
        SKIP("GLFW init failed (headless environment)");

    GlfwInputBackend backend(fx.window);
    InputSystem sys(backend);
    InputState state;

    // Should not crash or produce any event
    backend.onKey(GLFW_KEY_UNKNOWN, GLFW_PRESS);
    sys.update(state);

    // Verify no key is held
    for (size_t i = 0; i < InputState::kKeyCount; ++i)
        REQUIRE(state.keyFlags_[i] == 0);
}
