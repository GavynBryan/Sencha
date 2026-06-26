#include "TwoTuIdentity.h"

// All of these instantiate World templates against TwoTuComponent in THIS
// translation unit; the test reads them back from another.
ComponentId RegisterTwoTuComponentInOtherUnit(World& world)
{
    return world.RegisterComponent<TwoTuComponent>();
}

std::uint64_t TwoTuComponentIdFromOtherUnit()
{
    return ResolveComponentTypeId<TwoTuComponent>().Value;
}

void AddTwoTuComponentInOtherUnit(World& world, EntityId entity, int value)
{
    world.AddComponent<TwoTuComponent>(entity, { value });
}
