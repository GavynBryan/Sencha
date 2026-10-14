// Sight through PerceptionSystem: range, cone, occlusion, coverage and the
// frame's lifetime, one fixed tick at a time.

#include "PerceptionHarness.h"

#include <world/SimulationAuthority.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
    constexpr float kHalfTurn = std::numbers::pi_v<float>;
}

TEST(SightPerception, SeesATargetInRangeAndNotOnceItLeaves)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero(), 20.0f);
    const EntityId target = harness.Target(Vec3d(0, 0, -10));

    harness.Step();
    ASSERT_TRUE(harness.Sees(eye, target));
    const Observation& seen = *harness.Frame().Find(eye, target);
    EXPECT_EQ(seen.Tick, harness.Tick);
    EXPECT_FLOAT_EQ(seen.Confidence, 1.0f);
    EXPECT_NEAR(seen.Position.Z, -10.0f, 1e-4f);

    harness.Place(target, Vec3d(0, 0, -30));
    harness.Step();
    EXPECT_FALSE(harness.Sees(eye, target));
    EXPECT_EQ(harness.Frame().CoverageOf(eye), ScanCoverage::Complete);
    EXPECT_EQ(harness.System.LastSightStats().OutOfRange, 1u);
}

TEST(SightPerception, ATargetBehindIsUnseenUntilTheObserverTurns)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, 10));

    harness.Step();
    EXPECT_FALSE(harness.Sees(eye, target));
    EXPECT_EQ(harness.System.LastSightStats().OutsideCone, 1u);
    EXPECT_EQ(harness.System.LastSightStats().Raycasts, 0u);

    harness.Place(eye, Vec3d::Zero(), PerceptionHarness::Yaw(kHalfTurn));
    harness.Step();
    EXPECT_TRUE(harness.Sees(eye, target));
}

TEST(SightPerception, AWallHidesTheTargetAndRemovingItRevealsIt)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -10));
    const PhysicsBodyId wall = harness.Box(Vec3d(0, 0, -5), Vec3d(2, 2, 0.5f));

    harness.Step();
    EXPECT_FALSE(harness.Sees(eye, target));
    // Hidden is an answer: the scan finished.
    EXPECT_EQ(harness.Frame().CoverageOf(eye), ScanCoverage::Complete);
    EXPECT_EQ(harness.System.LastSightStats().Blocked, 1u);

    harness.Physics.RemoveBody(wall);
    harness.Step();
    EXPECT_TRUE(harness.Sees(eye, target));
}

TEST(SightPerception, TriggersDoNotBlock)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -10));
    (void)harness.Box(Vec3d(0, 0, -5), Vec3d(2, 2, 0.5f), {}, true);

    harness.Step();
    EXPECT_TRUE(harness.Sees(eye, target));
}

TEST(SightPerception, TheObserversOwnBodyDoesNotBlockItAndItNeverSeesItself)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    harness.Entities.AddComponent<SightTarget>(eye, SightTarget{ .SampleOffset = Vec3d::Zero() });
    (void)harness.Box(Vec3d::Zero(), Vec3d(1, 1, 1), eye);
    const EntityId target = harness.Target(Vec3d(0, 0, -10));

    harness.Step();
    EXPECT_TRUE(harness.Sees(eye, target));
    EXPECT_FALSE(harness.Sees(eye, eye));
}

TEST(SightPerception, ATargetsOwnBodyCountsAsReachingIt)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -10));
    (void)harness.Box(Vec3d(0, 0, -10), Vec3d(1, 1, 1), target);

    harness.Step();
    EXPECT_TRUE(harness.Sees(eye, target));
}

TEST(SightPerception, TheEyeAndSampleOffsetsFollowTheEntity)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    harness.Entities.TryGet<SightSensor>(eye)->EyeOffset = Vec3d(0, 3, 0);
    const EntityId target = harness.Target(Vec3d(0, 0, -10));
    harness.Entities.TryGet<SightTarget>(target)->SampleOffset = Vec3d(0, 3, 0);
    // A low wall the raised eye looks over.
    (void)harness.Box(Vec3d(0, 0.5f, -5), Vec3d(2, 1, 0.5f));

    harness.Step();
    ASSERT_TRUE(harness.Sees(eye, target));
    EXPECT_NEAR(harness.Frame().Find(eye, target)->Position.Y, 3.0f, 1e-4f);
}

TEST(SightPerception, AnUnusableSensorSeesNothingAndIsCounted)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero(), -1.0f);
    const EntityId target = harness.Target(Vec3d(0, 0, -5));

    harness.Step();
    EXPECT_FALSE(harness.Sees(eye, target));
    EXPECT_EQ(harness.Frame().CoverageOf(eye), ScanCoverage::NotEvaluated);
    EXPECT_EQ(harness.System.LastSightStats().UnusableSensors, 1u);
}

TEST(SightPerception, TheRayCapReportsAPartialScanNotAbsence)
{
    PerceptionHarness harness;
    harness.SetCVar("perception.sight.rays_per_observer", CVarValue{ std::int64_t{ 1 } });
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId near = harness.Target(Vec3d(0, 0, -5));
    const EntityId far = harness.Target(Vec3d(0, 0, -8));

    harness.Step();
    EXPECT_EQ(harness.Frame().CoverageOf(eye), ScanCoverage::Partial);
    EXPECT_TRUE(harness.Sees(eye, near));
    EXPECT_FALSE(harness.Sees(eye, far));
    EXPECT_EQ(harness.System.LastSightStats().Untested, 1u);
    EXPECT_EQ(harness.System.LastSightStats().PartialScans, 1u);
}

TEST(SightPerception, TheCapIsPerObserverSoNoObserverStarves)
{
    PerceptionHarness harness;
    harness.SetCVar("perception.sight.rays_per_observer", CVarValue{ std::int64_t{ 1 } });
    std::vector<EntityId> eyes;
    for (int i = 0; i < 4; ++i)
        eyes.push_back(harness.Observer(Vec3d(static_cast<float>(i) * 3.0f, 0, 0)));
    for (int i = 0; i < 4; ++i)
        (void)harness.Target(Vec3d(static_cast<float>(i) * 3.0f, 0, -6));

    harness.Step();
    for (const EntityId eye : eyes)
        EXPECT_EQ(harness.Frame().ScansOf(eye)[0].Count, 1u) << "observer " << eye.Index;
}

TEST(SightPerception, ADestroyedTargetLeavesNoObservation)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -5));
    harness.Step();
    ASSERT_TRUE(harness.Sees(eye, target));

    harness.Entities.DestroyEntity(target);
    harness.Step();
    EXPECT_FALSE(harness.Sees(eye, target));
    EXPECT_EQ(harness.Frame().ScansOf(eye)[0].Count, 0u);
}

TEST(SightPerception, RemovingTheComponentsRemovesTheCapability)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -5));
    harness.Step();
    ASSERT_TRUE(harness.Sees(eye, target));

    harness.Entities.RemoveComponent<SightTarget>(target);
    harness.Step();
    EXPECT_FALSE(harness.Sees(eye, target));

    harness.Entities.RemoveComponent<SightSensor>(eye);
    harness.Step();
    EXPECT_TRUE(harness.Frame().Scans().empty());
}

TEST(SightPerception, AWorldWithNoSensorsScansNothing)
{
    PerceptionHarness harness;
    (void)harness.Target(Vec3d(0, 0, -5));
    harness.Step();
    EXPECT_TRUE(harness.Frame().Scans().empty());
    EXPECT_EQ(harness.System.LastSightStats().Candidates, 0u);
    EXPECT_EQ(harness.System.LastSightStats().Raycasts, 0u);
}

TEST(SightPerception, AClientSensesNothingAndClaimsNoAbsence)
{
    PerceptionHarness harness;
    harness.Entities.SetResource(SimulationAuthority{ .Authoritative = false });
    const EntityId eye = harness.Observer(Vec3d::Zero());
    const EntityId target = harness.Target(Vec3d(0, 0, -5));

    harness.Step();
    EXPECT_FALSE(harness.Sees(eye, target));
    EXPECT_EQ(harness.Frame().CoverageOf(eye), ScanCoverage::NotEvaluated);
}

TEST(SightPerception, ResultsDoNotDependOnChunkLayoutOrChurn)
{
    const auto observe = [](bool churn) {
        PerceptionHarness harness;
        std::vector<EntityId> eyes;
        std::vector<EntityId> targets;
        for (int i = 0; i < 6; ++i)
        {
            eyes.push_back(harness.Observer(Vec3d(static_cast<float>(i), 0, 0)));
            targets.push_back(harness.Target(Vec3d(static_cast<float>(i) * 0.5f, 0, -8)));
            if (churn && i % 2 == 0)
            {
                // A different archetype for some of each, and slots created and freed between them.
                harness.Entities.AddComponent<GameplayTagContainer>(eyes.back(), {});
                harness.Entities.AddComponent<GameplayTagContainer>(targets.back(), {});
                harness.Entities.DestroyEntity(harness.Entities.CreateEntity());
            }
        }
        harness.Step();
        std::vector<std::pair<std::uint32_t, std::uint32_t>> seen;
        for (std::size_t o = 0; o < eyes.size(); ++o)
            for (std::size_t t = 0; t < targets.size(); ++t)
                if (harness.Sees(eyes[o], targets[t]))
                    seen.emplace_back(static_cast<std::uint32_t>(o), static_cast<std::uint32_t>(t));
        const std::span<const SenseScan> scans = harness.Frame().Scans();
        EXPECT_TRUE(std::ranges::is_sorted(scans, {}, &SenseScan::Observer));
        return seen;
    };

    const auto plain = observe(false);
    EXPECT_FALSE(plain.empty());
    EXPECT_EQ(plain, observe(true));
}

TEST(SightPerception, ASelectedObserverIsTracedCandidateByCandidate)
{
    PerceptionHarness harness;
    const EntityId eye = harness.Observer(Vec3d::Zero(), 20.0f);
    const EntityId other = harness.Observer(Vec3d(5, 0, 0));
    const EntityId visible = harness.Target(Vec3d(0, 0, -5));
    const EntityId behind = harness.Target(Vec3d(0, 0, 5));
    const EntityId far = harness.Target(Vec3d(0, 0, -40));
    harness.System.SelectObserver(eye);

    harness.Step();
    std::vector<std::pair<EntityId, SightOutcome>> rows;
    for (const SightTraceRow& row : harness.System.LastTrace())
    {
        EXPECT_EQ(row.Observer, eye);
        rows.emplace_back(row.Target, row.Outcome);
    }
    const std::vector<std::pair<EntityId, SightOutcome>> expected{
        { visible, SightOutcome::Seen }, { behind, SightOutcome::OutsideCone }, { far, SightOutcome::OutOfRange } };
    EXPECT_EQ(rows, expected);
    (void)other;
}
