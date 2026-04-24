#pragma once

// Query accessor tags — the vocabulary for Query<...> parameter lists.
// These are pure type-level markers; no runtime data.
//
// Read<T>    — const access to T column. Archetype must have T.
// Write<T>   — mutable access to T column. Bumps column version on access (conservative).
// With<T>    — archetype must have T; no accessor yielded (used for tags).
// Without<T> — archetype must NOT have T.
// Changed<T> — chunk-level filter: only visit chunks whose T column was written
//              at or after the reference frame.

template <typename T> struct Read    { using Component = T; };
template <typename T> struct Write   { using Component = T; };
template <typename T> struct With    { using Component = T; };
template <typename T> struct Without { using Component = T; };
template <typename T> struct Changed { using Component = T; };

// Traits for determining the accessor kind at compile time.
template <typename> struct IsRead    : std::false_type {};
template <typename T> struct IsRead<Read<T>>    : std::true_type {};

template <typename> struct IsWrite   : std::false_type {};
template <typename T> struct IsWrite<Write<T>>  : std::true_type {};

template <typename> struct IsWith    : std::false_type {};
template <typename T> struct IsWith<With<T>>    : std::true_type {};

template <typename> struct IsWithout : std::false_type {};
template <typename T> struct IsWithout<Without<T>> : std::true_type {};

template <typename> struct IsChanged : std::false_type {};
template <typename T> struct IsChanged<Changed<T>> : std::true_type {};

// True if an accessor provides a data column (Read or Write).
template <typename A>
constexpr bool AccessorHasColumn = IsRead<A>::value || IsWrite<A>::value;

#include <type_traits>
