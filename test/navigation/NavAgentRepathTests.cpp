// When an agent replans, and what it does while it waits: moving targets,
// changed geometry, deviation, and getting stuck.

#include "NavAgentHarness.h"

#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>
#include <navigation/NavigationGeometry.h>
#include <time/ActorTimeLeases.h>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{


    // A 20 x 12 m room split by a wall with a 2 m gap to the south and a 3 m gap
    // to the north.
    NavTestGeometry TwoGapRoom()
    {
        NavTestGeometry g;
        g.AddFloor(0.0f, 0.0f, 20.0f, 12.0f);
        g.AddBox(Vec3d(9.5f, 0.0f, 2.0f), Vec3d(10.5f, 3.0f, 9.0f));
        return g;
    }

    // A box that changes navigation and blocks bodies.
    EntityId AddObstacle(NavAgentHarness& harness, Vec3d min, Vec3d max, bool changesNavigation = true)
    {
        World& world = harness.Entities();
        const EntityId box = world.CreateEntity(harness.Partition);
        Transform3f pose = Transform3f::Identity();
        pose.Position = (min + max) * 0.5f;
        world.AddComponent<LocalTransform>(box, LocalTransform{ pose });
        world.AddComponent<WorldTransform>(box, WorldTransform{ pose });
        Collider collider;
        collider.Shape = CollisionShape::MakeBox((max - min) * 0.5f);
        world.AddComponent<Collider>(box, collider);
        world.AddComponent<RigidBody>(box, RigidBody{ .Motion = BodyMotion::Static });
        if (changesNavigation)
            world.AddComponent<NavigationGeometry>(box, NavigationGeometry{});
        return box;
    }

    struct Gap
    {
        Vec3d Min;
        Vec3d Max;
        float OtherZ;
    };

    // The gap the agent is heading for, judged from how it has moved, and a
    // point in the other one.
    Gap GapTaken(NavAgentHarness& harness, EntityId agent)
    {
        const bool north = harness.Position(agent).Z > 6.0f;
        return north ? Gap{ Vec3d(9.5f, 0.0f, 9.0f), Vec3d(10.5f, 3.0f, 12.0f), 1.0f }
                     : Gap{ Vec3d(9.5f, 0.0f, 0.0f), Vec3d(10.5f, 3.0f, 2.0f), 10.5f };
    }

}

// B: a moving target is followed with bounded, spaced refreshes.
TEST(NavAgentRepath, FollowsAMovingTargetWithoutReplanningEveryTick)
{
    NavAgentHarness harness(LongHall());
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    const EntityId target = harness.SpawnMarker(Vec3d(20.0f, 0.0f, 2.0f));
    (void)RequestNavigation(harness.Entities(), agent, NavDestination::AtEntity(target));

    float travelled = 0.0f;
    std::vector<std::uint64_t> repathTicks;
    std::uint16_t lastRepaths = 0;
    Vec3d lastWish = Vec3d::Zero();
    int reversals = 0;
    const bool ended = harness.StepUntil([&] {
        // The target walks east at 1.5 m/s for six seconds, then stops.
        if (harness.Tick < 360)
        {
            const float x = 20.0f + 1.5f * static_cast<float>(harness.Tick) / 60.0f;
            harness.MoveMarker(target, Vec3d(x, 0.0f, 2.0f + 6.0f * static_cast<float>(harness.Tick) / 360.0f));
            travelled = x - 20.0f;
        }
        const NavAgentProgress& progress = harness.Progress(agent);
        if (progress.Repaths != lastRepaths)
        {
            repathTicks.push_back(harness.Tick);
            lastRepaths = progress.Repaths;
        }
        const Vec3d wish = harness.Intent(agent);
        if (wish.X * lastWish.X + wish.Z * lastWish.Z < -0.25f)
            ++reversals;
        if (!harness.AsksForNothing(agent))
            lastWish = wish;
        return harness.HasEnded(agent);
    }, 60 * 30);

    ASSERT_TRUE(ended);
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived);
    EXPECT_LE(repathTicks.size(), static_cast<std::size_t>(travelled / 1.0f) + 1);
    for (std::size_t i = 1; i < repathTicks.size(); ++i)
        EXPECT_GE(repathTicks[i] - repathTicks[i - 1], 30u) << "refreshes closer than nav.agent.repath_interval";
    EXPECT_EQ(reversals, 0);
}

// C: a change to the route's own tiles replans around it, and with no way
// round the agent stops and says so.
TEST(NavAgentRepath, AChangedRouteReplansAroundTheObstacle)
{
    NavAgentHarness harness(TwoGapRoom());
    const EntityId agent = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 6.0f));
    (void)harness.GoTo(agent, Vec3d(17.0f, 0.0f, 6.0f));
    for (int i = 0; i < 20; ++i)
        harness.Step();
    ASSERT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Moving);
    ASSERT_GT(std::abs(harness.Position(agent).Z - 6.0f), 0.3f) << "heading for one of the gaps";

    const Gap taken = GapTaken(harness, agent);
    (void)AddObstacle(harness, taken.Min, taken.Max);
    ASSERT_TRUE(harness.StepUntil([&] { return harness.Progress(agent).LastRepath == NavRepathReason::TileChanged; },
                                  4));
    bool passedTheOther = false;
    ASSERT_TRUE(harness.StepUntil([&] {
        const Vec3d p = harness.Position(agent);
        if (std::abs(p.X - 10.0f) < 0.6f && std::abs(p.Z - taken.OtherZ) < 1.5f)
            passedTheOther = true;
        return harness.HasEnded(agent);
    }, 60 * 30));
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived);
    EXPECT_TRUE(passedTheOther);
}

TEST(NavAgentRepath, WithEveryWayBlockedTheAgentStopsAndSaysNoPath)
{
    NavAgentHarness harness(TwoGapRoom());
    const EntityId agent = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 6.0f));
    (void)harness.GoTo(agent, Vec3d(17.0f, 0.0f, 6.0f));
    for (int i = 0; i < 20; ++i)
        harness.Step();
    (void)AddObstacle(harness, Vec3d(9.5f, 0.0f, 9.0f), Vec3d(10.5f, 3.0f, 12.0f));
    (void)AddObstacle(harness, Vec3d(9.5f, 0.0f, 0.0f), Vec3d(10.5f, 3.0f, 2.0f));
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(agent); }, 10));
    EXPECT_EQ(harness.Progress(agent).Failure, NavAgentFailure::NoPath);
    EXPECT_EQ(harness.Progress(agent).LastRepath, NavRepathReason::TileChanged);
    EXPECT_TRUE(harness.AsksForNothing(agent));
}

// I: an invalidated route is never walked while its replan waits.
TEST(NavAgentRepath, AnInvalidatedRouteIsNotFollowedWhileItsReplanWaits)
{
    NavAgentHarness harness(TwoGapRoom());
    // Agents that will crowd the queue, made first so they also come first in it.
    std::vector<EntityId> crowd;
    for (int i = 0; i < 8; ++i)
        crowd.push_back(harness.SpawnAgent(Vec3d(2.0f + static_cast<float>(i) * 0.9f, 0.0f, 1.0f)));
    const EntityId agent = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 6.0f));
    (void)harness.GoTo(agent, Vec3d(17.0f, 0.0f, 6.0f));
    for (int i = 0; i < 20; ++i)
        harness.Step();
    ASSERT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Moving);

    harness.SetCVar("nav.agent.plans_per_tick", 1.0);
    const Gap taken = GapTaken(harness, agent);
    (void)AddObstacle(harness, taken.Min, taken.Max);
    harness.Step();  // navigation rebuilds the tile in this tick's PostFixed
    for (const EntityId other : crowd)
        (void)harness.GoTo(other, Vec3d(5.0f, 0.0f, 10.0f));
    harness.Step();
    ASSERT_EQ(harness.Progress(agent).LastRepath, NavRepathReason::TileChanged);

    int waited = 0;
    while (harness.Progress(agent).Phase == NavAgentPhase::Planning && waited < 40)
    {
        EXPECT_TRUE(harness.AsksForNothing(agent)) << "tick " << waited << " of the wait";
        harness.Step();
        ++waited;
    }
    EXPECT_GE(waited, 7) << "the crowd ahead of it planned first, one a tick";
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Moving);
}

// J: a refresh keeps walking its valid route and is not starved by blocking work.
TEST(NavAgentRepath, ARefreshWalksOnAndIsNotStarved)
{
    NavAgentHarness harness(LongHall());
    std::vector<EntityId> crowd;
    for (int i = 0; i < 6; ++i)
        crowd.push_back(harness.SpawnAgent(Vec3d(30.0f + static_cast<float>(i), 0.0f, 9.0f)));
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    const EntityId target = harness.SpawnMarker(Vec3d(30.0f, 0.0f, 5.0f));
    (void)RequestNavigation(harness.Entities(), agent, NavDestination::AtEntity(target));
    for (int i = 0; i < 10; ++i)
        harness.Step();
    ASSERT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Moving);
    harness.SetCVar("nav.agent.plans_per_tick", 1.0);
    for (int i = 0; i < 30; ++i)
        harness.Step();

    harness.MoveMarker(target, Vec3d(36.0f, 0.0f, 5.0f));
    const Vec3d planned = harness.Progress(agent).Destination;
    std::uint64_t refreshQueuedAt = 0;
    int ticks = 0;
    bool walkedWhileWaiting = true;
    const bool refreshed = harness.StepUntil([&] {
        // Every tick, the whole crowd is re-ordered: fresh blocking work at the back.
        for (const EntityId other : crowd)
            (void)harness.GoTo(other, Vec3d(30.0f, 0.0f, (ticks % 2 == 0) ? 1.0f : 9.0f));
        ++ticks;
        const NavAgentProgress& progress = harness.Progress(agent);
        if (refreshQueuedAt == 0 && progress.LastRepath == NavRepathReason::TargetMoved)
            refreshQueuedAt = harness.Tick;
        const bool committed = !(progress.Destination == planned);
        if (refreshQueuedAt != 0 && !committed && harness.AsksForNothing(agent))
            walkedWhileWaiting = false;
        return committed;
    }, 120);
    ASSERT_TRUE(refreshed) << "a refresh must not wait forever behind blocking work";
    EXPECT_TRUE(walkedWhileWaiting);
    // Its place is fixed when queued: eight ticks of deferral, then only the
    // blocking work already ahead of it, one plan a tick.
    EXPECT_LE(harness.Tick - refreshQueuedAt, 8u + crowd.size() + 2u);
}

// K: a teleport past the corner, away from the route, is a deviation.
TEST(NavAgentRepath, ATeleportAwayFromTheRouteIsADeviationNotProgress)
{
    NavAgentHarness harness(WallRoom());
    const EntityId agent = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    (void)harness.GoTo(agent, Vec3d(17.0f, 0.0f, 3.0f));
    for (int i = 0; i < 20; ++i)
        harness.Step();
    ASSERT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Moving);

    // Over the line to the gap corner, but 4 m off any leg of the route.
    const Vec3d landing(6.0f, NavAgentHarness::kCapsuleCentre, 1.0f);
    CharacterMoverPool& movers = harness.Schedule.Get<PhysicsStepSystem>()->GetCharacterMovers();
    ASSERT_TRUE(movers.SetPosition(harness.Entities(), agent, landing));
    harness.Entities().TryGet<WorldTransform>(agent)->Value.Position = landing;
    harness.Step();
    EXPECT_EQ(harness.Progress(agent).LastRepath, NavRepathReason::Deviated);
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(agent); }, 60 * 30));
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived);
}

// A body navigation does not know about stops the agent; it replans, then gives up.
TEST(NavAgentRepath, AnAgentThatCannotProgressFailsAsStuck)
{
    NavAgentHarness harness(LongHall());
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    (void)AddObstacle(harness, Vec3d(6.0f, 0.0f, 0.0f), Vec3d(7.0f, 3.0f, 10.0f), false);
    (void)harness.GoTo(agent, Vec3d(38.0f, 0.0f, 5.0f));
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(agent); }, 60 * 20));
    EXPECT_EQ(harness.Progress(agent).Failure, NavAgentFailure::Stuck);
    EXPECT_EQ(harness.Progress(agent).LastRepath, NavRepathReason::Stuck);
    EXPECT_LE(harness.Progress(agent).Repaths, 3u) << "bounded by nav.agent.stuck_repaths";
}

TEST(NavAgentRepath, ASlowedAgentIsNotStuck)
{
    NavAgentHarness harness(LongHall());
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
    World& world = harness.Entities();
    world.AddComponent<ActorTimeLeases>(agent, ActorTimeLeases{});
    ASSERT_TRUE(AcquireActorTimeLease(*world.TryGet<ActorTimeLeases>(agent), LeaseOwner{ .Entity = agent },
                                      kActorTimeOne / 2, 0));
    (void)harness.GoTo(agent, Vec3d(20.0f, 0.0f, 5.0f));
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(agent); }, 60 * 30));
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived);
    EXPECT_EQ(harness.Progress(agent).Repaths, 0u);
}
