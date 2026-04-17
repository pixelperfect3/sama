#pragma once

#include <cstdint>

#include "engine/input/Key.h"

namespace engine::platform
{

// Map Android AKEYCODE_* to engine::input::Key.
// Returns Key::COUNT for unmapped keys.
engine::input::Key mapAndroidKeyCode(int32_t akeycode);

}  // namespace engine::platform
