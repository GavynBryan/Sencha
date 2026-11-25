#include <navigation/NavAgentRequests.h>

#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <world/identity/PendingReferences.h>

#include <algorithm>
#include <cmath>

namespace
{
    NavRequestId IssueOrder(World& world, EntityId agent, NavAgentRequest order)
    {
        if (!world.IsRegistered<NavAgentRequest>())
            return {};
        NavAgentRequest* current = world.TryGet<NavAgentRequest>(agent);
        if (current == nullptr)
            return {};
        order.Id.Value = current->Id.Value + 1 != 0 ? current->Id.Value + 1 : 1;
        *current = order;
        ForgetPendingReferences(world, agent, ResolveComponentTypeId<NavAgentRequest>());
        return order.Id;
    }

    float FiniteOr(float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    }
}

void ComponentTraits<NavAgentRequest>::OnAdd(NavAgentRequest& order, World&, EntityId)
{
    if (order.Kind != NavDestinationKind::None && !order.Id.IsValid())
        order.Id.Value = 1;
}

NavRequestId RequestNavigation(World& world, EntityId agent, const NavDestination& destination)
{
    NavAgentRequest order;
    order.Kind = destination.Kind;
    order.Point = destination.Point;
    order.Target = destination.Kind == NavDestinationKind::Entity ? destination.Target : EntityId{};
    order.Follow = destination.Kind == NavDestinationKind::Entity && destination.Follow;
    order.ArriveRadius = std::max(0.0f, FiniteOr(destination.ArriveRadius, 0.0f));
    const float speed = FiniteOr(destination.SpeedScale, 1.0f);
    order.SpeedScale = speed > 0.0f ? std::min(speed, 1.0f) : 1.0f;
    return IssueOrder(world, agent, order);
}

NavRequestId CancelNavigation(World& world, EntityId agent)
{
    return IssueOrder(world, agent, NavAgentRequest{});
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
