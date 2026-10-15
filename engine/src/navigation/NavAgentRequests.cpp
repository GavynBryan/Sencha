#include <navigation/NavAgentRequests.h>

#include <ecs/World.h>

#include <algorithm>
#include <cmath>

namespace
{
    NavRequestId Issue(World& world, EntityId agent, NavAgentRequest order)
    {
        if (!world.IsRegistered<NavAgentRequest>())
            return {};
        NavAgentRequest* current = world.TryGet<NavAgentRequest>(agent);
        if (current == nullptr)
            return {};
        order.Id.Value = current->Id.Value + 1 != 0 ? current->Id.Value + 1 : 1;
        *current = order;
        return order.Id;
    }

    float ValidOr(float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    }
}

NavRequestId RequestNavigation(World& world, EntityId agent, const NavDestination& destination)
{
    NavAgentRequest order;
    order.Kind = destination.Kind;
    order.Point = destination.Point;
    order.Target = destination.Kind == NavDestinationKind::Entity ? destination.Target : EntityId{};
    order.Follow = destination.Kind == NavDestinationKind::Entity && destination.Follow;
    order.ArriveRadius = std::max(0.0f, ValidOr(destination.ArriveRadius, 0.0f));
    const float speed = ValidOr(destination.SpeedScale, 1.0f);
    order.SpeedScale = speed > 0.0f ? std::min(speed, 1.0f) : 1.0f;
    return Issue(world, agent, order);
}

NavRequestId CancelNavigation(World& world, EntityId agent)
{
    return Issue(world, agent, NavAgentRequest{});
}

NavRequestState ReadNavigation(const World& world, EntityId agent, NavRequestId request)
{
    if (!world.IsRegistered<NavAgentRequest>() || !world.IsRegistered<NavAgentProgress>())
        return {};
    const NavAgentRequest* order = world.TryGet<NavAgentRequest>(agent);
    const NavAgentProgress* progress = world.TryGet<NavAgentProgress>(agent);
    if (order == nullptr || progress == nullptr || !request.IsValid())
        return {};
    if (order->Id != request)
        return { NavRequestStanding::Superseded, NavAgentPhase::Idle, NavAgentFailure::None };
    if (progress->Request != request)
    {
        const NavAgentPhase pending =
            order->Kind == NavDestinationKind::None ? NavAgentPhase::Cancelled : NavAgentPhase::Planning;
        return { NavRequestStanding::Current, pending, NavAgentFailure::None };
    }
    return { NavRequestStanding::Current, progress->Phase, progress->Failure };
}
