#pragma once

#include <authored/AuthoredHandle.h>
#include <core/identity/StrongId.h>

#include <cstdint>

// Catalog-local runtime ids; content persists names, never these.

// Dense, one-based slot.
using AuthoredQueryId = StrongId<struct AuthoredQueryTag, std::uint32_t>;

using AuthoredQueryCatalogId = StrongId<struct AuthoredQueryCatalogTag, std::uint64_t>;

using AuthoredQueryRevision = StrongId<struct AuthoredQueryRevisionTag, std::uint32_t>;

using AuthoredQueryBindingGeneration =
    StrongId<struct AuthoredQueryBindingGenerationTag, std::uint32_t>;

using AuthoredQueryHandle =
    AuthoredHandle<AuthoredQueryCatalogId, AuthoredQueryId, AuthoredQueryRevision>;
