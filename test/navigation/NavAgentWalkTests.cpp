// Agents walking cooked navigation through the real fixed tick: movement,
// character physics, transform propagation and navigation's own PostFixed work.

#include "NavAgentHarness.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{


}

// A: around the wall, through its gap, to the far side.
TEST(NavAgentWalk, WalksAroundAWallAndArrives)
{
    NavAgentHarness harness(WallRoom());
    const EntityId agent = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    const NavRequestId request = harness.GoTo(agent, Vec3d(17.0f, 0.0f, 3.0f));

    bool crossedThroughGap = false;
    bool enteredWall = false;
    const bool arrived = harness.StepUntil([&] {
        const Vec3d p = harness.Position(agent);
        if (p.X > 9.2f && p.X < 10.8f && p.Z < 6.7f)
            enteredWall = true;
        if (std::abs(p.X - 10.0f) < 0.5f && p.Z > 7.0f)
            crossedThroughGap = true;
        return harness.HasEnded(agent);
    }, 60 * 30);

    ASSERT_TRUE(arrived);
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived) << NavStatusName(harness.Progress(agent).LastQuery);
    EXPECT_EQ(ReadNavigation(harness.Entities(), agent, request).Phase, NavAgentPhase::Arrived);
    EXPECT_TRUE(crossedThroughGap);
    EXPECT_FALSE(enteredWall);
    EXPECT_LE(NavPlanarDistance(harness.Position(agent), Vec3d(17.0f, 0.0f, 3.0f)), 0.5f + 0.25f);
    EXPECT_TRUE(harness.AsksForNothing(agent)) << "arriving leaves the character asking for nothing";
    EXPECT_EQ(harness.Agents().RouteSlotsInUse(), 0u);
}

// D: a replaced order never reports, and a cancel stops the agent that tick.
TEST(NavAgentWalk, AReplacedOrderNeverReportsAndACancelStopsAtOnce)
{
    NavAgentHarness harness(LongHall());
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    const NavRequestId first = harness.GoTo(agent, Vec3d(38.0f, 0.0f, 5.0f));
    for (int i = 0; i < 60; ++i)
        harness.Step();
    ASSERT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Moving);

    const NavRequestId second = harness.GoTo(agent, Vec3d(4.0f, 0.0f, 8.0f));
    bool firstEverArrived = false;
    const bool ended = harness.StepUntil([&] {
        const NavAgentProgress& progress = harness.Progress(agent);
        if (progress.Request == first && progress.Phase == NavAgentPhase::Arrived)
            firstEverArrived = true;
        EXPECT_EQ(ReadNavigation(harness.Entities(), agent, first).Standing, NavRequestStanding::Superseded);
        return harness.HasEnded(agent);
    }, 60 * 20);
    ASSERT_TRUE(ended);
    EXPECT_FALSE(firstEverArrived);
    EXPECT_EQ(harness.Progress(agent).Request, second);
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived);

    (void)harness.GoTo(agent, Vec3d(38.0f, 0.0f, 5.0f));
    for (int i = 0; i < 60; ++i)
        harness.Step();
    ASSERT_FALSE(harness.AsksForNothing(agent));
    const NavRequestId stop = CancelNavigation(harness.Entities(), agent);
    harness.Step();
    EXPECT_EQ(harness.Progress(agent).Request, stop);
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Cancelled);
    EXPECT_TRUE(harness.AsksForNothing(agent)) << "the cancel's tick already asks for nothing";
    EXPECT_EQ(harness.Agents().RouteSlotsInUse(), 0u);
}

// F: what an agent holds goes when it, its agent component, or its zone does.
TEST(NavAgentWalk, AnAgentsStateEndsWithItsEntityAndItsComponent)
{
    NavAgentHarness harness(LongHall());
    const EntityId doomed = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 3.0f));
    const EntityId stripped = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 7.0f));
    (void)harness.GoTo(doomed, Vec3d(38.0f, 0.0f, 3.0f));
    (void)harness.GoTo(stripped, Vec3d(38.0f, 0.0f, 7.0f));
    for (int i = 0; i < 30; ++i)
        harness.Step();
    ASSERT_EQ(harness.Agents().RouteSlotsInUse(), 2u);
    ASSERT_FALSE(harness.AsksForNothing(stripped));

    harness.Entities().DestroyEntity(doomed);
    harness.Entities().RemoveComponent<NavAgent>(stripped);
    harness.Step();
    EXPECT_EQ(harness.Agents().RouteSlotsInUse(), 0u);
    EXPECT_EQ(harness.Agents().Queue().Size(), 0u);
    EXPECT_TRUE(harness.AsksForNothing(stripped)) << "nothing else would ever clear it";
}

TEST(NavAgentWalk, ADetachedZoneTakesItsAgentsRoutes)
{
    NavAgentHarness harness(LongHall());
    for (int i = 0; i < 4; ++i)
        (void)harness.GoTo(harness.SpawnAgent(Vec3d(2.0f, 0.0f, 2.0f + 2.0f * static_cast<float>(i))),
                           Vec3d(38.0f, 0.0f, 5.0f));
    for (int i = 0; i < 10; ++i)
        harness.Step();
    ASSERT_EQ(harness.Agents().RouteSlotsInUse(), 4u);
    harness.Detach();
    harness.Step();
    EXPECT_EQ(harness.Agents().RouteSlotsInUse(), 0u);
}

TEST(NavAgentWalk, ADormantZoneFreezesItsAgentsAndTheyCarryOn)
{
    NavAgentHarness harness(LongHall());
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    (void)harness.GoTo(agent, Vec3d(38.0f, 0.0f, 5.0f));
    for (int i = 0; i < 30; ++i)
        harness.Step();
    const Vec3d before = harness.Position(agent);
    const NavAgentProgress frozen = harness.Progress(agent);

    harness.SetParticipation(ZoneParticipation{});
    for (int i = 0; i < 120; ++i)
        harness.Step();
    EXPECT_EQ(harness.Position(agent), before);
    EXPECT_EQ(harness.Progress(agent).Phase, frozen.Phase);
    EXPECT_EQ(harness.Progress(agent).Repaths, frozen.Repaths);

    harness.SetParticipation(ZoneParticipation{ .Visible = true, .Physics = true, .Logic = true, .Audio = true });
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(agent); }, 60 * 20));
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived);
    EXPECT_EQ(harness.Progress(agent).Repaths, frozen.Repaths) << "the route was still good";
}

TEST(NavAgentWalk, TakingControlEndsTheOrder)
{
    NavAgentHarness harness(LongHall());
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    (void)harness.GoTo(agent, Vec3d(38.0f, 0.0f, 5.0f));
    for (int i = 0; i < 30; ++i)
        harness.Step();
    harness.Entities().TryGet<GameplayTagContainer>(agent)->Grant(harness.Tag("movement.controlled"));
    harness.Step();
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Failed);
    EXPECT_EQ(harness.Progress(agent).Failure, NavAgentFailure::TakenOverByInput);
    EXPECT_EQ(harness.Agents().RouteSlotsInUse(), 0u);
}

TEST(NavAgentWalk, ANamedProfileTheZoneLacksFails)
{
    NavAgentHarness harness(LongHall());
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    harness.Entities().TryGet<NavAgent>(agent)->Profile = "navigation.profile.giant";
    (void)harness.GoTo(agent, Vec3d(38.0f, 0.0f, 5.0f));
    harness.Step();
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Failed);
    EXPECT_EQ(harness.Progress(agent).Failure, NavAgentFailure::ProfileUnavailable);
}

TEST(NavAgentWalk, AnUnreachablePointFailsAsNoPath)
{
    NavAgentHarness harness(TwoIslands());
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    (void)harness.GoTo(agent, Vec3d(18.0f, 0.0f, 5.0f));
    harness.Step();
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Failed);
    EXPECT_EQ(harness.Progress(agent).Failure, NavAgentFailure::NoPath);
    EXPECT_EQ(harness.Progress(agent).LastQuery, NavStatus::NoPath);
    EXPECT_TRUE(harness.AsksForNothing(agent));
}

// H: many agents at once, each on its own route, in parallel or not.
class NavAgentCrowd : public ::testing::TestWithParam<std::uint32_t>
{
};

TEST_P(NavAgentCrowd, ManyAgentsPlanInBudgetAndAllArrive)
{
    // Pillars stand between the agents' grid points, so every start is clear.
    NavTestGeometry floor;
    floor.AddFloor(0.0f, 0.0f, 48.0f, 48.0f);
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 8; ++k)
        {
            const float x = 3.4f + 5.6f * static_cast<float>(i);
            const float z = 3.4f + 5.6f * static_cast<float>(k);
            floor.AddBox(Vec3d(x - 0.3f, 0.0f, z - 0.3f), Vec3d(x + 0.3f, 3.0f, z + 0.3f));
        }
    NavAgentHarness harness(floor, {}, GetParam());

    constexpr int kAgents = 256;
    std::vector<EntityId> agents;
    for (int i = 0; i < kAgents; ++i)
    {
        const float row = static_cast<float>(i / 16);
        const float column = static_cast<float>(i % 16);
        agents.push_back(harness.SpawnAgent(Vec3d(2.0f + column * 2.8f, 0.0f, 2.0f + row * 2.8f)));
    }
    for (int i = 0; i < kAgents; ++i)
    {
        const float row = static_cast<float>((i * 7) % 16);
        const float column = static_cast<float>((i * 11) % 16);
        (void)harness.GoTo(agents[static_cast<std::size_t>(i)],
                           Vec3d(46.0f - column * 2.8f, 0.0f, 46.0f - row * 2.8f));
    }

    int ticks = 0;
    std::size_t mostPlanned = 0;
    std::size_t firstTickAllPlanned = 0;
    const bool done = harness.StepUntil([&] {
        ++ticks;
        mostPlanned = std::max(mostPlanned, harness.Agents().LastStats().Planned);
        if (firstTickAllPlanned == 0 && harness.Agents().Queue().Size() == 0)
            firstTickAllPlanned = static_cast<std::size_t>(ticks);
        for (const EntityId agent : agents)
            if (!harness.HasEnded(agent))
                return false;
        return true;
    }, 60 * 60);

    ASSERT_TRUE(done);
    EXPECT_LE(mostPlanned, 16u);
    EXPECT_LE(firstTickAllPlanned, static_cast<std::size_t>(kAgents / 16));
    std::size_t arrived = 0;
    for (const EntityId agent : agents)
        arrived += harness.Progress(agent).Phase == NavAgentPhase::Arrived ? 1 : 0;
    // Characters pass through one another (no character-versus-character
    // collision yet), so every agent's route is its own to finish.
    EXPECT_EQ(arrived, static_cast<std::size_t>(kAgents));
    EXPECT_EQ(harness.Agents().RouteSlotsInUse(), 0u);
}

INSTANTIATE_TEST_SUITE_P(Workers, NavAgentCrowd, ::testing::Values(0u, 4u));
