#pragma once

#include <authored/AuthoredHandle.h>
#include <core/identity/StrongId.h>

#include <cstdint>

//=============================================================================
// Authored query identities
//
// Runtime values scoped to one catalog, with the same contract as the verb
// ids: content persists a query's name, and these numbers never reach a file,
// a wire, or a save.
//=============================================================================

// A query's slot in one catalog. Dense and one-based, so id.Value - 1 indexes
// the dispatcher's table.
using AuthoredQueryId = StrongId<struct AuthoredQueryTag, std::uint32_t>;

// Which catalog minted an AuthoredQueryId.
using AuthoredQueryCatalogId = StrongId<struct AuthoredQueryCatalogTag, std::uint64_t>;

// How many times a query's contract -- its arguments or its result -- has
// changed meaning in this catalog.
using AuthoredQueryRevision = StrongId<struct AuthoredQueryRevisionTag, std::uint32_t>;

// Which implementation answers a query right now.
using AuthoredQueryBindingGeneration =
    StrongId<struct AuthoredQueryBindingGenerationTag, std::uint32_t>;

// A query name resolved against one catalog, which is what a compiled consumer
// stores and what the dispatcher evaluates.
using AuthoredQueryHandle =
    AuthoredHandle<AuthoredQueryCatalogId, AuthoredQueryId, AuthoredQueryRevision>;
