#pragma once

#include <cstdint>

#include "engine/math/Types.h"

namespace engine::ui
{

enum class UiEventType : uint8_t
{
    MouseDown,
    MouseUp,
    MouseMove,
    MouseEnter,
    MouseExit
};

struct UiEvent
{
    UiEventType type;
    math::Vec2 position{0.f, 0.f};  // in logical screen pixels
    int button = 0;                 // 0 = left, 1 = right
};

}  // namespace engine::ui
