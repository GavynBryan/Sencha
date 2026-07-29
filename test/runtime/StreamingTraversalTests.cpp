// Bounds over a traversal: the focus walks a chain of zones and back, several
// laps, through the real streaming path with frame work running every step. See
// StreamingTraversalFixture.h for the scenario.
//
// This is where the hardening phases are checked against each other rather than
// one at a time. Everything asserted here is counted work, so it holds on any
// machine and in any build; the wall-clock version of the same traversal lives in
// StreamingBench.Generate and the evidence doc.
//
// What this does NOT cover: the GPU. Nothing in this repository drives multi-zone
// streaming with a renderer attached — SceneViewer loads one zone and refuses a
// second — so cost here is the owner thread only. The application driver is a game
// module, which owns the focus position and streaming policy that make a traversal
// meaningful; frame cost with a renderer attached was measured there and is
// recorded in docs/plans/unified-world-hardening.md.

#include <gtest/gtest.h>

#include "StreamingTraversalFixture.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace
{
using namespace StreamingTraversal;

class StreamingTraversalTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::string error = Traversal.LoadManifest();
        ASSERT_TRUE(error.empty()) << error;
    }

    Harness Traversal;
};
} // namespace

// Resident chunk memory must track how much world is live at once, not how much
// has ever streamed through. This is the criterion-3 property observed over a real
// traversal rather than a synthetic load/unload loop: the census after one lap and
// after four must match, because the resident zone cap is the same at both points.
TEST_F(StreamingTraversalTest, ResidentChunksPlateauAcrossLaps)
{
    Traversal.WalkLap();
    const size_t afterFirstLap = Traversal.World().Entities().ChunkCount();
    ASSERT_GT(afterFirstLap, 0u) << "the traversal streamed nothing";
    // More attaches than zones means zones were unloaded and re-streamed, which is
    // the only way this bound means anything.
    ASSERT_GT(Traversal.Attaches(), kZoneCount)
        << "only " << Traversal.Attaches() << " attaches for " << kZoneCount
        << " zones: nothing was unloaded, so no memory could accumulate";

    std::vector<size_t> perLap;
    for (int lap = 0; lap < 3; ++lap)
    {
        Traversal.WalkLap();
        perLap.push_back(Traversal.World().Entities().ChunkCount());
    }

    for (size_t lap = 0; lap < perLap.size(); ++lap)
    {
        EXPECT_EQ(perLap[lap], afterFirstLap)
            << "lap " << lap + 2 << " holds " << perLap[lap]
            << " chunks against " << afterFirstLap << " after the first: "
            << "streaming history is accumulating memory";
    }
}

// The propagation order is rebuilt when the hierarchy changes, and a streamed zone
// carries hierarchy, so an attach or unload legitimately rebuilds. What must not
// happen is a rebuild every frame: that would mean the order is keyed on something
// that moves whether or not the hierarchy did.
TEST_F(StreamingTraversalTest, OrderRebuildsTrackZoneEventsNotFrames)
{
    // Settle: the first lap includes the initial attaches.
    Traversal.WalkLap();

    const int attachesBefore = Traversal.Attaches();
    const uint64_t rebuildsBefore = Traversal.Cache().RebuildCount();
    const uint64_t resolvesBefore = Traversal.Cache().AddressResolveCount();

    Traversal.WalkLap();

    const uint64_t rebuilds = Traversal.Cache().RebuildCount() - rebuildsBefore;
    const uint64_t resolves =
        Traversal.Cache().AddressResolveCount() - resolvesBefore;
    const int attaches = Traversal.Attaches() - attachesBefore;

    ASSERT_GT(attaches, 0)
        << "the second lap streamed nothing, so a rebuild count of zero proves "
           "nothing";

    // A lap crosses far fewer zone boundaries than it renders frames, and only a
    // zone event can change the hierarchy.
    EXPECT_LT(rebuilds, static_cast<uint64_t>(kFramesPerLap))
        << rebuilds << " order rebuilds over " << kFramesPerLap
        << " frames: the order is being invalidated by something other than the "
           "hierarchy";

    // Address resolution is driven by structural change, which streaming causes on
    // purpose — it just has to stay proportional to zone events too.
    EXPECT_LE(resolves, static_cast<uint64_t>(kFramesPerLap))
        << resolves << " address resolves over " << kFramesPerLap << " frames";
}

// Transform results must not depend on how the traversal arrived at a state. A
// zone unloaded and re-streamed several times must produce the same world
// transforms as one freshly attached — the check that reclaimed chunk slabs,
// recycled partition ids, and scoped invalidation compose.
TEST_F(StreamingTraversalTest, RestreamedZoneMatchesAFreshAttach)
{
    Traversal.WalkLap();

    const auto sampleFocusZone = [this]
    {
        Traversal.SetFocusToZone(0);
        for (int frame = 0; frame < 8; ++frame)
            Traversal.StepFrame();

        const RuntimeZoneRecord* record = Traversal.World().FindZone(ZoneAt(0));
        if (record == nullptr)
            return std::vector<Vec3d>{};

        std::vector<Vec3d> positions;
        World& entities = Traversal.World().Entities();
        Query<Read<WorldTransform>> query(entities);
        StoragePartitionSet only;
        only.Add(record->Partition);
        query.ForEachChunkIn(only, [&](auto& view)
        {
            const auto worlds = view.template Read<WorldTransform>();
            for (uint32_t row = 0; row < view.Count(); ++row)
                positions.push_back(worlds[row].Value.Position);
        });
        std::sort(
            positions.begin(),
            positions.end(),
            [](const Vec3d& a, const Vec3d& b)
            {
                if (a.X != b.X) return a.X < b.X;
                if (a.Y != b.Y) return a.Y < b.Y;
                return a.Z < b.Z;
            });
        return positions;
    };

    const std::vector<Vec3d> first = sampleFocusZone();
    ASSERT_FALSE(first.empty()) << "zone 0 was not resident at the start of the lap";
    EXPECT_EQ(first.size(), static_cast<size_t>(kEntitiesPerZone));
    const int attachesAfterFirstSample = Traversal.Attaches();

    for (int lap = 0; lap < 2; ++lap)
    {
        Traversal.WalkLap();
        const std::vector<Vec3d> again = sampleFocusZone();
        ASSERT_GT(Traversal.Attaches(), attachesAfterFirstSample + lap)
            << "zone 0 was never re-streamed, so this compares a zone with itself";
        ASSERT_EQ(again.size(), first.size()) << "lap " << lap + 1;
        for (size_t index = 0; index < first.size(); ++index)
        {
            EXPECT_EQ(again[index], first[index])
                << "lap " << lap + 1 << ", entity " << index
                << ": a re-streamed zone produced a different world transform";
        }
    }
}

namespace
{
// The residency and participation of every zone, as one comparable line.
std::string ResidencySnapshot(RuntimeWorld& world)
{
    std::string line;
    for (int index = 0; index < kZoneCount; ++index)
    {
        const RuntimeZoneRecord* zone = world.FindZone(ZoneAt(index));
        line += zone == nullptr ? "-" : "VPLA";
        if (zone != nullptr)
        {
            const std::size_t base = line.size() - 4;
            if (!zone->Participation.Visible) line[base + 0] = '.';
            if (!zone->Participation.Physics) line[base + 1] = '.';
            if (!zone->Participation.Logic)   line[base + 2] = '.';
            if (!zone->Participation.Audio)   line[base + 3] = '.';
        }
        line += "|";
    }
    return line;
}

struct SettledLap
{
    std::vector<std::string> Observations;
    int Attaches = 0;
};

// One lap, with all in-flight work settled before each observation, so what the
// record holds is streaming policy rather than how many workers happened to run.
SettledLap RunSettledLap(unsigned taskThreads)
{
    Harness harness(taskThreads);
    const std::string error = harness.LoadManifest();
    EXPECT_TRUE(error.empty()) << error;

    SettledLap lap;
    harness.WalkLap([&]
    {
        harness.StepFrame();
        harness.SettleLoads();
        lap.Observations.push_back(ResidencySnapshot(harness.World()));
    });
    lap.Attaches = harness.Attaches();
    return lap;
}
} // namespace

TEST(StreamingTraversalDeterminism, SettledTraversalIsIdenticalAcrossTaskThreadCounts)
{
    // The async lane may finish a build on any thread and in any order. Once the
    // in-flight work is settled, which zones are resident and at what
    // participation is policy, and policy does not know the worker count.
    const SettledLap serial = RunSettledLap(0);
    const SettledLap oneWorker = RunSettledLap(1);
    const SettledLap fourWorkers = RunSettledLap(4);

    // A lap that streamed nothing, or that never changed shape, would satisfy
    // the comparison trivially.
    ASSERT_GT(serial.Attaches, kZoneCount);
    ASSERT_GT(std::set<std::string>(serial.Observations.begin(),
                                    serial.Observations.end()).size(), 1u);
    ASSERT_EQ(serial.Observations.size(), static_cast<std::size_t>(kFramesPerLap));

    EXPECT_EQ(serial.Observations, oneWorker.Observations);
    EXPECT_EQ(serial.Observations, fourWorkers.Observations);
    EXPECT_EQ(serial.Attaches, oneWorker.Attaches);
    EXPECT_EQ(serial.Attaches, fourWorkers.Attaches);
}
