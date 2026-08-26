#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

//=============================================================================
// FNV-1a, incremental
//
// A 64-bit hash accumulated field by field, for state digests: "has this
// shadow view's content changed since we rendered it", "did the editor's
// lights change since the last cook". The caller seeds an accumulator and
// folds values into it as it walks whatever it is summarising.
//
// Distinct from ContentHash's HashBytes64, which is one-shot XXH64 over a byte
// span and belongs to asset invalidation (pipeline Decision A). Chaining that
// through its seed would work but pays XXH64's setup per field, and these
// callers fold a dozen 4-to-64-byte values at a time.
//
// Not cryptographic, and not stable across builds in any sense worth relying
// on: these digests compare a value against one produced moments earlier in
// the same process. Never serialise one.
//=============================================================================

// The published FNV-1a 64-bit parameters. All four copies this replaced
// carried 1469598103934665603 for the basis, which is the real value with its
// last digit dropped -- one typo propagated by copy. Harmless in practice
// (any odd seed hashes fine, and the prime was right), but it made the name a
// lie, and no digest here is persisted or compared across processes, so
// correcting it changes no behaviour.
inline constexpr std::uint64_t kFnv1aOffsetBasis = 14695981039346656037ull;
inline constexpr std::uint64_t kFnv1aPrime = 1099511628211ull;

constexpr void HashFnv1aByte(std::uint64_t& hash, std::uint8_t value)
{
    hash ^= value;
    hash *= kFnv1aPrime;
}

inline void HashFnv1aBytes(std::uint64_t& hash, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index)
        HashFnv1aByte(hash, bytes[index]);
}

// Folds an object's bytes in. Trivially copyable only: hashing the
// representation of anything else would be reading padding around pointers, or
// worse, hashing the pointer instead of what it points at.
template <typename T>
void HashFnv1aValue(std::uint64_t& hash, const T& value)
    requires std::is_trivially_copyable_v<T>
{
    HashFnv1aBytes(hash, &value, sizeof(T));
}
