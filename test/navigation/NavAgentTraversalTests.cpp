// Agents crossing navigation links through registered executors, over the
// real fixed tick.

#include "NavAgentHarness.h"

#include <navigation/NavLinkState.h>
#include <navigation/NavTraversal.h>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{
    constexpr NavLinkId kJumpLink{ 0x1 };



    // The fixture's islands, x 0-8 and 12-20, with a jump across the gap.
    std::vector<NavLinkRecord> JumpLink()
    {
        return { NavZoneFixture::Link(kJumpLink.Value, "navigation.traversal.jump", Vec3d(7.5f, 0.0f, 5.0f),
                                      Vec3d(12.5f, 0.0f, 5.0f)) };
    }

    // The islands joined by a walkable bridge along their far edge: longer than the jump.
    NavTestGeometry IslandsWithABridge()
    {
        NavTestGeometry g = TwoIslands();
        g.AddFloor(8.0f, 8.5f, 12.0f, 10.0f);
        return g;
    }

    NavTraversalResult AlwaysFails(NavTraversalFrame&) { return NavTraversalResult::Failed; }

    int gAborts = 0;
    NavTraversalResult NeverFinishes(NavTraversalFrame& frame)
    {
        frame.Intent.WishDir = Vec3d::Zero();
        return NavTraversalResult::Running;
    }
    void CountAbort(World&, EntityId, const NavRouteStep&) { ++gAborts; }
}

// E (1): reaching the entry is not completion; walking resumes after the crossing.
TEST(NavAgentTraversal, JumpsTheGapAndWalksOnOnlyAfterLanding)
{
    NavAgentHarness harness(TwoIslands(), JumpLink());
    ASSERT_TRUE(RegisterNavTraversal(harness.Entities(), harness.Tag("navigation.traversal.jump"), kNavJumpAcross));
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f), { "navigation.traversal.jump" });
    (void)harness.GoTo(agent, Vec3d(18.0f, 0.0f, 5.0f));

    std::vector<NavAgentPhase> phases;
    Vec3d crossingStart = Vec3d::Zero();
    bool walkedBeforeLanding = false;
    const bool ended = harness.StepUntil([&] {
        const NavAgentPhase phase = harness.Progress(agent).Phase;
        if (phases.empty() || phases.back() != phase)
        {
            if (phase == NavAgentPhase::Traversing)
                crossingStart = harness.Position(agent);
            phases.push_back(phase);
        }
        if (phase == NavAgentPhase::Moving && phases.size() >= 2 && phases[phases.size() - 2] == NavAgentPhase::Traversing
            && harness.Position(agent).X < 12.0f)
            walkedBeforeLanding = true;
        return harness.HasEnded(agent);
    }, 60 * 30);

    ASSERT_TRUE(ended);
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived)
        << "failure " << static_cast<int>(harness.Progress(agent).Failure);
    // An order is planned in the tick it is taken up, so Planning never shows between ticks.
    EXPECT_EQ(phases, (std::vector<NavAgentPhase>{ NavAgentPhase::Moving, NavAgentPhase::Traversing,
                                                   NavAgentPhase::Moving, NavAgentPhase::Arrived }));
    EXPECT_LE(NavPlanarDistance(crossingStart, Vec3d(7.5f, 0.0f, 5.0f)), 0.5f + 0.15f);
    EXPECT_FALSE(walkedBeforeLanding);
}

// E (2): a crossing the agent may not make, or nobody can carry out, is never planned.
TEST(NavAgentTraversal, AnUnhandledCrossingIsNeverPlanned)
{
    {
        NavAgentHarness harness(TwoIslands(), JumpLink());
        ASSERT_TRUE(RegisterNavTraversal(harness.Entities(), harness.Tag("navigation.traversal.jump"), kNavJumpAcross));
        const EntityId grounded = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f));
        (void)harness.GoTo(grounded, Vec3d(18.0f, 0.0f, 5.0f));
        harness.Step();
        EXPECT_EQ(harness.Progress(grounded).Failure, NavAgentFailure::NoPath) << "the agent lacks the tag";
    }
    {
        NavAgentHarness harness(TwoIslands(), JumpLink());
        const EntityId willing = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f), { "navigation.traversal.jump" });
        (void)harness.GoTo(willing, Vec3d(18.0f, 0.0f, 5.0f));
        harness.Step();
        EXPECT_EQ(harness.Progress(willing).Failure, NavAgentFailure::NoPath) << "no executor carries jumps";
        for (int i = 0; i < 120; ++i)
            harness.Step();
        EXPECT_LT(harness.Position(willing).X, 8.0f) << "it never walks off its island";
    }
}

// E (3): the only way across fails; the request says so and keeps the search's answer apart.
TEST(NavAgentTraversal, AFailedCrossingWithNoWayRoundEndsAsTraversalFailed)
{
    NavAgentHarness harness(TwoIslands(), JumpLink());
    ASSERT_TRUE(RegisterNavTraversal(harness.Entities(), harness.Tag("navigation.traversal.jump"),
                                     NavTraversalExecutor{ &AlwaysFails, nullptr }));
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f), { "navigation.traversal.jump" });
    (void)harness.GoTo(agent, Vec3d(18.0f, 0.0f, 5.0f));
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(agent); }, 60 * 20));
    EXPECT_EQ(harness.Progress(agent).Failure, NavAgentFailure::TraversalFailed);
    EXPECT_EQ(harness.Progress(agent).LastQuery, NavStatus::NoPath);
    EXPECT_EQ(harness.Progress(agent).LastRepath, NavRepathReason::TraversalFailed);
    EXPECT_LT(harness.Position(agent).X, 8.5f);
}

// E (4): the same failure with a way round replans around the link and arrives.
TEST(NavAgentTraversal, AFailedCrossingIsAvoidedAndTheAgentWalksRound)
{
    NavAgentHarness harness(IslandsWithABridge(), JumpLink());
    ASSERT_TRUE(RegisterNavTraversal(harness.Entities(), harness.Tag("navigation.traversal.jump"),
                                     NavTraversalExecutor{ &AlwaysFails, nullptr }));
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f), { "navigation.traversal.jump" });
    (void)harness.GoTo(agent, Vec3d(18.0f, 0.0f, 5.0f));
    // The executor fails within the tick the crossing starts, so the attempt shows
    // as the replan it causes rather than as a Traversing phase.
    bool replannedAroundTheJump = false;
    const bool ended = harness.StepUntil([&] {
        replannedAroundTheJump =
            replannedAroundTheJump || harness.Progress(agent).LastRepath == NavRepathReason::TraversalFailed;
        return harness.HasEnded(agent);
    }, 60 * 30);
    ASSERT_TRUE(ended);
    EXPECT_TRUE(replannedAroundTheJump) << "the jump was the first choice";
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived);
    EXPECT_GE(harness.Progress(agent).Repaths, 1u);
}

TEST(NavAgentTraversal, ReplacingTheOrderMidCrossingAbortsTheExecutor)
{
    gAborts = 0;
    NavAgentHarness harness(TwoIslands(), JumpLink());
    ASSERT_TRUE(RegisterNavTraversal(harness.Entities(), harness.Tag("navigation.traversal.jump"),
                                     NavTraversalExecutor{ &NeverFinishes, &CountAbort }));
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f), { "navigation.traversal.jump" });
    (void)harness.GoTo(agent, Vec3d(18.0f, 0.0f, 5.0f));
    ASSERT_TRUE(harness.StepUntil([&] { return harness.Progress(agent).Phase == NavAgentPhase::Traversing; }, 60 * 10));
    (void)harness.GoTo(agent, Vec3d(2.0f, 0.0f, 2.0f));
    harness.Step();
    EXPECT_EQ(gAborts, 1);
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(agent); }, 60 * 10));
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived);
    EXPECT_EQ(gAborts, 1);
}

TEST(NavAgentTraversal, ACrossingWhoseLinkIsDisabledIsAbortedAndCountsAsFailed)
{
    gAborts = 0;
    NavAgentHarness harness(TwoIslands(), JumpLink());
    ASSERT_TRUE(RegisterNavTraversal(harness.Entities(), harness.Tag("navigation.traversal.jump"),
                                     NavTraversalExecutor{ &NeverFinishes, &CountAbort }));
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 0.0f, 5.0f), { "navigation.traversal.jump" });
    (void)harness.GoTo(agent, Vec3d(18.0f, 0.0f, 5.0f));
    ASSERT_TRUE(harness.StepUntil([&] { return harness.Progress(agent).Phase == NavAgentPhase::Traversing; }, 60 * 10));

    const EntityId gate = harness.Entities().CreateEntity(harness.Partition);
    harness.Entities().AddComponent<NavLinkState>(gate, NavLinkState{ .Link = kJumpLink, .Enabled = false });
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(agent); }, 10));
    EXPECT_EQ(gAborts, 1);
    EXPECT_EQ(harness.Progress(agent).Failure, NavAgentFailure::TraversalFailed);
}

// A drop: walked off, fallen, and walking again on the floor below.
TEST(NavAgentTraversal, WalksOffALedgeThroughADropLink)
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 20.0f, 10.0f);
    g.AddBox(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(8.0f, 2.0f, 10.0f));
    NavAgentHarness harness(g, { NavZoneFixture::Link(0x2, "navigation.traversal.drop", Vec3d(7.5f, 2.0f, 5.0f),
                                                      Vec3d(9.5f, 0.0f, 5.0f)) });
    ASSERT_TRUE(RegisterNavTraversal(harness.Entities(), harness.Tag("navigation.traversal.drop"), kNavWalkAcross));
    const EntityId agent = harness.SpawnAgent(Vec3d(2.0f, 2.0f, 5.0f), { "navigation.traversal.drop" });
    (void)harness.GoTo(agent, Vec3d(18.0f, 0.0f, 5.0f));
    bool crossed = false;
    ASSERT_TRUE(harness.StepUntil([&] {
        crossed = crossed || harness.Progress(agent).Phase == NavAgentPhase::Traversing;
        return harness.HasEnded(agent);
    }, 60 * 20));
    EXPECT_TRUE(crossed);
    EXPECT_EQ(harness.Progress(agent).Phase, NavAgentPhase::Arrived)
        << "failure " << static_cast<int>(harness.Progress(agent).Failure);
    EXPECT_LT(harness.Position(agent).Y, 1.5f);
}
