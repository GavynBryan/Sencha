// A run is a pure function of its inputs: identical inputs give identical
// results and traces, storage layout never reorders candidates, and a seeded
// pick repeats per seed.

#include "CandidateHarness.h"

#include <array>
#include <set>
#include <vector>

namespace
{
    const ZoneId kRoom{ 1 };

    void ExpectSameResults(std::span<const CandidateResultEntry> left, std::span<const CandidateResultEntry> right)
    {
        ASSERT_EQ(left.size(), right.size());
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            EXPECT_EQ(left[index].Position, right[index].Position);
            EXPECT_EQ(left[index].Entity, right[index].Entity);
            EXPECT_EQ(left[index].Nav.Ref, right[index].Nav.Ref);
            EXPECT_EQ(left[index].Score, right[index].Score);
            EXPECT_EQ(left[index].Order, right[index].Order);
            EXPECT_EQ(left[index].Rank, right[index].Rank);
        }
    }

    void ExpectSameTraces(const CandidateTrace& left, const CandidateTrace& right)
    {
        ASSERT_EQ(left.Rows().size(), right.Rows().size());
        ASSERT_EQ(left.Criteria().size(), right.Criteria().size());
        for (std::uint32_t criterion = 0; criterion < left.Criteria().size(); ++criterion)
        {
            EXPECT_EQ(left.Criteria()[criterion].Statuses, right.Criteria()[criterion].Statuses);
            EXPECT_EQ(left.Criteria()[criterion].Raycasts, right.Criteria()[criterion].Raycasts);
            EXPECT_EQ(left.Criteria()[criterion].ExactNavSearches, right.Criteria()[criterion].ExactNavSearches);
        }
        for (std::uint32_t row = 0; row < left.Rows().size(); ++row)
        {
            EXPECT_EQ(left.Rows()[row].Position, right.Rows()[row].Position);
            EXPECT_EQ(left.Rows()[row].Outcome, right.Rows()[row].Outcome);
            EXPECT_EQ(left.Rows()[row].Score, right.Rows()[row].Score);
            for (std::uint32_t criterion = 0; criterion < left.Criteria().size(); ++criterion)
            {
                EXPECT_EQ(left.Value(row, criterion).Value, right.Value(row, criterion).Value);
                EXPECT_EQ(left.Value(row, criterion).Status, right.Value(row, criterion).Status);
            }
        }
    }

    constexpr std::string_view kCoverSearch = R"({
        "slots": [{ "name": "target" }],
        "generators": [
            { "generator": "candidates.generator.ring", "arguments": { "center": "target", "rings": 2,
              "points_per_ring": 12, "inner_radius": 6, "outer_radius": 10 } },
            { "generator": "candidates.generator.entities", "arguments": { "tags": { "all": ["marker.cover"] } } }
        ],
        "criteria": [
            { "measure": "candidates.measure.travel_cost", "arguments": { "max_cost": 200 },
              "score": { "curve": { "shape": "linear", "direction": "falling", "low": 0, "high": 40 } } },
            { "measure": "candidates.measure.visibility", "arguments": { "slot": "target", "reduce": "any" },
              "score": { "curve": { "shape": "step", "direction": "falling", "low": 0 } }, "unmeasured": { "score": 0 } },
            { "measure": "candidates.measure.distance", "arguments": { "slot": "target" }, "require": { "min": 3 } }
        ],
        "selection": { "mode": "all_qualified" }
    })";
}

TEST(CandidateDeterminism, RepeatedRunsGiveIdenticalResultsAndTraces)
{
    CandidateHarness harness;
    NavTestGeometry room;
    room.AddFloor(0, 0, 30, 30);
    const RuntimeZoneRecord& zone = harness.AttachZone(kRoom, room);
    harness.AddStaticBox(Vec3d(13, 0, 10), Vec3d(17, 3, 11));
    for (int index = 0; index < 6; ++index)
        (void)harness.AddTagged(Vec3d(4.0f + 3.0f * index, 0, 24), { "marker.cover" }, zone.Partition);

    const CandidateEvaluation evaluation = harness.Compile(kCoverSearch);
    const std::array target{ CandidatePoint{ .Position = Vec3d(15, 0, 15) } };
    const std::array slots{ CandidateSlotBinding{ 1, target } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(3, 0, 3));
    context.Slots = slots;

    CandidateResultBuffer first(64);
    CandidateResultBuffer second(64);
    CandidateTrace firstTrace(CandidateTraceLevel::Full, 64);
    CandidateTrace secondTrace(CandidateTraceLevel::Full, 64);
    ASSERT_EQ(harness.Evaluator.Evaluate(evaluation, context, harness.Scratch, first, &firstTrace).Status,
              CandidateRunStatus::Success);
    CandidateScratch otherScratch;
    ASSERT_EQ(harness.Evaluator.Evaluate(evaluation, context, otherScratch, second, &secondTrace).Status,
              CandidateRunStatus::Success);

    ExpectSameResults(first.Entries(), second.Entries());
    ExpectSameTraces(firstTrace, secondTrace);
}

TEST(CandidateDeterminism, StructuralChurnElsewhereChangesNothing)
{
    CandidateHarness harness;
    NavTestGeometry room;
    room.AddFloor(0, 0, 30, 30);
    const RuntimeZoneRecord& zone = harness.AttachZone(kRoom, room);
    std::vector<EntityId> covers;
    for (int index = 0; index < 6; ++index)
        covers.push_back(harness.AddTagged(Vec3d(4.0f + 3.0f * index, 0, 24), { "marker.cover" }, zone.Partition));
    std::vector<EntityId> bystanders;
    for (int index = 0; index < 8; ++index)
        bystanders.push_back(harness.AddTagged(Vec3d(1, 0, 1.0f + index), { "marker.bystander" }, zone.Partition));

    const CandidateEvaluation evaluation = harness.Compile(kCoverSearch);
    const std::array target{ CandidatePoint{ .Position = Vec3d(15, 0, 15) } };
    const std::array slots{ CandidateSlotBinding{ 1, target } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(3, 0, 3));
    context.Slots = slots;

    CandidateResultBuffer before(64);
    ASSERT_EQ(harness.Evaluator.Evaluate(evaluation, context, harness.Scratch, before).Status, CandidateRunStatus::Success);

    // Archetype moves: bystanders and some candidates change signature, which
    // relocates their rows between chunks.
    World& world = harness.Runtime.Entities();
    for (std::size_t index = 0; index < bystanders.size(); index += 2)
        world.AddComponent(bystanders[index], NavigationGeometry{});
    world.AddComponent(covers[1], NavigationGeometry{});
    world.AddComponent(covers[4], NavigationGeometry{});
    world.DestroyEntity(bystanders[3]);

    CandidateResultBuffer after(64);
    ASSERT_EQ(harness.Evaluator.Evaluate(evaluation, context, harness.Scratch, after).Status, CandidateRunStatus::Success);
    ExpectSameResults(before.Entries(), after.Entries());
}

TEST(CandidateDeterminism, PickFromBandRepeatsPerSeedAndReachesEveryMember)
{
    CandidateHarness harness;
    (void)harness.AttachBareZone(kRoom);
    const CandidateEvaluation evaluation = harness.Compile(R"({
        "slots": [{ "name": "spots" }],
        "generators": [{ "generator": "candidates.generator.points", "arguments": { "slot": "spots" } }],
        "selection": { "mode": "pick_from_band", "band_width": 0 }
    })");
    std::array<CandidatePoint, 6> spots;
    for (std::size_t index = 0; index < spots.size(); ++index)
        spots[index].Position = Vec3d(static_cast<float>(index), 0, 0);
    const std::array slots{ CandidateSlotBinding{ 1, spots } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d::Zero());
    context.Slots = slots;

    std::set<std::uint32_t> picked;
    for (std::uint64_t seed = 0; seed < 1000; ++seed)
    {
        context.Seed = seed;
        ASSERT_EQ(harness.Run(evaluation, context).Status, CandidateRunStatus::Success);
        ASSERT_EQ(harness.Returned().size(), 1u);
        const std::uint32_t order = harness.Returned()[0].Order;
        picked.insert(order);

        ASSERT_EQ(harness.Run(evaluation, context).Status, CandidateRunStatus::Success);
        EXPECT_EQ(harness.Returned()[0].Order, order) << "seed " << seed;
    }
    EXPECT_EQ(picked.size(), spots.size());
}
