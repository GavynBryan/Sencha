#pragma once

#include <core/identity/Id.h>
#include <ecs/EntityId.h>

#include <cstdint>

class World;

struct PersistentIdMint
{
    std::uint64_t Next = 1;
};

// Authority only: invalid where this World is not the simulation authority.
[[nodiscard]] PersistentEntityId MintRuntimePersistentId(World& world);

// Mints only on the authority; invalid elsewhere for an entity without an id.
PersistentEntityId EnsurePersistentId(World& world, EntityId entity);
