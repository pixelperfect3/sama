#define GLFW_INCLUDE_NONE
#include "engine/input/desktop/GlfwInputBackend.h"

#include <GLFW/glfw3.h>

#include <utility>

namespace engine::input
{

// ---------------------------------------------------------------------------
// Key mapping
// ---------------------------------------------------------------------------

std::optional<Key> glfwKeyCodeToKey(int k)
{
    // clang-format off
    switch (k)
    {
        case GLFW_KEY_A:              return Key::A;
        case GLFW_KEY_B:              return Key::B;
        case GLFW_KEY_C:              return Key::C;
        case GLFW_KEY_D:              return Key::D;
        case GLFW_KEY_E:              return Key::E;
        case GLFW_KEY_F:              return Key::F;
        case GLFW_KEY_G:              return Key::G;
        case GLFW_KEY_H:              return Key::H;
        case GLFW_KEY_I:              return Key::I;
        case GLFW_KEY_J:              return Key::J;
        case GLFW_KEY_K:              return Key::K;
        case GLFW_KEY_L:              return Key::L;
        case GLFW_KEY_M:              return Key::M;
        case GLFW_KEY_N:              return Key::N;
        case GLFW_KEY_O:              return Key::O;
        case GLFW_KEY_P:              return Key::P;
        case GLFW_KEY_Q:              return Key::Q;
        case GLFW_KEY_R:              return Key::R;
        case GLFW_KEY_S:              return Key::S;
        case GLFW_KEY_T:              return Key::T;
        case GLFW_KEY_U:              return Key::U;
        case GLFW_KEY_V:              return Key::V;
        case GLFW_KEY_W:              return Key::W;
        case GLFW_KEY_X:              return Key::X;
        case GLFW_KEY_Y:              return Key::Y;
        case GLFW_KEY_Z:              return Key::Z;
        case GLFW_KEY_0:              return Key::Num0;
        case GLFW_KEY_1:              return Key::Num1;
        case GLFW_KEY_2:              return Key::Num2;
        case GLFW_KEY_3:              return Key::Num3;
        case GLFW_KEY_4:              return Key::Num4;
        case GLFW_KEY_5:              return Key::Num5;
        case GLFW_KEY_6:              return Key::Num6;
        case GLFW_KEY_7:              return Key::Num7;
        case GLFW_KEY_8:              return Key::Num8;
        case GLFW_KEY_9:              return Key::Num9;
        case GLFW_KEY_F1:             return Key::F1;
        case GLFW_KEY_F2:             return Key::F2;
        case GLFW_KEY_F3:             return Key::F3;
        case GLFW_KEY_F4:             return Key::F4;
        case GLFW_KEY_F5:             return Key::F5;
        case GLFW_KEY_F6:             return Key::F6;
        case GLFW_KEY_F7:             return Key::F7;
        case GLFW_KEY_F8:             return Key::F8;
        case GLFW_KEY_F9:             return Key::F9;
        case GLFW_KEY_F10:            return Key::F10;
        case GLFW_KEY_F11:            return Key::F11;
        case GLFW_KEY_F12:            return Key::F12;
        case GLFW_KEY_SPACE:          return Key::Space;
        case GLFW_KEY_ENTER:          return Key::Enter;
        case GLFW_KEY_ESCAPE:         return Key::Escape;
        case GLFW_KEY_TAB:            return Key::Tab;
        case GLFW_KEY_BACKSPACE:      return Key::Backspace;
        case GLFW_KEY_DELETE:         return Key::Delete;
        case GLFW_KEY_INSERT:         return Key::Insert;
        case GLFW_KEY_LEFT:           return Key::Left;
        case GLFW_KEY_RIGHT:          return Key::Right;
        case GLFW_KEY_UP:             return Key::Up;
        case GLFW_KEY_DOWN:           return Key::Down;
        case GLFW_KEY_HOME:           return Key::Home;
        case GLFW_KEY_END:            return Key::End;
        case GLFW_KEY_PAGE_UP:        return Key::PageUp;
        case GLFW_KEY_PAGE_DOWN:      return Key::PageDown;
        case GLFW_KEY_LEFT_SHIFT:     return Key::LeftShift;
        case GLFW_KEY_RIGHT_SHIFT:    return Key::RightShift;
        case GLFW_KEY_LEFT_CONTROL:   return Key::LeftCtrl;
        case GLFW_KEY_RIGHT_CONTROL:  return Key::RightCtrl;
        case GLFW_KEY_LEFT_ALT:       return Key::LeftAlt;
        case GLFW_KEY_RIGHT_ALT:      return Key::RightAlt;
        case GLFW_KEY_LEFT_SUPER:     return Key::LeftSuper;
        case GLFW_KEY_RIGHT_SUPER:    return Key::RightSuper;
        case GLFW_KEY_APOSTROPHE:     return Key::Apostrophe;
        case GLFW_KEY_COMMA:          return Key::Comma;
        case GLFW_KEY_MINUS:          return Key::Minus;
        case GLFW_KEY_PERIOD:         return Key::Period;
        case GLFW_KEY_SLASH:          return Key::Slash;
        case GLFW_KEY_SEMICOLON:      return Key::Semicolon;
        case GLFW_KEY_EQUAL:          return Key::Equal;
        case GLFW_KEY_LEFT_BRACKET:   return Key::LeftBracket;
        case GLFW_KEY_BACKSLASH:      return Key::Backslash;
        case GLFW_KEY_RIGHT_BRACKET:  return Key::RightBracket;
        case GLFW_KEY_GRAVE_ACCENT:   return Key::GraveAccent;
        case GLFW_KEY_KP_0:           return Key::Numpad0;
        case GLFW_KEY_KP_1:           return Key::Numpad1;
        case GLFW_KEY_KP_2:           return Key::Numpad2;
        case GLFW_KEY_KP_3:           return Key::Numpad3;
        case GLFW_KEY_KP_4:           return Key::Numpad4;
        case GLFW_KEY_KP_5:           return Key::Numpad5;
        case GLFW_KEY_KP_6:           return Key::Numpad6;
        case GLFW_KEY_KP_7:           return Key::Numpad7;
        case GLFW_KEY_KP_8:           return Key::Numpad8;
        case GLFW_KEY_KP_9:           return Key::Numpad9;
        case GLFW_KEY_KP_DECIMAL:     return Key::NumpadDecimal;
        case GLFW_KEY_KP_DIVIDE:      return Key::NumpadDivide;
        case GLFW_KEY_KP_MULTIPLY:    return Key::NumpadMultiply;
        case GLFW_KEY_KP_SUBTRACT:    return Key::NumpadSubtract;
        case GLFW_KEY_KP_ADD:         return Key::NumpadAdd;
        case GLFW_KEY_KP_ENTER:       return Key::NumpadEnter;
        default:                      return std::nullopt;
    }
    // clang-format on
}

// ---------------------------------------------------------------------------
// GlfwInputBackend
// ---------------------------------------------------------------------------

GlfwInputBackend::GlfwInputBackend(GLFWwindow* window) : window_(window)
{
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetCursorPosCallback(window_, cursorPosCallback);
}

GlfwInputBackend::~GlfwInputBackend()
{
    // Remove callbacks so they don't fire after this object is gone.
    if (window_)
    {
        glfwSetKeyCallback(window_, nullptr);
        glfwSetMouseButtonCallback(window_, nullptr);
        glfwSetCursorPosCallback(window_, nullptr);
        glfwSetWindowUserPointer(window_, nullptr);
    }
}

void GlfwInputBackend::collectEvents(std::vector<RawEvent>& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    out.insert(out.end(), writeBuffer_.begin(), writeBuffer_.end());
    writeBuffer_.clear();
}

void GlfwInputBackend::mousePosition(double& x, double& y) const
{
    glfwGetCursorPos(window_, &x, &y);
}

void GlfwInputBackend::onKey(int glfwKey, int action)
{
    if (action == GLFW_REPEAT)
        return;  // key-repeat events have no meaning in our three-state model

    auto key = glfwKeyCodeToKey(glfwKey);
    if (!key)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (action == GLFW_PRESS)
        writeBuffer_.push_back(RawEvent::keyDown(*key));
    else
        writeBuffer_.push_back(RawEvent::keyUp(*key));
}

void GlfwInputBackend::onMouseButton(int button, int action)
{
    MouseButton b;
    switch (button)
    {
        case GLFW_MOUSE_BUTTON_LEFT:
            b = MouseButton::Left;
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            b = MouseButton::Right;
            break;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            b = MouseButton::Middle;
            break;
        default:
            return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (action == GLFW_PRESS)
        writeBuffer_.push_back(RawEvent::mouseButtonDown(b));
    else
        writeBuffer_.push_back(RawEvent::mouseButtonUp(b));
}

void GlfwInputBackend::onCursorPos(double x, double y)
{
    std::lock_guard<std::mutex> lock(mutex_);
    writeBuffer_.push_back(RawEvent::mouseMove(x, y));
}

// ---------------------------------------------------------------------------
// Static GLFW callbacks
// ---------------------------------------------------------------------------

void GlfwInputBackend::keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action,
                                   int /*mods*/)
{
    auto* self = static_cast<GlfwInputBackend*>(glfwGetWindowUserPointer(w));
    if (self)
        self->onKey(key, action);
}

void GlfwInputBackend::mouseButtonCallback(GLFWwindow* w, int button, int action, int /*mods*/)
{
    auto* self = static_cast<GlfwInputBackend*>(glfwGetWindowUserPointer(w));
    if (self)
        self->onMouseButton(button, action);
}

void GlfwInputBackend::cursorPosCallback(GLFWwindow* w, double x, double y)
{
    auto* self = static_cast<GlfwInputBackend*>(glfwGetWindowUserPointer(w));
    if (self)
        self->onCursorPos(x, y);
}

}  // namespace engine::input
