#include <navigation/NavigationVerbs.h>

#include <ecs/World.h>
#include <navigation/NavAgentRequests.h>
#include <navigation/NavTraversal.h>

namespace
{
    const NavAgentProgress* ProgressOf(const World& world, EntityId agent)
    {
        return world.IsRegistered<NavAgentProgress>() ? world.TryGet<NavAgentProgress>(agent) : nullptr;
    }

    template <Vec3d NavAbilityCrossing::*Point, bool NeedsAnchor>
    std::optional<Vec3d> CrossingPoint(const World& world, EntityId agent)
    {
        const NavAbilityCrossings* crossings = world.TryGetResource<NavAbilityCrossings>();
        if (crossings == nullptr)
            return std::nullopt;
        for (const NavAbilityCrossing& crossing : crossings->Active)
            if (crossing.Agent == agent && (!NeedsAnchor || crossing.Anchored))
                return crossing.*Point;
        return std::nullopt;
    }

    VerbAdmission AdmissionFor(NavRequestId issued)
    {
        return issued.IsValid() ? VerbAdmission::Accepted : VerbAdmission::Refused;
    }
}

VerbAdmission NavigationOperations::MoveTo(EntityId target, Vec3d point, float arriveRadius, float speed)
{
    NavDestination destination;
    destination.Point = point;
    destination.ArriveRadius = arriveRadius;
    destination.SpeedScale = speed;
    return AdmissionFor(RequestNavigation(*Entities, target, destination));
}

VerbAdmission NavigationOperations::Approach(EntityId target, EntityId subject, bool follow, float arriveRadius,
                                             float speed)
{
    NavDestination destination = NavDestination::AtEntity(subject);
    destination.Follow = follow;
    destination.ArriveRadius = arriveRadius;
    destination.SpeedScale = speed;
    return AdmissionFor(RequestNavigation(*Entities, target, destination));
}

VerbAdmission NavigationOperations::Stop(EntityId target)
{
    return AdmissionFor(CancelNavigation(*Entities, target));
}

std::optional<NavAgentPhase> NavigationOperations::Phase(EntityId agent) const
{
    const NavAgentProgress* progress = ProgressOf(*Entities, agent);
    return progress != nullptr ? std::optional<NavAgentPhase>(progress->Phase) : std::nullopt;
}

std::optional<NavAgentFailure> NavigationOperations::Failure(EntityId agent) const
{
    const NavAgentProgress* progress = ProgressOf(*Entities, agent);
    return progress != nullptr ? std::optional<NavAgentFailure>(progress->Failure) : std::nullopt;
}

std::optional<float> NavigationOperations::Remaining(EntityId agent) const
{
    const NavAgentProgress* progress = ProgressOf(*Entities, agent);
    return progress != nullptr ? std::optional<float>(progress->Remaining) : std::nullopt;
}

std::optional<Vec3d> NavigationOperations::CrossingStart(EntityId agent) const
{
    return CrossingPoint<&NavAbilityCrossing::Start, false>(*Entities, agent);
}

std::optional<Vec3d> NavigationOperations::CrossingExit(EntityId agent) const
{
    return CrossingPoint<&NavAbilityCrossing::Exit, false>(*Entities, agent);
}

std::optional<Vec3d> NavigationOperations::CrossingAnchor(EntityId agent) const
{
    return CrossingPoint<&NavAbilityCrossing::Anchor, true>(*Entities, agent);
}

std::optional<Vec3d> NavigationOperations::CrossingRelease(EntityId agent) const
{
    return CrossingPoint<&NavAbilityCrossing::Release, false>(*Entities, agent);
}
