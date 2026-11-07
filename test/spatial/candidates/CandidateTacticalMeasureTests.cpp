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
