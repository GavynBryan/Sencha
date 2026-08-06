#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <core/identity/StrongId.h>

//=============================================================================
// Identity types
//
// Distinct value types for tagging types and persisted entities. Each is a
// StrongId — the engine's single id vocabulary — so the phantom Tag prevents
// accidental mixing (e.g. passing a TypeId where a PersistentEntityId is
// expected) while sharing one implementation of equality, ordering, hashing,
// and binary serialization.
//
// A zero Value means "invalid / unset". The explicit operator bool() lets
// you write: if (id) { ... }
//
// The stable asset identity (AssetId) is also a StrongId with its own text
// form; it lives in core/assets/AssetId.h (Decision A).
//=============================================================================

using TypeId = StrongId<struct TypeIdTag, std::uint32_t>;

// The persistent entity identity: minted by the editor when an entity is
// authored, serialized per entity in scene documents, carried verbatim through
// the cook, and resolved at runtime to the live generational EntityId through
// the world's PersistentEntityIndex. Runtime EntityId values are never
// persisted; this id is the only entity identity that crosses a save, a cook,
// or a zone unload.
//
// Bit 63 is reserved. Editor mints always clear it, so a future runtime
// allocator (persistent identity for dynamically spawned entities) can mint
// from the disjoint high half without ever colliding with authored content.
using PersistentEntityId = StrongId<struct PersistentEntityIdTag, std::uint64_t>;

inline constexpr std::uint64_t PersistentEntityIdRuntimeBit = 1ull << 63;

// Text form follows the AssetId precedent: 16-digit lowercase hex, no prefix,
// because JSON numbers are doubles and cannot hold 64 bits. FromString is
// strict: exactly 16 lowercase hex digits and nonzero, anything else is
// nullopt so malformed content fails loudly at parse time.
[[nodiscard]] std::string PersistentEntityIdToString(PersistentEntityId id);
[[nodiscard]] std::optional<PersistentEntityId>
PersistentEntityIdFromString(std::string_view text);
