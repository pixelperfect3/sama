#pragma once

#include <ankerl/unordered_dense.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::core
{

// ---------------------------------------------------------------------------
// TransparentStringHash — heterogeneous hash for std::string-keyed maps.
//
// Pair with std::equal_to<> as KeyEqual to enable .find()/.contains()/.count()
// lookups against std::string_view (and char*) without constructing a temporary
// std::string. Both std::string and std::string_view hash to the same value
// via ankerl's wyhash, so equal keys hash equally.
//
// Usage:
//   ankerl::unordered_dense::map<std::string, V,
//                                engine::core::TransparentStringHash,
//                                std::equal_to<>> map;
//   map.find(std::string_view{"abc"});  // no temporary allocation
//
// Insertions still need a real std::string key — the map owns its keys.
// ---------------------------------------------------------------------------

struct TransparentStringHash
{
    using is_transparent = void;
    using is_avalanching = void;  // ankerl: skip extra mixing; wyhash is already strong.

    [[nodiscard]] uint64_t operator()(std::string_view sv) const noexcept
    {
        return ankerl::unordered_dense::hash<std::string_view>{}(sv);
    }

    [[nodiscard]] uint64_t operator()(const std::string& s) const noexcept
    {
        return ankerl::unordered_dense::hash<std::string_view>{}(s);
    }

    [[nodiscard]] uint64_t operator()(const char* s) const noexcept
    {
        return ankerl::unordered_dense::hash<std::string_view>{}(std::string_view{s});
    }
};

}  // namespace engine::core
