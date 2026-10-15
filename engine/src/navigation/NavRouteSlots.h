#pragma once

#include <ecs/EntityId.h>
#include <navigation/NavAgent.h>
#include <navigation/NavRouteFollowing.h>
#include <navigation/NavRoutePlanning.h>
#include <navigation/NavigationTypes.h>

#include <array>
#include <cstdint>
#include <vector>

// What the follower keeps for one agent with a live request: its route, how far
// along it is, and the bookkeeping its repath rules need.
struct NavRouteSlot
{
    EntityId Owner;
    NavRouteBuffer Route;
    bool HasRoute = false;

    std::uint32_t Step = 0;
    std::uint32_t Corner = 0;
    Vec3d LegStart = Vec3d::Zero();
    float Height = 1.8f;

    std::uint32_t CrossingTicks = 0;
    // An invalidation seen mid-crossing, acted on when the crossing ends.
    NavRepathReason ReplanAfterCrossing = NavRepathReason::None;

    NavStuckWindow Stuck;
    std::uint8_t StuckReplans = 0;
    std::uint8_t CrossingFailures = 0;
    // The replan in flight was asked for because a crossing failed.
    bool ReplanningAroundFailedCrossing = false;

    std::array<NavLinkId, kNavPlanAvoidLinks> Avoid{};
    std::array<std::uint64_t, kNavPlanAvoidLinks> AvoidUntilTick{};

    Vec3d PlannedTarget = Vec3d::Zero();
    std::uint64_t PlanTick = 0;
    bool OffMesh = false;
    std::uint64_t OffMeshSinceTick = 0;
    NavQueryDiagnostics LastDiagnostics;

    explicit NavRouteSlot(const NavRouteCapacity& capacity)
        : Route(capacity.Steps, capacity.Corners, capacity.Dependencies)
    {
    }

    // Everything but the route's storage, which is kept for the next owner.
    void Reset(EntityId owner);
};

// The follower's slots. A slot index is honoured only while the slot's owner is
// the entity asking, so a copied component never aliases another agent's route.
class NavRouteSlots
{
public:
    explicit NavRouteSlots(NavRouteCapacity capacity) : Capacity(capacity) {}

    [[nodiscard]] std::uint32_t Acquire(EntityId owner);
    void Release(std::uint32_t index);
    [[nodiscard]] NavRouteSlot* Owned(std::uint32_t index, EntityId owner);

    // Calls `release(owner, index)` for, then frees, each slot `keep` rejects.
    template <typename Keep, typename OnRelease>
    void Reclaim(Keep&& keep, OnRelease&& release)
    {
        for (std::uint32_t i = 0; i < Slots.size(); ++i)
            if (InUse[i] && !keep(Slots[i].Owner))
            {
                release(Slots[i].Owner, i);
                Release(i);
            }
    }

    [[nodiscard]] std::size_t InUseCount() const { return Used; }
    [[nodiscard]] std::size_t SlotCount() const { return Slots.size(); }

private:
    NavRouteCapacity Capacity;
    std::vector<NavRouteSlot> Slots;
    std::vector<bool> InUse;
    std::vector<std::uint32_t> Free;
    std::size_t Used = 0;
};
