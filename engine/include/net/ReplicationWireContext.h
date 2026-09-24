#pragma once

#include <ecs/EntityId.h>

#include <cstdint>

class ReplicationAuthorityIdentity;
class ReplicationClientIdentity;
class World;

//=============================================================================
// ReplicationWireContext
//
// What a component codec needs to turn process-local values into what every
// machine agrees on and back: the World the component lives in, the entity it
// belongs to, and how this machine names replicated entities -- by the
// authority's mint, or by a client's map of what it was sent. Exactly one of
// the two identities is set.
//
// The authority's is writable because a component can name a replicated
// entity the publish has not reached yet; it is minted then rather than sent
// as nothing for a tick.
//=============================================================================
struct ReplicationWireContext
{
    const World* Entities = nullptr;
    EntityId Entity;
    ReplicationAuthorityIdentity* Authority = nullptr;
    const ReplicationClientIdentity* Client = nullptr;

    // A local entity as the wire names it: its NetEntityId, or zero for one
    // that is not replicated and so means nothing to another machine.
    [[nodiscard]] std::uint64_t WireEntity(EntityId entity) const;
    // The entity a wire name stands for here, or invalid when this machine
    // has not been sent it (yet).
    [[nodiscard]] EntityId LocalEntity(std::uint64_t wire) const;
};
