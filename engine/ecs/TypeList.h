#pragma once

#include <cstddef>
#include <type_traits>

namespace engine::ecs
{

// ---------------------------------------------------------------------------
// TypeList<Ts...>
// A compile-time list of types. Used to declare component read/write sets
// on systems for the DAG scheduler.
// ---------------------------------------------------------------------------

template <typename... Ts>
struct TypeList
{
    static constexpr std::size_t size = sizeof...(Ts);
};

// ---------------------------------------------------------------------------
// Contains<T, List> — true if T appears in List
// ---------------------------------------------------------------------------

template <typename T, typename List>
struct Contains : std::false_type
{
};

template <typename T, typename... Ts>
struct Contains<T, TypeList<T, Ts...>> : std::true_type
{
};

template <typename T, typename U, typename... Ts>
struct Contains<T, TypeList<U, Ts...>> : Contains<T, TypeList<Ts...>>
{
};

template <typename T, typename List>
inline constexpr bool kContains = Contains<T, List>::value;

// ---------------------------------------------------------------------------
// Intersects<A, B> — true if TypeList A and TypeList B share at least one type
// ---------------------------------------------------------------------------

template <typename A, typename B>
struct Intersects : std::false_type
{
};

template <typename T, typename... Ts, typename B>
struct Intersects<TypeList<T, Ts...>, B>
    : std::bool_constant<kContains<T, B> || Intersects<TypeList<Ts...>, B>::value>
{
};

template <typename A, typename B>
inline constexpr bool kIntersects = Intersects<A, B>::value;

// ---------------------------------------------------------------------------
// Concat<A, B> — concatenate two TypeLists into one
// ---------------------------------------------------------------------------

template <typename A, typename B>
struct Concat;

template <typename... As, typename... Bs>
struct Concat<TypeList<As...>, TypeList<Bs...>>
{
    using Type = TypeList<As..., Bs...>;
};

template <typename A, typename B>
using ConcatT = typename Concat<A, B>::Type;

// ---------------------------------------------------------------------------
// IndexOf<T, List> — index of T in List (compile error if not found)
// ---------------------------------------------------------------------------

template <typename T, typename List, std::size_t I = 0>
struct IndexOf;

template <typename T, typename... Ts, std::size_t I>
struct IndexOf<T, TypeList<T, Ts...>, I> : std::integral_constant<std::size_t, I>
{
};

template <typename T, typename U, typename... Ts, std::size_t I>
struct IndexOf<T, TypeList<U, Ts...>, I> : IndexOf<T, TypeList<Ts...>, I + 1>
{
};

template <typename T, typename List>
inline constexpr std::size_t kIndexOf = IndexOf<T, List>::value;

}  // namespace engine::ecs
