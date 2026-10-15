#include <ecs/World.h>
#include <navigation/NavAgentRequests.h>
#include <navigation/NavigationComponentRegistration.h>
#include <world/ComponentRegistrar.h>

#include <gtest/gtest.h>

namespace
{
    struct Agents
    {
        World Entities;

        Agents()
        {
            ComponentRegistrar registrar(Entities);
            RegisterNavigationComponents(registrar);
        }

        EntityId Agent()
        {
            const EntityId agent = Entities.CreateEntity();
            Entities.AddComponent<NavAgent>(agent, NavAgent{});
            return agent;
        }
    };
}

TEST(NavAgentRequests, AnAgentComesWithItsRequestAndProgress)
{
    Agents agents;
    const EntityId agent = agents.Agent();
    EXPECT_TRUE(agents.Entities.HasComponent<NavAgentRequest>(agent));
    EXPECT_TRUE(agents.Entities.HasComponent<NavAgentProgress>(agent));
}

TEST(NavAgentRequests, EachOrderGetsAFreshIdAndReplacesTheLast)
{
    Agents agents;
    const EntityId agent = agents.Agent();
    const NavRequestId first = RequestNavigation(agents.Entities, agent, NavDestination::AtPoint(Vec3d(1.0f, 0.0f, 0.0f)));
    const NavRequestId second = RequestNavigation(agents.Entities, agent, NavDestination::AtPoint(Vec3d(2.0f, 0.0f, 0.0f)));
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());
    EXPECT_NE(first, second);

    EXPECT_EQ(ReadNavigation(agents.Entities, agent, first).Standing, NavRequestStanding::Superseded);
    const NavRequestState current = ReadNavigation(agents.Entities, agent, second);
    EXPECT_EQ(current.Standing, NavRequestStanding::Current);
    EXPECT_EQ(current.Phase, NavAgentPhase::Planning);

    const NavAgentRequest* order = std::as_const(agents.Entities).TryGet<NavAgentRequest>(agent);
    EXPECT_EQ(order->Point, Vec3d(2.0f, 0.0f, 0.0f));
}

TEST(NavAgentRequests, ACancelIsAnOrderOfItsOwn)
{
    Agents agents;
    const EntityId agent = agents.Agent();
    const NavRequestId go = RequestNavigation(agents.Entities, agent, NavDestination::AtPoint(Vec3d(1.0f, 0.0f, 0.0f)));
    const NavRequestId stop = CancelNavigation(agents.Entities, agent);
    EXPECT_EQ(ReadNavigation(agents.Entities, agent, go).Standing, NavRequestStanding::Superseded);
    const NavRequestState cancelled = ReadNavigation(agents.Entities, agent, stop);
    EXPECT_EQ(cancelled.Standing, NavRequestStanding::Current);
    EXPECT_EQ(cancelled.Phase, NavAgentPhase::Cancelled);
}

TEST(NavAgentRequests, OnlyAgentsTakeOrders)
{
    Agents agents;
    const EntityId bystander = agents.Entities.CreateEntity();
    EXPECT_FALSE(RequestNavigation(agents.Entities, bystander, NavDestination{}).IsValid());
    EXPECT_EQ(ReadNavigation(agents.Entities, bystander, NavRequestId{ 1 }).Standing, NavRequestStanding::NoAgent);
}

TEST(NavAgentRequests, OrdersAreSanitised)
{
    Agents agents;
    const EntityId agent = agents.Agent();
    const EntityId target = agents.Entities.CreateEntity();
    NavDestination muddled = NavDestination::AtPoint(Vec3d(1.0f, 0.0f, 0.0f));
    muddled.Target = target;
    muddled.Follow = true;
    muddled.ArriveRadius = -2.0f;
    muddled.SpeedScale = 0.0f;
    (void)RequestNavigation(agents.Entities, agent, muddled);
    const NavAgentRequest* order = std::as_const(agents.Entities).TryGet<NavAgentRequest>(agent);
    EXPECT_FALSE(order->Target.IsValid());
    EXPECT_FALSE(order->Follow);
    EXPECT_FLOAT_EQ(order->ArriveRadius, 0.0f);
    EXPECT_FLOAT_EQ(order->SpeedScale, 1.0f);

    NavDestination follow = NavDestination::AtEntity(target, true);
    follow.SpeedScale = 0.5f;
    (void)RequestNavigation(agents.Entities, agent, follow);
    EXPECT_EQ(order->Target, target);
    EXPECT_TRUE(order->Follow);
    EXPECT_FLOAT_EQ(order->SpeedScale, 0.5f);
}
