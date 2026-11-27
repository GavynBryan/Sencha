#include <navigation/NavigationComponentRegistration.h>

#include <navigation/NavAgentRequests.h>
#include <world/relocation/RelocationRules.h>

#include <utility>

namespace
{
    // Given again with a fresh id, so its route is planned again from the new
    // side; a point order's point is carried with the agent. The progress
    // describes the superseded order and is replaced when the follower plans.
    void OrderAgain(void*, World& world, EntityId agent, const RelocationFrame& frame)
    {
        const NavAgentRequest* order = std::as_const(world).TryGet<NavAgentRequest>(agent);
        if (order == nullptr || order->Kind == NavDestinationKind::None)
            return;
        NavDestination destination;
        destination.Kind = order->Kind;
        destination.Point = frame.MapPoint(order->Point);
        destination.Target = order->Target;
        destination.Follow = order->Follow;
        destination.ArriveRadius = order->ArriveRadius;
        destination.SpeedScale = order->SpeedScale;
        (void)RequestNavigation(world, agent, destination);
    }
}

void RegisterNavigationRelocation(World& world)
{
    SetRelocationRule<NavAgentRequest>(world, RelocationRuleKind::Maps, &OrderAgain);
}
