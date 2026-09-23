#pragma once

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

// One subscription, and the generation it was minted with.
using AuthoredEventSubscriptionId = StrongId<struct AuthoredEventSubscriptionTag, std::uint64_t>;
using AuthoredEventSubscriptionGeneration =
    StrongId<struct AuthoredEventSubscriptionGenerationTag, std::uint32_t>;
