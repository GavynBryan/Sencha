#include "TwoTuIdentity.h"

// All of these instantiate World templates against TwoTuComponent in THIS
// translation unit; the test reads them back from another.
ComponentId RegisterTwoTuComponentInOtherUnit(EntityStore& world)
{
    return world.RegisterComponent<TwoTuComponent>();
}

std::uint64_t TwoTuComponentIdFromOtherUnit()
{
    return ResolveComponentTypeId<TwoTuComponent>().Value;
}

void AddTwoTuComponentInOtherUnit(EntityStore& world, EntityId entity, int value)
{
    world.AddComponent<TwoTuComponent>(entity, { value });
}
