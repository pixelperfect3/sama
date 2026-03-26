#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include "engine/input/IInputBackend.h"
#include "engine/input/Key.h"

struct GLFWwindow;

namespace engine::input
{

// Translate a GLFW key code (GLFW_KEY_*) to an engine Key.
// Returns nullopt for unmapped or unknown keys.
std::optional<Key> glfwKeyCodeToKey(int glfwKeyCode);

// GLFW-based input backend. Registers key, mouse-button, and cursor-position
// callbacks on the given window. Uses a mutex-protected write buffer so GLFW
// callbacks (which run on the main thread inside glfwPollEvents) are safe to
// call from a thread other than the consumer.
//
// Lifetime: the backend must not outlive the GLFWwindow.
class GlfwInputBackend final : public IInputBackend
{
public:
    explicit GlfwInputBackend(GLFWwindow* window);
    ~GlfwInputBackend() override;

    // IInputBackend
    void collectEvents(std::vector<RawEvent>& out) override;
    void mousePosition(double& x, double& y) const override;

    // Called by GLFW callbacks and directly in tests.
    void onKey(int glfwKey, int action);
    void onMouseButton(int button, int action);
    void onCursorPos(double x, double y);

private:
    GLFWwindow* window_;

    mutable std::mutex mutex_;
    std::vector<RawEvent> writeBuffer_;

    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double x, double y);
};

}  // namespace engine::input
