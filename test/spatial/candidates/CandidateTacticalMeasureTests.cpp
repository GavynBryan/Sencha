// The measures that read a place's tactical shape: how many ways lead out of
// it, how far walkable ground extends around it, and how much of a body there
// is exposed to a watcher.

#include "CandidateHarness.h"

#include <array>

namespace
{
    const ZoneId kRoom{ 1 };

    // A 20 m room with a 3 m wide, 10 m deep dead-end alcove off its east side.
    NavTestGeometry RoomWithAlcove()
    {
        NavTestGeometry geometry;
        geometry.AddFloor(0.0f, 0.0f, 20.0f, 20.0f);
        geometry.AddFloor(20.0f, 8.5f, 30.0f, 11.5f);
        return geometry;
    }

    struct Measured
    {
        CandidateMeasureStatus Status = CandidateMeasureStatus::Failed;
        float Value = 0.0f;
    };

    std::vector<Measured> MeasureAt(CandidateHarness& harness, std::string_view criterion,
                                    std::span<const CandidatePoint> spots, std::span<const CandidatePoint> threats = {})
    {
        const std::string json = std::format(R"({{
            "slots": [{{ "name": "spots" }}, {{ "name": "threats", "required": false }}],
            "generators": [{{ "generator": "candidates.generator.points", "arguments": {{ "slot": "spots" }} }}],
            "criteria": [{}],
            "selection": {{ "mode": "all_qualified" }},
            "limits": {{ "raycasts": 1024, "exact_nav_searches": 64 }}
        }})", criterion);
        const CandidateEvaluation evaluation = harness.Compile(json);
        std::vector<CandidateSlotBinding> slots{ CandidateSlotBinding{ 1, spots } };
        if (!threats.empty())
            slots.push_back(CandidateSlotBinding{ 2, threats });
        CandidateContext context = QuerierAt(harness, kRoom, Vec3d(1, 0, 1));
        context.Slots = slots;
        CandidateTrace trace(CandidateTraceLevel::Full, 16);
        (void)harness.Run(evaluation, context, &trace);
        std::vector<Measured> measured;
        for (std::uint32_t row = 0; row < trace.Rows().size(); ++row)
            measured.push_back({ trace.Value(row, 0).Status, trace.Value(row, 0).Value });
        return measured;
    }
}

TEST(CandidateTacticalMeasures, ADeadEndOpensOneWayAndOpenGroundOpensEveryWay)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, RoomWithAlcove());
    const std::array spots{ CandidatePoint{ .Position = Vec3d(29, 0, 10) },
                            CandidatePoint{ .Position = Vec3d(10, 0, 10) },
                            CandidatePoint{ .Position = Vec3d(10, 0, 1) } };
    const std::vector<Measured> escape = MeasureAt(
        harness, R"({ "measure": "candidates.measure.escape_routes", "arguments": { "distance": 6 }, "score": { "curve": { "shape": "linear", "low": 0, "high": 8 } } })",
        spots);
    ASSERT_EQ(escape.size(), 3u);
    for (const Measured& measured : escape)
        EXPECT_EQ(measured.Status, CandidateMeasureStatus::Ok);
    EXPECT_EQ(escape[0].Value, 1.0f);
    EXPECT_EQ(escape[1].Value, 8.0f);
    EXPECT_GE(escape[2].Value, 3.0f);
    EXPECT_LE(escape[2].Value, 5.0f);
}

TEST(CandidateTacticalMeasures, AnEscapeThatNeedsTooLongADetourDoesNotCount)
{
    CandidateHarness harness;
    NavTestGeometry geometry;
    geometry.AddFloor(0.0f, 0.0f, 20.0f, 3.0f);
    geometry.AddFloor(0.0f, 9.0f, 20.0f, 12.0f);
    geometry.AddFloor(17.0f, 3.0f, 20.0f, 9.0f);
    harness.AttachZone(kRoom, geometry);
    const std::array spots{ CandidatePoint{ .Position = Vec3d(2, 0, 1.5f) } };
    const auto escapeWith = [&](float detour) {
        const std::string criterion = std::format(
            R"({{ "measure": "candidates.measure.escape_routes", "arguments": {{ "distance": 9, "detour": {}, "directions": 4 }}, "score": {{ "curve": {{ "shape": "linear", "low": 0, "high": 4 }} }} }})",
            detour);
        return MeasureAt(harness, criterion, spots).front().Value;
    };
    EXPECT_EQ(escapeWith(1.5f), 1.0f);
    EXPECT_EQ(escapeWith(5.0f), 2.0f);
}

TEST(CandidateTacticalMeasures, EdgeDistanceIsHalfACorridorsWidthAtItsMiddle)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, RoomWithAlcove());
    const std::array spots{ CandidatePoint{ .Position = Vec3d(25, 0, 10) },
                            CandidatePoint{ .Position = Vec3d(10, 0, 10) } };
    const std::vector<Measured> edge = MeasureAt(
        harness, R"({ "measure": "candidates.measure.edge_distance", "arguments": { "max_radius": 4 }, "require": { "min": 0 } })",
        spots);
    ASSERT_EQ(edge.size(), 2u);
    EXPECT_NEAR(edge[0].Value, 1.5f - 0.3f, 0.35f);
    EXPECT_FLOAT_EQ(edge[1].Value, 4.0f);
}

TEST(CandidateTacticalMeasures, SampleHeightsTellLowCoverFromOpenGround)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, RoomWithAlcove());
    harness.AddStaticBox(Vec3d(8, 0, 9), Vec3d(8.5f, 1.1f, 11));
    harness.AddStaticBox(Vec3d(8, 0, 14), Vec3d(8.5f, 3.0f, 16));
    const std::array threats{ CandidatePoint{ .Position = Vec3d(2, 0, 10), .Forward = Vec3d(1, 0, 0) },
                              CandidatePoint{ .Position = Vec3d(2, 0, 15), .Forward = Vec3d(1, 0, 0) } };
    const std::array spots{ CandidatePoint{ .Position = Vec3d(9, 0, 10) },
                            CandidatePoint{ .Position = Vec3d(9, 0, 5) },
                            CandidatePoint{ .Position = Vec3d(9, 0, 15) } };
    const auto exposure = [&](std::span<const CandidatePoint> threat) {
        return MeasureAt(harness,
                         R"({ "measure": "candidates.measure.visibility", "arguments": { "slot": "threats", "reduce": "fraction", "eye_height": 1.6, "sample_heights": [0.4, 1.0, 1.6] }, "require": { "min": 0 } })",
                         spots, threat);
    };
    const std::vector<Measured> low = exposure(std::span(threats).first(1));
    ASSERT_EQ(low.size(), 3u);
    EXPECT_GT(low[0].Value, 0.0f);
    EXPECT_LT(low[0].Value, 1.0f);
    EXPECT_FLOAT_EQ(low[1].Value, 1.0f);

    const std::vector<Measured> tall = exposure(std::span(threats).last(1));
    EXPECT_FLOAT_EQ(tall[2].Value, 0.0f);
}

namespace
{
    // A 10 m hub with hallways leaving east, north and south.
    NavTestGeometry HubWithThreeHallways()
    {
        NavTestGeometry geometry;
        geometry.AddFloor(0.0f, 0.0f, 10.0f, 10.0f);
        geometry.AddFloor(10.0f, 4.0f, 30.0f, 6.0f);
        geometry.AddFloor(4.0f, 10.0f, 6.0f, 30.0f);
        geometry.AddFloor(4.0f, -20.0f, 6.0f, 0.0f);
        return geometry;
    }

    std::vector<Measured> CrowdingRows(CandidateHarness& harness, std::string_view arguments,
                                       std::span<const CandidatePoint> spots, std::span<const CandidatePoint> others,
                                       const Vec3d& origin)
    {
        const std::string json = std::format(R"({{
            "slots": [{{ "name": "spots" }}, {{ "name": "others", "required": false }}, {{ "name": "origin" }}],
            "generators": [{{ "generator": "candidates.generator.points", "arguments": {{ "slot": "spots" }} }}],
            "criteria": [{{ "measure": "candidates.measure.crowding", "arguments": {}, "require": {{ "min": 0 }} }}],
            "selection": {{ "mode": "all_qualified" }},
            "limits": {{ "exact_nav_searches": 64 }}
        }})", arguments);
        const CandidateEvaluation evaluation = harness.Compile(json);
        const std::array origins{ CandidatePoint{ .Position = origin } };
        const std::vector<CandidateSlotBinding> slots{ CandidateSlotBinding{ 1, spots }, CandidateSlotBinding{ 2, others },
                                                       CandidateSlotBinding{ 3, origins } };
        CandidateContext context = QuerierAt(harness, kRoom, origin);
        context.Slots = slots;
        CandidateTrace trace(CandidateTraceLevel::Full, 16);
        (void)harness.Run(evaluation, context, &trace);
        std::vector<Measured> rows;
        for (std::uint32_t row = 0; row < trace.Rows().size(); ++row)
            rows.push_back({ trace.Value(row, 0).Status, trace.Value(row, 0).Value });
        return rows;
    }

    std::vector<float> CrowdingAt(CandidateHarness& harness, std::string_view arguments,
                                  std::span<const CandidatePoint> spots, std::span<const CandidatePoint> others,
                                  const Vec3d& origin)
    {
        std::vector<float> counts;
        for (const Measured& row : CrowdingRows(harness, arguments, spots, others, origin))
        {
            EXPECT_EQ(row.Status, CandidateMeasureStatus::Ok);
            counts.push_back(row.Value);
        }
        return counts;
    }
}

TEST(CandidateTacticalMeasures, CrowdingCountsWhoIsAlreadyHeadedEachWay)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, HubWithThreeHallways());
    const std::array others{ CandidatePoint{ .Position = Vec3d(15, 0, 5) }, CandidatePoint{ .Position = Vec3d(20, 0, 5) },
                             CandidatePoint{ .Position = Vec3d(5, 0, 15) } };
    const std::array spots{ CandidatePoint{ .Position = Vec3d(25, 0, 5) }, CandidatePoint{ .Position = Vec3d(5, 0, 25) },
                            CandidatePoint{ .Position = Vec3d(5, 0, -15) } };
    const std::vector<float> counts =
        CrowdingAt(harness, R"({ "slot": "others", "origin": "origin", "angle": 30 })", spots, others, Vec3d(5, 0, 5));
    ASSERT_EQ(counts.size(), 3u);
    EXPECT_EQ(counts[0], 2.0f);
    EXPECT_EQ(counts[1], 1.0f);
    EXPECT_EQ(counts[2], 0.0f) << "the south hallway is the least crowded";

    const std::array insideHub{ CandidatePoint{ .Position = Vec3d(7, 0, 7) } };
    const std::array headedOut{ CandidatePoint{ .Position = Vec3d(6, 0, 6) }, CandidatePoint{ .Position = Vec3d(20, 0, 5) } };
    EXPECT_EQ(CrowdingRows(harness, R"({ "slot": "others", "origin": "origin", "radius": 6 })", insideHub, headedOut,
                           Vec3d(5, 0, 5))
                  .front()
                  .Status,
              CandidateMeasureStatus::NotApplicable)
        << "within the radius a candidate has set out in no direction";
    EXPECT_EQ(CrowdingAt(harness, R"({ "slot": "others", "origin": "origin", "radius": 6 })", spots, headedOut,
                         Vec3d(5, 0, 5))[0],
              1.0f)
        << "and an ally within it is not counted";

    const std::vector<float> near =
        CrowdingAt(harness, R"({ "slot": "others", "mode": "distance", "distance": 6 })", spots, others, Vec3d(5, 0, 5));
    EXPECT_EQ(near[0], 1.0f) << "within 6 m of (25, 5) is the one at (20, 5)";
    EXPECT_EQ(near[2], 0.0f);
}

TEST(CandidateTacticalMeasures, ByRouteABendingHallwayIsItsOwnWayOut)
{
    CandidateHarness harness;
    NavTestGeometry geometry;
    geometry.AddFloor(0.0f, 0.0f, 10.0f, 10.0f);
    geometry.AddFloor(4.0f, 10.0f, 6.0f, 30.0f);
    // East through a low door, then north up a parallel hallway.
    geometry.AddFloor(10.0f, 1.0f, 20.0f, 3.0f);
    geometry.AddFloor(18.0f, 3.0f, 20.0f, 30.0f);
    harness.AttachZone(kRoom, geometry);
    const std::array others{ CandidatePoint{ .Position = Vec3d(5, 0, 25) } };
    const std::array spots{ CandidatePoint{ .Position = Vec3d(19, 0, 25) } };
    const Vec3d origin(5, 0, 5);
    EXPECT_EQ(CrowdingAt(harness, R"({ "slot": "others", "origin": "origin", "angle": 40 })", spots, others, origin)[0],
              1.0f)
        << "in a straight line the two hallways look alike";
    EXPECT_EQ(CrowdingAt(harness, R"({ "slot": "others", "origin": "origin", "angle": 40, "bearing": "route", "radius": 6 })",
                         spots, others, origin)[0],
              0.0f)
        << "by route one leaves the hub north and the other east";
}
