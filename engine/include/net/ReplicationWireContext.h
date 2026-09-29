#pragma once

#include <ecs/EntityId.h>

#include <cstdint>

class ReplicationAuthorityIdentity;
class ReplicationClientIdentity;
class World;

// Exactly one identity is set. The authority's is writable so an entity the
// publish has not reached yet is minted now rather than sent as nothing.
struct ReplicationWireContext
{
    const World* Entities = nullptr;
    EntityId Entity;
    ReplicationAuthorityIdentity* Authority = nullptr;
    const ReplicationClientIdentity* Client = nullptr;

    // Its NetEntityId, or zero for an entity that is not replicated.
    [[nodiscard]] std::uint64_t WireEntity(EntityId entity) const;
    // Invalid when this machine has not been sent the entity (yet).
    [[nodiscard]] EntityId LocalEntity(std::uint64_t wire) const;
};
