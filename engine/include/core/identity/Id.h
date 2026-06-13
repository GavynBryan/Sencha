#pragma once

#include <core/identity/StrongId.h>

//=============================================================================
// Identity types
//
// Distinct value types for tagging types and serialized entities. Each is a
// StrongId<Tag, uint32_t> — the engine's single id vocabulary — so the phantom
// Tag prevents accidental mixing (e.g. passing a TypeId where a
// SerializedEntityId is expected) while sharing one implementation of
// equality, ordering, hashing, and binary serialization.
//
// A zero Value means "invalid / unset". The explicit operator bool() lets
// you write: if (id) { ... }
//
// The stable asset identity (AssetId) is also a StrongId, but 64-bit and with
// its own text form; it lives in core/assets/AssetId.h (Decision A).
//=============================================================================

using TypeId             = StrongId<struct TypeIdTag, std::uint32_t>;
using SerializedEntityId = StrongId<struct SerializedEntityIdTag, std::uint32_t>;
