#pragma once

#include <core/identity/Id.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class World;

// Entity fields a load could not resolve yet, each filled when its target's id
// arrives. Dropped when the owning entity dies and at every level change.
// Made by the first such load, so a game that never loads one pays nothing.
class PendingReferences
{
public:
    void Await(EntityId owner, ComponentTypeId type, std::uint32_t fieldOffset, PersistentEntityId target);
    [[nodiscard]] bool Awaits(EntityId owner, ComponentTypeId type) const;
    void Forget(EntityId owner, ComponentTypeId type);
    void Arrived(World& world, PersistentEntityId id, EntityId entity);
    void Clear() { Waiting.clear(); }
    [[nodiscard]] std::size_t Count() const { return Waiting.size(); }

private:
    struct Reference
    {
        EntityId Owner;
        ComponentTypeId Type;
        std::uint32_t FieldOffset = 0;
        PersistentEntityId Target;
    };

    std::vector<Reference> Waiting;
};

// Forgets what a load left waiting for `owner`'s component, which code is about to set afresh.
void ForgetPendingReferences(World& world, EntityId owner, ComponentTypeId type);
