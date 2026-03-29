#pragma once

#include <string>

namespace engine::scene
{

// Optional name for display/serialization.  Not required for engine operation.
struct NameComponent
{
    std::string name;
};

}  // namespace engine::scene
