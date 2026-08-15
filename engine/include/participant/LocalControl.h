#pragma once

#include <ecs/EntityId.h>

class World;

// The one entity this machine currently presents and drives. The resource does
// not retain the entity; a dead handle reads as no subject.
struct LocalControlSubject
{
    EntityId Value;
};

struct LocalControlChange
{
    EntityId Previous;
    EntityId Current;
    bool Changed = false;
};

[[nodiscard]] EntityId LocalControlSubjectOf(const World& world);

// Updates the local-control resource and its LocalLookControl projection as one
// operation. An invalid or dead entity relinquishes local control.
LocalControlChange SetLocalControlSubject(World& world, EntityId entity);

