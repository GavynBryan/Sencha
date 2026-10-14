// An entity's authored SightSensor and SightTarget are its eye and sample
// point in candidate evaluation too; the measure's arguments only describe
// eyes with no sensor behind them.

#include "CandidateHarness.h"

#include <array>

namespace
{
    const ZoneId kRoom{ 1 };

    NavTestGeometry Floor(float size)
    {
        NavTestGeometry geometry;
        geometry.AddFloor(0.0f, 0.0f, size, size);
        return geometry;
    }

    constexpr std::string_view kSpotsTheWatcherSees = R"({
        "slots": [{ "name": "watcher" }, { "name": "spots" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "spots" } }],
        "criteria": [{ "measure": "candidates.measure.visibility",
                       "arguments": { "slot": "watcher", "range": 30 },
                       "require": { "min": 1 } }],
        "selection": { "mode": "all_qualified" }
    })";

    std::size_t SpotsSeen(CandidateHarness& harness, EntityId watcher)
    {
        const CandidateEvaluation evaluation = harness.Compile(kSpotsTheWatcherSees);
        const std::array watcherPoint{ CandidatePoint{ .Entity = watcher } };
        const std::array spots{ CandidatePoint{ .Position = Vec3d(5, 0, 10) } };
        const std::array slots{ CandidateSlotBinding{ 1, watcherPoint }, CandidateSlotBinding{ 2, spots } };
        CandidateContext context = QuerierAt(harness, kRoom, Vec3d(5, 0, 20));
        context.Slots = slots;
        const CandidateRunStatus status = harness.Run(evaluation, context).Status;
        EXPECT_TRUE(status == CandidateRunStatus::Success || status == CandidateRunStatus::NoneQualified);
        return status == CandidateRunStatus::Success ? harness.Returned().size() : 0u;
    }
}

TEST(CandidateSightComponents, TheArgumentsDescribeAnEyeWithNoSensor)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(40.0f));
    const EntityId watcher = harness.AddPlaced(Vec3d(5, 0, 20));
    EXPECT_EQ(SpotsSeen(harness, watcher), 1u);
}

TEST(CandidateSightComponents, AnAuthoredSensorIsTheWatchersEye)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(40.0f));
    const EntityId watcher = harness.AddPlaced(Vec3d(5, 0, 20));
    // Shorter than the argument's 30 m and short of the spot 10 m away.
    harness.Runtime.Entities().AddComponent(watcher, SightSensor{ .Range = 5.0f });
    EXPECT_EQ(SpotsSeen(harness, watcher), 0u);
}

TEST(CandidateSightComponents, AnUnusableSensorSeesNothing)
{
    CandidateHarness harness;
    harness.AttachZone(kRoom, Floor(40.0f));
    const EntityId watcher = harness.AddPlaced(Vec3d(5, 0, 20));
    harness.Runtime.Entities().AddComponent(watcher, SightSensor{ .Range = 0.0f });
    EXPECT_EQ(SpotsSeen(harness, watcher), 0u);
}
