#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>
#include <string_view>

//=============================================================================
// Authored verb identities
//
// All but one of the ids here are runtime values scoped to one catalog.
// Content persists the verb's qualified name and the binding's authored key;
// the slot numbers never reach a file, a wire, or a save. A catalog in another
// World hands the same numbers to different verbs, which is why a compiled
// binding records the catalog it resolved against beside the id it resolved
// to.
//
// VerbBindingKey is the exception: it is a name hashed, the way
// ComponentTypeId is, and it is what a component or a runtime table stores in
// place of the string.
//=============================================================================

// A verb's slot in one catalog. Dense and one-based, so id.Value - 1 indexes
// the dense tables a dispatcher keeps beside the registry.
using VerbId = StrongId<struct VerbTag, std::uint32_t>;

// Which catalog minted a VerbId. Monotonic per process: an allocator can hand
// a new registry the address a destroyed one had, so a pointer is not an
// identity a compiled binding can be checked against.
using VerbCatalogId = StrongId<struct VerbCatalogTag, std::uint64_t>;

// How many times a verb's argument contract has changed meaning or layout in
// this catalog. A binding compiled against an older revision is stale and has
// to be recompiled before it can be invoked again. Presentation-only edits --
// display name, description, category -- do not move it.
using VerbContractRevision = StrongId<struct VerbContractRevisionTag, std::uint32_t>;

// Which implementation is behind a verb right now. Replacing an implementation
// moves this, so a token minted for the previous one cannot unbind the
// replacement.
using VerbBindingGeneration = StrongId<struct VerbBindingGenerationTag, std::uint32_t>;

// One accepted request. Minted by the dispatcher when an operation accepts,
// monotonic within a catalog, zero when absent. Diagnostic and causality
// state: never a simulation ordering key, and never serialized.
using InvocationId = StrongId<struct InvocationTag, std::uint64_t>;

// A binding's authored key, hashed. The string is the authored identity and
// travels in the binding asset; this is what a component field or a runtime
// table holds instead, for the same reason PersistentEntityId is a number in a
// component and sixteen hex digits in a document.
using VerbBindingKey = StrongId<struct VerbBindingKeyTag, std::uint64_t>;

// FNV-1a over the key's exact bytes, constexpr and case-sensitive, matching
// MakeComponentTypeId so the two stable-name hashes in the engine are one
// mechanism written twice rather than two different ones. Distinct keys
// colliding at 64 bits is astronomically unlikely, and the asset still carries
// both strings, so a diagnostic can always name what it meant.
constexpr VerbBindingKey MakeVerbBindingKey(std::string_view key)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : key)
    {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ull;
    }
    if (hash == 0)
        hash = 1; // zero is StrongId's invalid sentinel; never hand it out.
    return VerbBindingKey{ hash };
}
