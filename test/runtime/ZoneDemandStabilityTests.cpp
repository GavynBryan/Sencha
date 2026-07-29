// Whether streaming settles, and what happens when the focus will not.
//
// The demand policy is recomputed from scratch every frame with no memory of
// what it decided last frame. Nothing in it is hysteretic: focus resolution
// outside every zone picks the nearest box, radius demand is a hard distance
// cutoff, and cap eviction re-sorts every candidate with no credit for already
// being resident. The one damper is linger, which delays unload rather than
// steadying the demand signal.
//
// So the question is not whether the demand set flips — a pawn oscillating on a
// boundary genuinely is in two places — but whether that flipping reaches
// residency and turns into load/unload churn. These tests separate the two and
// bound the second.

#include <gtest/gtest.h>

#include <zone/ZoneDemand.h>

#include "ZoneTopologyStressFixture.h"

#include <set>
#include <string>
#include <vector>

using namespace ZoneTopologyStress;

namespace
{
WorldPartitionStreamingConfig StabilityConfig(double lingerSeconds)
{
    WorldPartitionStreamingConfig config;
    config.HopCount = 1;
    config.LingerSeconds = lingerSeconds;
    config.ResidentZoneCap = 8;
    return config;
}

// A point in the gap between two zones, nudged to one side or the other. Zone
// bounds are laid out span-wide and adjacent, so the "gap" here is the shared
// boundary plane; a position exactly on it is inside neither box by the
// half-open containment the resolver uses.
Vec3d SeamPosition(const TopologySpec& spec, int lowerZone, double offset)
{
    const Aabb3d bounds = StressZoneBounds(spec, lowerZone);
    return Vec3d{ static_cast<float>(bounds.Max.X + offset),
                  (bounds.Min.Y + bounds.Max.Y) * 0.5f,
                  (bounds.Min.Z + bounds.Max.Z) * 0.5f };
}
}  // namespace

// The pure resolver, stated plainly: outside every box, the previous zone earns
// nothing. This is the mechanism behind any focus flapping, and it is worth
// having written down because the fix for flapping, if one is ever wanted,
// belongs here rather than in the runtime that calls it.
TEST(ZoneDemandStability, FocusResolutionOutsideEveryZoneIgnoresThePreviousZone)
{
    const TopologySpec spec{ .Shape = TopologyShape::Chain, .ZoneCount = 4 };
    const WorldPartitionManifest manifest = BuildTopology(spec);

    // Just past the boundary, so zone 1 is nearest and zone 0 is not containing.
    const Vec3d justInsideUpper = SeamPosition(spec, 0, 0.25);
    EXPECT_EQ(ResolveFocusZone(manifest, justInsideUpper, StressZoneAt(0)),
              StressZoneAt(1))
        << "the previous zone held focus from outside its own bounds";

    // The same position resolves the same way regardless of where the caller
    // says it came from, which is what makes the result position-determined
    // rather than history-determined.
    EXPECT_EQ(ResolveFocusZone(manifest, justInsideUpper, StressZoneAt(3)),
              ResolveFocusZone(manifest, justInsideUpper, StressZoneAt(0)));
}

// A pawn jittering across a shared boundary. The focus zone is expected to
// follow the position; residency is expected not to, because linger holds the
// zone that was just left. This is the bound that says the default
// configuration does not thrash.
TEST(ZoneDemandStability, SeamJitterUnderDefaultLingerCausesNoReloadChurn)
{
    const TopologySpec spec{ .Shape = TopologyShape::Chain,
                             .ZoneCount = 4,
                             .EntitiesPerZone = 16 };
    TopologyHarness harness(spec, StabilityConfig(3.0));
    ASSERT_EQ(harness.LoadManifest(), "");

    harness.RelocateToZone(1);
    harness.SettleLoads();
    for (int frame = 0; frame < 8; ++frame)
        harness.StepFrame();

    ASSERT_GT(harness.Attaches(), 0) << "nothing streamed; the bound is vacuous";

    // Jitter across the zone 1 / zone 2 boundary, alternating sides every
    // frame. The first crossings legitimately widen the demand set: standing in
    // zone 2 asks for zone 3, which standing in zone 1 did not. Those are
    // absorbed by a warm-up, and the bound is on what happens afterwards.
    const auto jitter = [&](int frames)
    {
        for (int frame = 0; frame < frames; ++frame)
        {
            const double offset = (frame % 2 == 0) ? -0.05 : 0.05;
            harness.Partition().SetFocus(SeamPosition(spec, 1, offset));
            harness.StepFrame();
        }
    };

    jitter(30);
    const int attachesAfterWarmup = harness.Attaches();

    // Four more seconds of frames, well past the three-second linger, so a
    // zone that fell out of demand would have had time to unload and come back.
    jitter(240);

    EXPECT_EQ(harness.Attaches(), attachesAfterWarmup)
        << "boundary jitter re-attached zones that were already resident";
}

// The same jitter with linger switched off, which is how the traversal fixture
// runs. Recorded rather than bounded: with no damper, whether this churns is a
// property of the policy, and the number is the evidence for what linger is
// actually buying.
TEST(ZoneDemandStability, SeamJitterWithoutLingerIsMeasuredNotAssumed)
{
    const TopologySpec spec{ .Shape = TopologyShape::Chain,
                             .ZoneCount = 4,
                             .EntitiesPerZone = 16 };
    TopologyHarness harness(spec, StabilityConfig(0.0));
    ASSERT_EQ(harness.LoadManifest(), "");

    harness.RelocateToZone(1);
    harness.SettleLoads();
    for (int frame = 0; frame < 8; ++frame)
        harness.StepFrame();

    const int settled = harness.Attaches();
    for (int frame = 0; frame < 240; ++frame)
    {
        const double offset = (frame % 2 == 0) ? -0.05 : 0.05;
        harness.Partition().SetFocus(SeamPosition(spec, 1, offset));
        harness.StepFrame();
    }

    const int churn = harness.Attaches() - settled;
    // Four zones is a small world; an unbounded flap would re-attach on a large
    // fraction of the 240 frames. The bound is deliberately loose because the
    // point is to catch a runaway, not to pin the current number.
    EXPECT_LT(churn, 120) << "zero-linger boundary jitter re-attached on "
                          << churn << " of 240 frames";
}

// Streaming has to reach a fixed point. Holding the focus still must stop
// producing work, or every frame pays for a decision that never changes.
TEST(ZoneDemandStability, AStationaryFocusStopsProducingStreamingWork)
{
    const TopologySpec spec{ .Shape = TopologyShape::Grid,
                             .ZoneCount = 16,
                             .EntitiesPerZone = 16 };
    TopologyHarness harness(spec, StabilityConfig(3.0));
    ASSERT_EQ(harness.LoadManifest(), "");

    harness.RelocateToZone(5);
    harness.SettleLoads();
    for (int frame = 0; frame < 16; ++frame)
        harness.StepFrame();

    const int settledAttaches = harness.Attaches();
    const std::size_t settledBuilds = harness.BuildSequence().size();
    const int settledResident = harness.ResidentCount();
    ASSERT_GT(settledResident, 0);

    for (int frame = 0; frame < 120; ++frame)
        harness.StepFrame();

    EXPECT_EQ(harness.Attaches(), settledAttaches)
        << "a stationary focus kept attaching zones";
    EXPECT_EQ(harness.BuildSequence().size(), settledBuilds)
        << "a stationary focus kept issuing builds";
    EXPECT_EQ(harness.ResidentCount(), settledResident)
        << "residency drifted under a stationary focus";
}

// The resident cap is the streaming budget. Walking a world larger than the cap
// must keep residency inside it rather than accumulating everything visited.
TEST(ZoneDemandStability, WalkingAWorldLargerThanTheCapKeepsResidencyBounded)
{
    const TopologySpec spec{ .Shape = TopologyShape::Chain,
                             .ZoneCount = 24,
                             .EntitiesPerZone = 16 };
    WorldPartitionStreamingConfig config = StabilityConfig(0.0);
    config.ResidentZoneCap = 4;
    TopologyHarness harness(spec, config);
    ASSERT_EQ(harness.LoadManifest(), "");

    harness.RelocateToZone(0);
    harness.SettleLoads();

    int worstResident = 0;
    for (int zone = 0; zone < spec.ZoneCount; ++zone)
    {
        harness.SetFocusToZone(zone);
        for (int frame = 0; frame < 4; ++frame)
        {
            harness.StepFrame();
            worstResident = std::max(worstResident, harness.ResidentCount());
        }
    }

    ASSERT_GT(harness.Attaches(), spec.ZoneCount / 2)
        << "the walk did not stream enough to bound anything";
    // Focus and pins may exceed the cap by design; the demand set is what the
    // cap governs. A walk that stayed near the cap has a bounded working set,
    // which is the property worth protecting.
    EXPECT_LE(worstResident, config.ResidentZoneCap + 2)
        << "residency grew past the cap while walking";
}

// Streaming decisions must not depend on how many workers happened to run. The
// existing determinism test compares where a traversal ends up; this compares
// the order the decisions were made in, which is the stronger statement.
TEST(ZoneDemandStability, TheBuildSequenceIsIdenticalOnRepeatedSerialRuns)
{
    const TopologySpec spec{ .Shape = TopologyShape::Grid,
                             .ZoneCount = 16,
                             .EntitiesPerZone = 8 };

    const auto runOnce = [&]
    {
        TopologyHarness harness(spec, StabilityConfig(1.0));
        EXPECT_EQ(harness.LoadManifest(), "");
        harness.RelocateToZone(0);
        for (int zone = 0; zone < spec.ZoneCount; ++zone)
        {
            harness.SetFocusToZone(zone);
            for (int frame = 0; frame < 3; ++frame)
                harness.StepFrame();
        }
        harness.SettleLoads();
        return harness.BuildSequence();
    };

    const std::vector<ZoneId> first = runOnce();
    const std::vector<ZoneId> second = runOnce();

    ASSERT_FALSE(first.empty()) << "the walk issued no builds";
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t index = 0; index < first.size(); ++index)
        EXPECT_EQ(first[index].Value, second[index].Value)
            << "decision " << index << " differed between identical runs";
}
