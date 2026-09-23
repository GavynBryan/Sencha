#pragma once

#include <authored/AuthoredHandle.h>
#include <core/identity/StrongId.h>

#include <cstdint>

// Catalog- or dispatcher-local runtime ids; never persisted.

// Dense, one-based slot.
using AuthoredEventId = StrongId<struct AuthoredEventTag, std::uint32_t>;

using AuthoredEventCatalogId = StrongId<struct AuthoredEventCatalogTag, std::uint64_t>;

using AuthoredEventRevision = StrongId<struct AuthoredEventRevisionTag, std::uint32_t>;

// Publication order within one dispatcher.
using AuthoredEventSequence = StrongId<struct AuthoredEventSequenceTag, std::uint64_t>;

// Serials only increase, so each event's subscriber list is sorted by them.
struct AuthoredEventSubscriptionKey
{
    AuthoredEventId Event{};
    std::uint64_t Serial = 0;

    [[nodiscard]] bool IsValid() const { return Serial != 0; }
    friend bool operator==(const AuthoredEventSubscriptionKey&,
                           const AuthoredEventSubscriptionKey&) = default;
};

using AuthoredEventSubscriptionGeneration =
    StrongId<struct AuthoredEventSubscriptionGenerationTag, std::uint32_t>;

using AuthoredEventHandle =
    AuthoredHandle<AuthoredEventCatalogId, AuthoredEventId, AuthoredEventRevision>;
