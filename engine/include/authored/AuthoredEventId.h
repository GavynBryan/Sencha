#pragma once

#include <authored/AuthoredHandle.h>
#include <core/identity/StrongId.h>

#include <cstdint>

//=============================================================================
// Authored event identities
//
// Runtime values scoped to one catalog or one dispatcher. Content persists an
// event's name; none of these numbers reach a file, a wire, or a save.
//=============================================================================

// An event's slot in one catalog. Dense and one-based.
using AuthoredEventId = StrongId<struct AuthoredEventTag, std::uint32_t>;

// Which catalog minted an AuthoredEventId.
using AuthoredEventCatalogId = StrongId<struct AuthoredEventCatalogTag, std::uint64_t>;

// How many times an event's payload contract has changed meaning in this
// catalog.
using AuthoredEventRevision = StrongId<struct AuthoredEventRevisionTag, std::uint32_t>;

// One published occurrence, in publication order within a dispatcher.
// Diagnostic and causality state: never a simulation ordering key beyond the
// FIFO the dispatcher already guarantees, and never serialized.
using AuthoredEventSequence = StrongId<struct AuthoredEventSequenceTag, std::uint64_t>;

// One subscription: the event whose list it is in, and its serial in that
// list. Serials only increase, so a list kept in subscription order is also
// sorted by serial, and a subscription is found without scanning anything
// but a binary search of its own event's list.
struct AuthoredEventSubscriptionKey
{
    AuthoredEventId Event{};
    std::uint64_t Serial = 0;

    [[nodiscard]] bool IsValid() const { return Serial != 0; }
    friend bool operator==(const AuthoredEventSubscriptionKey&,
                           const AuthoredEventSubscriptionKey&) = default;
};

// The generation a subscription was minted with.
using AuthoredEventSubscriptionGeneration =
    StrongId<struct AuthoredEventSubscriptionGenerationTag, std::uint32_t>;

// An event name resolved against one catalog, which is what a compiled
// consumer stores and what the dispatcher subscribes by.
using AuthoredEventHandle =
    AuthoredHandle<AuthoredEventCatalogId, AuthoredEventId, AuthoredEventRevision>;
