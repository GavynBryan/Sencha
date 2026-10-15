#include "NavRouteSlots.h"

void NavRouteSlot::Reset(EntityId owner)
{
    Owner = owner;
    Route.Clear();
    HasRoute = false;
    Step = 0;
    Corner = 0;
    LegStart = Vec3d::Zero();
    CrossingTicks = 0;
    ReplanAfterCrossing = NavRepathReason::None;
    Stuck = NavStuckWindow{};
    StuckReplans = 0;
    CrossingFailures = 0;
    ReplanningAroundFailedCrossing = false;
    Avoid = {};
    AvoidUntilTick = {};
    PlannedTarget = Vec3d::Zero();
    PlanTick = 0;
    OffMesh = false;
    OffMeshSinceTick = 0;
    LastDiagnostics = NavQueryDiagnostics{};
}

std::uint32_t NavRouteSlots::Acquire(EntityId owner)
{
    std::uint32_t index = 0;
    if (!Free.empty())
    {
        index = Free.back();
        Free.pop_back();
    }
    else
    {
        index = static_cast<std::uint32_t>(Slots.size());
        Slots.emplace_back(Capacity);
        InUse.push_back(false);
    }
    InUse[index] = true;
    ++Used;
    Slots[index].Reset(owner);
    return index;
}

void NavRouteSlots::Release(std::uint32_t index)
{
    if (index >= Slots.size() || !InUse[index])
        return;
    InUse[index] = false;
    Slots[index].Owner = EntityId{};
    Free.push_back(index);
    --Used;
}

NavRouteSlot* NavRouteSlots::Owned(std::uint32_t index, EntityId owner)
{
    if (index >= Slots.size() || !InUse[index] || Slots[index].Owner != owner)
        return nullptr;
    return &Slots[index];
}
