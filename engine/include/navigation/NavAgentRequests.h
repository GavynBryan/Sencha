#pragma once

#include <navigation/NavAgent.h>

class World;

struct NavDestination
{
    NavDestinationKind Kind = NavDestinationKind::Point;
    Vec3d Point = Vec3d::Zero();
    EntityId Target;
    bool Follow = false;
    float ArriveRadius = 0.0f;
    float SpeedScale = 1.0f;

    [[nodiscard]] static NavDestination AtPoint(const Vec3d& point)
    {
        NavDestination destination;
        destination.Point = point;
        return destination;
    }

    [[nodiscard]] static NavDestination AtEntity(EntityId target, bool follow = false)
    {
        NavDestination destination;
        destination.Kind = NavDestinationKind::Entity;
        destination.Target = target;
        destination.Follow = follow;
        return destination;
    }
};

// Replaces the agent's current order with a new one and returns its id; an
// invalid id when `agent` is not a live NavAgent. The only writers of
// NavAgentRequest are these two.
NavRequestId RequestNavigation(World& world, EntityId agent, const NavDestination& destination);
// Ends the current order. Progress reports Cancelled under the returned id.
NavRequestId CancelNavigation(World& world, EntityId agent);

enum class NavRequestStanding : std::uint8_t
{
    Current,
    // Another order replaced it; its outcome will never be reported.
    Superseded,
    NoAgent,
};

struct NavRequestState
{
    NavRequestStanding Standing = NavRequestStanding::NoAgent;
    NavAgentPhase Phase = NavAgentPhase::Idle;
    NavAgentFailure Failure = NavAgentFailure::None;
};

// What became of `request`. An order the follower has not taken up yet reads
// as Planning, or Cancelled for a cancel.
[[nodiscard]] NavRequestState ReadNavigation(const World& world, EntityId agent, NavRequestId request);
