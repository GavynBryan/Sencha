// Warmed candidate evaluation allocates nothing: scratch, results and
// navigation context are sized once. For the authored-query measure the
// guarantee is the engine's side of the call only, so the query here is one
// the test knows does not allocate. Lives in core_tests because only this
// binary replaces operator new to count allocations.

#include "AllocationCounter.h"
#include "spatial/candidates/CandidateHarness.h"

#include <authored/AuthoredQueryDispatcher.h>

#include <array>

namespace
{
    const ZoneId kRoom{ 1 };

    struct Rounds
    {
        std::int64_t Value = 5;
    };

    AuthoredQueryStatus FixedRounds(const Rounds& rounds, std::span<const AuthoredValue>, AuthoredValue& result)
    {
        result = AuthoredValue::Int(rounds.Value);
        return AuthoredQueryStatus::Value;
    }
}

TEST(CandidateAllocation, WarmedRunsAllocateNothing)
{
    CandidateHarness harness;
    NavTestGeometry room;
    room.AddFloor(0, 0, 30, 30);
    const RuntimeZoneRecord& zone = harness.AttachZone(kRoom, room);
    harness.AddStaticBox(Vec3d(14, 0, 8), Vec3d(16, 3, 10), EntityId{ 77, 1 });
    for (int index = 0; index < 4; ++index)
        (void)harness.AddTagged(Vec3d(5.0f + 5.0f * index, 0, 25), { "marker.cover" }, zone.Partition);

    AuthoredQueryRegistry registry;
    {
        AuthoredQueryRegistrationScope scope(registry, "test");
        AuthoredQueryDefinition rounds;
        rounds.Name = "test.rounds";
        rounds.Arguments.Children.push_back(MakeDataField(DataFieldKind::Entity, "Target", "Target"));
        rounds.Result = MakeDataField(DataFieldKind::Int, "", "Rounds");
        ASSERT_TRUE(scope.Declare(std::move(rounds)));
        ASSERT_TRUE(scope.Commit());
    }
    AuthoredQueryDispatcher dispatcher(registry);
    const Rounds rounds;
    const AuthoredQueryBindingToken token = dispatcher.Bind<&FixedRounds>(registry.Find("test.rounds"), rounds);
    CandidateEvaluator evaluator(harness.Runtime, harness.Catalogs, &harness.Physics, &dispatcher);

    const CandidateEvaluation firing = harness.Compile(R"({
        "slots": [{ "name": "target" }],
        "generators": [{ "generator": "candidates.generator.ring",
                         "arguments": { "center": "target", "points_per_ring": 16, "outer_radius": 10 } }],
        "criteria": [
            { "measure": "candidates.measure.reachable", "arguments": { "max_cost": 100 }, "require": { "expect": true } },
            { "measure": "candidates.measure.visibility", "arguments": { "slot": "target", "observer": "candidate" },
              "require": { "expect": true } },
            { "measure": "candidates.measure.distance", "arguments": { "slot": "target" },
              "score": { "curve": { "shape": "band", "low": 6, "preferred_low": 8, "preferred_high": 12, "high": 14 } } }
        ],
        "selection": { "mode": "top_n", "count": 3 }
    })");
    const CandidateEvaluation cover = harness.Compile(R"({
        "generators": [{ "generator": "candidates.generator.entities", "arguments": { "tags": { "all": ["marker.cover"] } } }],
        "criteria": [
            { "measure": "candidates.measure.authored_query",
              "arguments": { "query": "test.rounds", "entity_argument": "Target" }, "require": { "min": 1 } },
            { "measure": "candidates.measure.travel_cost", "arguments": { "fidelity": "exact" },
              "score": { "curve": { "shape": "linear", "direction": "falling", "low": 0, "high": 60 } } }
        ],
        "selection": { "mode": "best" }
    })", &registry);
    const std::array target{ CandidatePoint{ .Position = Vec3d(15, 0, 15) } };
    const std::array slots{ CandidateSlotBinding{ 1, target } };
    CandidateContext context = QuerierAt(harness, kRoom, Vec3d(15, 0, 28));
    context.Slots = slots;
    const CandidateContext coverContext = QuerierAt(harness, kRoom, Vec3d(15, 0, 28));

    const auto runs = [&] {
        const bool firingOk = evaluator.Evaluate(firing, context, harness.Scratch, harness.Results).Status
            == CandidateRunStatus::Success;
        const bool coverOk = evaluator.Evaluate(cover, coverContext, harness.Scratch, harness.Results).Status
            == CandidateRunStatus::Success;
        return firingOk && coverOk;
    };

    ASSERT_TRUE(runs()); // warm-up sizes the navigation context and query caches
    const std::size_t before = AllocationCount();
    bool allOk = true;
    for (int index = 0; index < 1000; ++index)
        allOk = runs() && allOk;
    const std::size_t after = AllocationCount();
    EXPECT_TRUE(allOk);
    EXPECT_EQ(after, before) << "a warmed candidate evaluation allocated";
}
