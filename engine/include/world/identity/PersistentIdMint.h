#pragma once

#include <core/identity/Id.h>
#include <ecs/EntityId.h>

#include <cstdint>

class World;

// The counter of the runtime half of persistent identity (bit 63), which no
// editor writes. A World resource, added by the first mint.
struct PersistentIdMint
{
    std::uint64_t Next = 1;
};

// Invalid where this World is not the simulation authority: only the
// authority creates identity.
[[nodiscard]] PersistentEntityId MintRuntimePersistentId(World& world);

// `entity`'s persistent id, minting one on the authority when it has none.
// Invalid on another machine, or for an entity that is not alive.
PersistentEntityId EnsurePersistentId(World& world, EntityId entity);
