// The first ticket's acceptance scenarios, each over real cooked navigation
// and a real physics scene.

#include "CandidateHarness.h"

#include <spatial/sight/SightTest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace
{
    const ZoneId kRoom{ 1 };
    const ZoneId kNextRoom{ 2 };
    const Vec3d kUp(0.0f, 1.0f, 0.0f);

    std::uint32_t CriterionIndex(const CandidateTrace& trace, std::string_view measure)
    {
        const auto criteria = trace.Criteria();
        for (std::uint32_t index = 0; index < criteria.size(); ++index)
        {
            if (criteria[index].Measure == measure)
                return index;
        }
        ADD_FAILURE() << "no criterion " << measure;
        return 0;
    }

    const CandidateTraceRow* RowNear(const CandidateTrace& trace, float x, float z, float tolerance = 0.3f)
    {
        for (const CandidateTraceRow& row : trace.Rows())
        {
            if (std::abs(row.Position.X - x) < tolerance && std::abs(row.Position.Z - z) < tolerance)
                return &row;
        }
        return nullptr;
    }

    bool SeenFrom(const PhysicsWorld& physics, Vec3d eye, Vec3d target)
    {
        const PhysicsQueries queries(physics);
        return TestSight(queries, SightObserver{ .Eye = eye }, target, {}, {}).Seen();
    }

    NavTestGeometry Floor(float size)
    {
        NavTestGeometry geometry;
        geometry.AddFloor(0.0f, 0.0f, size, size);
        return geometry;
    }
}

// Scenario 1.
TEST(CandidateScenario, CheapestPickupPrefersTheShorterWalkAndRejectsTheNextRoom)
{
    CandidateHarness harness;
    NavTestGeometry room = Floor(20.0f);
    room.AddBox(Vec3d(4.0f, 0.0f, 0.0f), Vec3d(5.0f, 3.0f, 18.0f));
    const RuntimeZoneRecord& here = harness.AttachZone(kRoom, room);
    const RuntimeZoneRecord& next = harness.AttachBareZone(kNextRoom);

    const EntityId behindWall = harness.AddTagged(Vec3d(7, 0, 10), { "pickup.health" }, here.Partition);
    const EntityId shortWalk = harness.AddTagged(Vec3d(2, 0, 2), { "pickup.health" }, here.Partition);
    (void)harness.AddTagged(Vec3d(2, 0, 19), { "pickup.health" }, here.Partition);
    const EntityId nextRoom = harness.AddTagged(Vec3d(30, 0, 10), { "pickup.health" }, next.Partition);

    const auto evaluation = [&](std::string_view outsideZone) {
        return harness.Compile(std::format(R"({{
            "generators": [{{ "generator": "candidates.generator.entities",
                              "arguments": {{ "tags": {{ "all": ["pickup.health"] }}, "all_resident": true }} }}],
            "criteria": [{{ "measure": "candidates.measure.travel_cost",
                            "arguments": {{ "fidelity": "exact", "outside_zone": "{}" }},
                            "score": {{ "curve": {{ "shape": "linear", "direction": "falling", "low": 0, "high": 60 }} }} }}],
            "selection": {{ "mode": "all_qualified" }}
        }})", outsideZone));
    };
    const CandidateContext context = QuerierAt(harness, kRoom, Vec3d(2, 0, 10));

    CandidateTrace trace(CandidateTraceLevel::Full, 16);
    const CandidateRunResult rejecting = harness.Run(evaluation("reject"), context, &trace);
    ASSERT_EQ(rejecting.Status, CandidateRunStatus::Success);
    ASSERT_EQ(harness.Returned().size(), 3u);
    EXPECT_EQ(harness.Returned()[0].Entity, shortWalk);
    EXPECT_EQ(harness.Returned()[2].Entity, behindWall);
    const auto outside = std::ranges::find(trace.Rows(), nextRoom, &CandidateTraceRow::Entity);
    ASSERT_NE(outside, trace.Rows().end());
    EXPECT_EQ(outside->Outcome, CandidateTraceOutcome::Rejected);
    EXPECT_EQ(outside->RejectStatus, CandidateMeasureStatus::OutsideZone);
    EXPECT_FALSE(rejecting.Flags.UsedEstimates);

    const CandidateRunResult estimating = harness.Run(evaluation("estimate"), context);
    ASSERT_EQ(estimating.Status, CandidateRunStatus::Success);
    EXPECT_EQ(harness.Returned().size(), 4u);
    EXPECT_TRUE(estimating.Flags.UsedEstimates);
    EXPECT_EQ(harness.Returned()[0].Entity, shortWalk);
}

// Scenario 2.
TEST(CandidateScenario, FiringPositionsNeedSightAndThePillarIsNamed)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(30.0f));
    const EntityId pillar{ 77, 1 };
    harness.AddStaticBox(Vec3d(14, 0, 8), Vec3d(16, 3, 10), pillar);

    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "target" }],
        "generators": [{ "generator": "candidates.generator.ring",
                         "arguments": { "center": "target", "points_per_ring": 16, "outer_radius": 10 } }],
        "criteria": [
            { "measure": "candidates.measure.reachable", "arguments": { "max_cost": 100 }, "require": { "expect": true } },
            { "measure": "candidates.measure.visibility", "arguments": { "slot": "target", "observer": "candidate" },
              "require": { "expect": true } },
            { "measure": "candidates.measure.distance", "arguments": { "slot": "target" },
              "score": { "curve": { "shape": "band", "low": 6, "preferred_low": 8, "preferred_high": 12, "high": 14 } } },
            { "measure": "candidates.measure.height", "arguments": { "slot": "target" },
              "score": { "curve": { "shape": "linear", "low": -1, "high": 1 } } }
        ],
        "selection": { "mode": "top_n", "count": 3 }
    })");
    const std::array target{ CandidatePoint{ .Position = Vec3d(15, 0, 15) } };
    const std::array slots{ CandidateSlotBinding{ *evaluation.FindSlot("target"), target } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(15, 0, 28));
    context.Slots = slots;

    CandidateTrace trace(CandidateTraceLevel::Full, 32);
    const CandidateRunResult result = harness.Run(evaluation, context, &trace);

    ASSERT_EQ(result.Status, CandidateRunStatus::Success);
    ASSERT_EQ(harness.Returned().size(), 3u);
    for (const CandidateResultEntry& entry : harness.Returned())
        EXPECT_TRUE(SeenFrom(harness.Physics, entry.Position + kUp * 1.6f, Vec3d(15, 1, 15)));

    const std::uint32_t visibility = CriterionIndex(trace, "candidates.measure.visibility");
    const CandidateTraceRow* shadowed = RowNear(trace, 15.0f, 5.0f);
    ASSERT_NE(shadowed, nullptr);
    EXPECT_EQ(shadowed->Outcome, CandidateTraceOutcome::Rejected);
    EXPECT_EQ(shadowed->RejectedBy, visibility);
    const CandidateTraceValue& detail = trace.Value(shadowed->Order, visibility);
    ASSERT_TRUE(detail.HasDetail);
    EXPECT_EQ(detail.Detail.Entity, pillar);
    EXPECT_EQ(detail.Detail.Code, static_cast<std::uint32_t>(SightOutcome::Blocked));
}

// Scenario 3.
TEST(CandidateScenario, RetreatLeavesEveryThreatBlind)
{
    CandidateHarness harness;
    NavTestGeometry room = Floor(30.0f);
    room.AddBox(Vec3d(0, 0, 20), Vec3d(25, 3, 21));
    harness.AttachZone(kRoom, room);
    harness.AddStaticBox(Vec3d(0, 0, 20), Vec3d(25, 3, 21));

    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "threats" }],
        "generators": [{ "generator": "candidates.generator.reachable",
                         "arguments": { "radius": 24, "max_cost": 80, "spacing": 3 } }],
        "criteria": [
            { "measure": "candidates.measure.distance", "arguments": { "slot": "threats" }, "require": { "min": 15 } },
            { "measure": "candidates.measure.visibility", "arguments": { "slot": "threats", "reduce": "fraction" },
              "score": { "curve": { "shape": "linear", "direction": "falling", "low": 0, "high": 1 } } }
        ],
        "limits": { "raycasts": 512 }
    })");
    const std::array threats{ CandidatePoint{ .Position = Vec3d(5, 0, 10) }, CandidatePoint{ .Position = Vec3d(12, 0, 10) } };
    const std::array slots{ CandidateSlotBinding{ 1, threats } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(5, 0, 15));
    context.Slots = slots;

    ASSERT_EQ(harness.Run(evaluation, context).Status, CandidateRunStatus::Success);
    ASSERT_EQ(harness.Returned().size(), 1u);
    const Vec3d spot = harness.Returned()[0].Position;
    EXPECT_FLOAT_EQ(harness.Returned()[0].Score, 1.0f);
    for (const CandidatePoint& threat : threats)
    {
        EXPECT_GE((spot - threat.Position).Magnitude(), 15.0f);
        EXPECT_FALSE(SeenFrom(harness.Physics, threat.Position + kUp * 1.6f, spot + kUp));
    }
}

// Scenario 4.
TEST(CandidateScenario, FlankPositionsLieBehindTheTarget)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(30.0f));
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "target" }],
        "generators": [{ "generator": "candidates.generator.ring",
                         "arguments": { "center": "target", "points_per_ring": 16, "outer_radius": 6 } }],
        "criteria": [{ "measure": "candidates.measure.facing", "arguments": { "slot": "target" }, "require": { "min": 120 } }],
        "selection": { "mode": "all_qualified" }
    })");
    const Vec3d forward(0, 0, 1);
    const std::array target{ CandidatePoint{ .Position = Vec3d(15, 0, 15), .Forward = forward } };
    const std::array slots{ CandidateSlotBinding{ 1, target } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(15, 0, 2));
    context.Slots = slots;

    ASSERT_EQ(harness.Run(evaluation, context).Status, CandidateRunStatus::Success);
    ASSERT_FALSE(harness.Returned().empty());
    for (const CandidateResultEntry& entry : harness.Returned())
        EXPECT_LT((entry.Position - target[0].Position).Dot(forward), -0.5f * 6.0f + 0.1f);
}

// Scenario 5.
TEST(CandidateScenario, NoPositionInsideTheBlast)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(30.0f));
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "hazard" }],
        "generators": [{ "generator": "candidates.generator.grid", "arguments": { "radius": 6, "spacing": 1 } }],
        "criteria": [{ "measure": "candidates.measure.distance", "arguments": { "slot": "hazard" }, "require": { "min": 4 } }],
        "selection": { "mode": "all_qualified" }
    })");
    const std::array hazard{ CandidatePoint{ .Position = Vec3d(17, 0, 15) } };
    const std::array slots{ CandidateSlotBinding{ 1, hazard } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(15, 0, 15));
    context.Slots = slots;
    CandidateResultBuffer results(256);

    ASSERT_EQ(harness.Evaluator.Evaluate(evaluation, context, harness.Scratch, results).Status,
              CandidateRunStatus::Success);
    ASSERT_FALSE(results.Entries().empty());
    for (const CandidateResultEntry& entry : results.Entries())
        EXPECT_GE((entry.Position - hazard[0].Position).Magnitude(), 4.0f);
}

// Scenario 6.
TEST(CandidateScenario, OutOfSightPlacementSpendsRaysOnlyInsideConeAndRange)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(40.0f));
    harness.AddStaticBox(Vec3d(20, 0, 12), Vec3d(21, 3, 18));

    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "observer" }],
        "generators": [{ "generator": "candidates.generator.grid",
                         "arguments": { "center": "observer", "radius": 12, "spacing": 2, "project": false } }],
        "criteria": [{ "measure": "candidates.measure.visibility",
                       "arguments": { "slot": "observer", "half_angle_degrees": 45, "range": 30 },
                       "require": { "max": 0 } }],
        "selection": { "mode": "all_qualified" }
    })");
    const SightObserver sight{ .Eye = Vec3d(15, 1.6f, 15), .Forward = Vec3d(1, 0, 0),
                               .HalfAngle = std::numbers::pi_v<float> / 4.0f, .Range = 30.0f };
    const std::array observer{ CandidatePoint{ .Position = Vec3d(15, 0, 15), .Forward = Vec3d(1, 0, 0) } };
    const std::array slots{ CandidateSlotBinding{ 1, observer } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(15, 0, 15));
    context.Slots = slots;
    CandidateResultBuffer results(256);
    CandidateTrace trace(CandidateTraceLevel::Summary, 0);

    ASSERT_EQ(harness.Evaluator.Evaluate(evaluation, context, harness.Scratch, results, &trace).Status,
              CandidateRunStatus::Success);

    std::uint32_t insideConeAndRange = 0;
    for (int z = -6; z <= 6; ++z)
    {
        for (int x = -6; x <= 6; ++x)
        {
            const Vec3d point(15.0f + 2.0f * x, 0.0f, 15.0f + 2.0f * z);
            if ((point - observer[0].Position).Magnitude() <= 12.0f
                && TestSightGeometry(sight, point + kUp) == SightOutcome::Seen)
                ++insideConeAndRange;
        }
    }
    EXPECT_EQ(trace.Criteria()[0].Raycasts, insideConeAndRange);

    const PhysicsQueries queries(harness.Physics);
    for (const CandidateResultEntry& entry : results.Entries())
        EXPECT_FALSE(TestSight(queries, sight, entry.Position + kUp, {}, {}).Seen());
}

namespace
{
    // A 30 x 20 m room split by a partition along z = 10 from x = 4 to 24;
    // both ends are open.
    NavTestGeometry SplitRoom(bool partition)
    {
        NavTestGeometry room;
        room.AddFloor(0.0f, 0.0f, 30.0f, 20.0f);
        if (partition)
            room.AddBox(Vec3d(4, 0, 9.5f), Vec3d(24, 3, 10.5f));
        return room;
    }

    constexpr std::string_view kUnseenApproach = R"({
        "slots": [{ "name": "observer" }, { "name": "spots" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "spots" } }],
        "criteria": [{ "measure": "candidates.measure.route_visibility",
                       "arguments": { "slot": "observer", "half_angle_degrees": 40, "range": 40 },
                       "require": { "max": 0 } }],
        "selection": { "mode": "all_qualified" },
        "limits": { "raycasts": 1024 }
    })";
}

// Scenario 7.
TEST(CandidateScenario, UnseenApproachTakesTheRouteAroundThePartition)
{
    const std::array observer{ CandidatePoint{ .Position = Vec3d(22, 0, 15), .Forward = Vec3d(-1, 0, 0) } };
    const std::array spots{ CandidatePoint{ .Position = Vec3d(10, 0, 15) }, CandidatePoint{ .Position = Vec3d(14, 0, 17) },
                            CandidatePoint{ .Position = Vec3d(27, 0, 15) }, CandidatePoint{ .Position = Vec3d(28, 0, 18) } };
    const std::array slots{ CandidateSlotBinding{ 1, observer }, CandidateSlotBinding{ 2, spots } };

    CandidateHarness harness;
    harness.AttachZone(kRoom, SplitRoom(true));
    harness.AddStaticBox(Vec3d(4, 0, 9.5f), Vec3d(24, 3, 10.5f));
    const CandidateEvaluation evaluation = harness.Compile(kUnseenApproach);
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(8, 0, 5));
    context.Slots = slots;

    CandidateTrace trace(CandidateTraceLevel::Full, 8);
    ASSERT_EQ(harness.Run(evaluation, context, &trace).Status, CandidateRunStatus::Success);
    ASSERT_EQ(harness.Returned().size(), 2u);
    for (const CandidateResultEntry& entry : harness.Returned())
        EXPECT_GT(entry.Position.X, 24.0f);
    for (const CandidateTraceRow& row : trace.Rows())
    {
        if (row.Position.X < 20.0f)
        {
            EXPECT_EQ(row.Outcome, CandidateTraceOutcome::Rejected);
        }
    }

    NavQueryContext navigation;
    NavRouteBuffer route;
    const ZoneNavigation& zone = *FindZoneNavigation(harness.Runtime, kRoom);
    const NavQueryRequest request = harness.Request(kRoom);
    const NavLocation start = NavProjectPoint(zone, navigation, request, Vec3d(8, 0, 5), Vec3d(1, 2, 1)).Location;
    ASSERT_EQ(NavFindRoute(zone, navigation, request, start, harness.Returned()[0].Nav, route), NavStatus::Success);
    const bool aroundTheEastEnd = std::ranges::any_of(route.Corners(), [](const Vec3d& corner) {
        return corner.X > 23.5f && corner.Z < 11.0f;
    });
    EXPECT_TRUE(aroundTheEastEnd);

    CandidateHarness open;
    open.AttachZone(kRoom, SplitRoom(false));
    const CandidateEvaluation openEvaluation = open.Compile(kUnseenApproach);
    CandidateContext openContext = QuerierAt(open, kRoom, Vec3d(8, 0, 5));
    openContext.Slots = slots;
    CandidateTrace openTrace(CandidateTraceLevel::Full, 8);
    ASSERT_EQ(open.Run(openEvaluation, openContext, &openTrace).Status, CandidateRunStatus::NoneQualified);
    for (const CandidateTraceRow& row : openTrace.Rows())
    {
        EXPECT_EQ(row.Outcome, CandidateTraceOutcome::Rejected);
        EXPECT_EQ(row.RejectedBy, 0);
    }
}

// Scenario 8.
TEST(CandidateScenario, AvoidedLinksAndCostOverridesShapeCandidateCosts)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, TwoIslands(),
                       { NavZoneFixture::Link(0xA, "navigation.traversal.jump", Vec3d(7, 0, 5), Vec3d(13, 0, 5)) });
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "spots" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "spots" } }],
        "criteria": [{ "measure": "candidates.measure.travel_cost", "arguments": { "fidelity": "exact" },
                       "require": { "min": 0 } }]
    })");
    const std::array spots{ CandidatePoint{ .Position = Vec3d(18, 0, 5) } };
    const std::array slots{ CandidateSlotBinding{ 1, spots } };
    const std::array jump{ harness.Tags.FindTag("navigation.traversal.jump") };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(2, 0, 5));
    context.Slots = slots;
    context.Navigation.Capabilities = jump;
    CandidateTrace trace(CandidateTraceLevel::Full, 4);

    const auto costWith = [&](float overrideCost) {
        const std::array cost{ NavLinkCostOverride{ NavLinkId{ 0xA }, overrideCost } };
        CandidateContext overridden = context;
        overridden.Navigation.LinkCostOverrides = cost;
        EXPECT_EQ(harness.Run(evaluation, overridden, &trace).Status, CandidateRunStatus::Success);
        return trace.Value(0, 0).Value;
    };
    EXPECT_NEAR(costWith(100.0f) - costWith(0.0f), 100.0f, 0.01f);

    const std::array avoid{ NavLinkId{ 0xA } };
    CandidateContext avoiding = context;
    avoiding.Navigation.AvoidLinks = avoid;
    EXPECT_EQ(harness.Run(evaluation, avoiding, &trace).Status, CandidateRunStatus::NoneQualified);
    EXPECT_EQ(trace.Value(0, 0).Status, CandidateMeasureStatus::Unreachable);
}

// Scenario 11.
TEST(CandidateScenario, ExactSearchBudgetRunsOutInGenerationOrder)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(20.0f));
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "spots" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "spots" } }],
        "criteria": [{ "measure": "candidates.measure.travel_cost", "arguments": { "fidelity": "exact" },
                       "require": { "min": 0 } }],
        "limits": { "exact_nav_searches": 2 }
    })");
    std::array<CandidatePoint, 5> spots;
    for (std::size_t index = 0; index < spots.size(); ++index)
        spots[index].Position = Vec3d(3.0f + 3.0f * static_cast<float>(index), 0, 10);
    const std::array slots{ CandidateSlotBinding{ 1, spots } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(2, 0, 2));
    context.Slots = slots;
    CandidateTrace trace(CandidateTraceLevel::Full, 8);

    const CandidateRunResult result = harness.Run(evaluation, context, &trace);
    EXPECT_TRUE(result.Flags.BudgetExhausted);
    EXPECT_EQ(result.Qualified, 2u);
    for (std::uint32_t row = 0; row < 5; ++row)
    {
        EXPECT_EQ(trace.Value(row, 0).Status,
                  row < 2 ? CandidateMeasureStatus::Ok : CandidateMeasureStatus::BeyondBudget) << row;
    }
}

// Scenario 12.
TEST(CandidateScenario, AnAirborneQuerierIsReportedNotSilentlyEmpty)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(20.0f));
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "floor" }],
        "generators": [{ "generator": "candidates.generator.grid",
                         "arguments": { "center": "floor", "radius": 2, "spacing": 1 } }],
        "criteria": [{ "measure": "candidates.measure.travel_cost", "arguments": { "fidelity": "exact" },
                       "require": { "min": 0 } }]
    })");
    const std::array floor{ CandidatePoint{ .Position = Vec3d(10, 0, 10) } };
    const std::array slots{ CandidateSlotBinding{ 1, floor } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(10, 50, 10));
    context.Slots = slots;
    CandidateTrace trace(CandidateTraceLevel::Summary, 0);
    const CandidateRunResult result = harness.Run(evaluation, context, &trace);

    EXPECT_EQ(result.Status, CandidateRunStatus::NoneQualified);
    EXPECT_TRUE(result.Flags.QuerierOffNavigation);
    EXPECT_EQ(trace.Criteria()[0].Statuses[static_cast<std::size_t>(CandidateMeasureStatus::Failed)], result.Generated);
}

// Scenario 14.
TEST(CandidateScenario, AZoneWithoutNavigationStillAnswersGeometricAndTagQuestions)
{
    CandidateHarness harness;
    const RuntimeZoneRecord& bare = harness.AttachBareZone(kRoom);
    const EntityId near = harness.AddTagged(Vec3d(2, 0, 0), { "pickup.ammo" }, bare.Partition);
    (void)harness.AddTagged(Vec3d(9, 0, 0), { "pickup.ammo", "pickup.spent" }, bare.Partition);
    CandidateEvaluator groundOnly(harness.Runtime, harness.Catalogs);

    const CandidateEvaluation evaluation = harness.Compile(R"({
        "generators": [{ "generator": "candidates.generator.entities", "arguments": { "tags": { "all": ["pickup.ammo"] } } }],
        "criteria": [
            { "measure": "candidates.measure.entity_tags", "arguments": { "tags": { "none": ["pickup.spent"] } },
              "require": { "expect": true } },
            { "measure": "candidates.measure.distance", "arguments": { "slot": "querier" }, "require": { "max": 20 } },
            { "measure": "candidates.measure.travel_cost", "require": { "max": 50 }, "unmeasured": { "score": 0 } },
            { "measure": "candidates.measure.visibility", "arguments": { "slot": "querier" }, "require": { "expect": true },
              "unmeasured": { "score": 0 } }
        ]
    })");
    CandidateTrace trace(CandidateTraceLevel::Summary, 0);
    const CandidateRunResult result = groundOnly.Evaluate(evaluation, QuerierAt(harness, kRoom, Vec3d::Zero()),
                                                          harness.Scratch, harness.Results, &trace);

    ASSERT_EQ(result.Status, CandidateRunStatus::Success);
    EXPECT_TRUE(result.Flags.NavigationUnavailable);
    ASSERT_EQ(harness.Returned().size(), 1u);
    EXPECT_EQ(harness.Returned()[0].Entity, near);
    const auto notApplicable = static_cast<std::size_t>(CandidateMeasureStatus::NotApplicable);
    EXPECT_EQ(trace.Criteria()[CriterionIndex(trace, "candidates.measure.travel_cost")].Statuses[notApplicable], 1u);
    EXPECT_EQ(trace.Criteria()[CriterionIndex(trace, "candidates.measure.visibility")].Statuses[notApplicable], 1u);

    EXPECT_EQ(groundOnly.Evaluate(evaluation, QuerierAt(harness, ZoneId{ 99 }, Vec3d::Zero()), harness.Scratch,
                                  harness.Results).Status,
              CandidateRunStatus::QuerierZoneUnavailable);
}

// Scenario 15.
TEST(CandidateScenario, ADoorSensorDoesNotBlockSight)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(20.0f));
    harness.AddStaticBox(Vec3d(9.5f, 0, 0), Vec3d(10.5f, 3, 20), EntityId{ 5, 1 }, true);
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "target" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "querier" } }],
        "criteria": [{ "measure": "candidates.measure.visibility", "arguments": { "slot": "target", "observer": "candidate" },
                       "require": { "expect": true } }]
    })");
    const std::array target{ CandidatePoint{ .Position = Vec3d(16, 0, 10) } };
    const std::array slots{ CandidateSlotBinding{ 1, target } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(4, 0, 10));
    context.Slots = slots;

    EXPECT_EQ(harness.Run(evaluation, context).Status, CandidateRunStatus::Success);
}

// Freshness: entity-bearing points are read from WorldTransform, never from
// what the caller wrote beside them.
TEST(CandidateScenario, EntityPositionsComeFromTheirWorldTransform)
{
    CandidateHarness harness;
    const RuntimeZoneRecord& bare = harness.AttachBareZone(kRoom);
    const EntityId querier = harness.AddPlaced(Vec3d(10, 0, 0));
    const EntityId marker = harness.AddPlaced(Vec3d(10, 0, 4));
    (void)bare;

    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "marker" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "marker" } }],
        "criteria": [{ "measure": "candidates.measure.distance", "arguments": { "slot": "querier" }, "require": { "min": 0 } }]
    })");
    const std::array markers{ CandidatePoint{ .Position = Vec3d(100, 0, 100), .Entity = marker } };
    const std::array slots{ CandidateSlotBinding{ 1, markers } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(-50, 0, -50));
    context.Querier = querier;
    context.Slots = slots;
    CandidateTrace trace(CandidateTraceLevel::Full, 4);

    ASSERT_EQ(harness.Run(evaluation, context, &trace).Status, CandidateRunStatus::Success);
    EXPECT_EQ(harness.Returned()[0].Entity, marker);
    EXPECT_FLOAT_EQ(harness.Returned()[0].Position.Z, 4.0f);
    EXPECT_NEAR(trace.Value(0, 0).Value, 4.0f, 1e-4f);
}
