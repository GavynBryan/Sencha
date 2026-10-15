#pragma once

#include <abilities/ActiveAbilities.h>
#include <ecs/LeaseOwner.h>

#include <cstdint>

class VerbDispatcher;

// Whom a record's op holds a lease as: its actor, keyed by the activation's
// identity and the op, so a guess and the authority's record hold as one.
[[nodiscard]] LeaseOwner AbilityLeaseOwner(EntityId actor, const AbilityRecord& record, std::uint8_t op);

// Lets go of everything `record` holds, newest first, so a hold taken on top of
// another goes before it.
void ReleaseAbilityLeases(VerbDispatcher* verbs, EntityId actor, AbilityRecord& record, std::uint64_t tick);
