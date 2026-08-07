// What the partition's manifest-level mechanisms owe a world as it grows.
//
// Index construction, validation, and demand policy are pure functions of the
// manifest, so their behavior at five hundred zones can be asserted without
// loading a single entity. The bounds here are counted work and structural
// results, never wall clock, so they hold on any machine and in any build.
//
// The load-bearing one is DemandSetSizeDoesNotGrowWithWorldSize. Demand is
// recomputed every frame, and the residency it drives is what a frame budget is
// spent on; if the set ever started scaling with world size rather than with
// the neighbourhood the focus can reach, streaming cost would grow with content
// the player is nowhere near.

#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/WorldPartitionValidation.h>
#include <zone/ZoneDemand.h>

#include "ZoneTopologyStressFixture.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace ZoneTopologyStress;

namespace
{
// The sweep every scaling assertion runs over. 512 is well past any world the
// engine has been driven with, which is the point: a bound that only holds at
// the size already shipped is not a bound.
constexpr int kScales[] = { 8, 32, 128, 512 };

std::vector<TopologySpec> ShapesAt(int zoneCount)
{
    return {
        TopologySpec{ .Shape = TopologyShape::Chain, .ZoneCount = zoneCount },
        TopologySpec{ .Shape = TopologyShape::Grid, .ZoneCount = zoneCount },
        TopologySpec{ .Shape = TopologyShape::Hub, .ZoneCount = zoneCount },
        TopologySpec{ .Shape = TopologyShape::MultiGraph,
                      .ZoneCount = zoneCount,
                      .GraphCount = 4 },
    };
}

const char* ShapeName(TopologyShape shape)
{
    switch (shape)
    {
        case TopologyShape::Chain: return "Chain";
        case TopologyShape::Grid: return "Grid";
        case TopologyShape::Hub: return "Hub";
        case TopologyShape::MultiGraph: return "MultiGraph";
    }
    return "?";
}

std::string Describe(const TopologySpec& spec)
{
    return std::string(ShapeName(spec.Shape)) + " x" + std::to_string(spec.ZoneCount);
}
}  // namespace

// A generated topology is a topology the cook could have produced: validation
// must find nothing wrong with it at any size. This doubles as the first direct
// coverage of the dock and link endpoint rules over well-formed reciprocal
// pairs, which the manifest suite only ever exercised through legacy
// transitions.
TEST(ZoneTopologyScaling, GeneratedTopologiesValidateCleanlyAtEveryScale)
{
    for (int zoneCount : kScales)
    {
        for (const TopologySpec& spec : ShapesAt(zoneCount))
        {
            const WorldPartitionManifest manifest = BuildTopology(spec);
            const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
            const std::vector<ContentRiskRecord> records =
                ValidateWorldPartitionManifest(manifest, index);

            for (const ContentRiskRecord& record : records)
            {
                EXPECT_NE(record.Severity, ContentRiskSeverity::Error)
                    << Describe(spec) << " produced error " << record.RuleId << ": "
                    << record.Message;
            }
        }
    }
}

// Adjacency is derived data; at scale it must still describe exactly the edges
// the manifest carries, for every zone rather than for a sampled few.
TEST(ZoneTopologyScaling, IndexAdjacencyMatchesTheManifestAtEveryScale)
{
    for (int zoneCount : kScales)
    {
        for (const TopologySpec& spec : ShapesAt(zoneCount))
        {
            const WorldPartitionManifest manifest = BuildTopology(spec);
            const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);

            std::size_t dockEndpoints = 0;
            std::size_t linkEndpoints = 0;
            for (const ZoneHeader& zone : manifest.Zones)
            {
                ASSERT_TRUE(index.ContainsZone(zone.Id)) << Describe(spec);
                EXPECT_EQ(index.DocksFrom(zone.Id).size(), zone.Docks.size())
                    << Describe(spec) << " zone " << zone.Name;
                EXPECT_EQ(index.LinksFrom(zone.Id).size(), zone.Links.size())
                    << Describe(spec) << " zone " << zone.Name;
                dockEndpoints += zone.Docks.size();
                linkEndpoints += zone.Links.size();
            }

            // Every edge contributes exactly two endpoints, one per side.
            const std::size_t edges = (dockEndpoints + linkEndpoints) / 2;
            EXPECT_EQ(dockEndpoints % 2, 0u) << Describe(spec);
            EXPECT_EQ(linkEndpoints % 2, 0u) << Describe(spec);
            EXPECT_EQ(edges, static_cast<std::size_t>(ExpectedEdgeCount(spec)))
                << Describe(spec);
        }
    }
}

// The bound that matters for frame cost: what one focus demands is a property
// of its neighbourhood and the cap, not of how much world exists behind it.
TEST(ZoneTopologyScaling, DemandSetSizeDoesNotGrowWithWorldSize)
{
    WorldPartitionStreamingConfig config;
    config.HopCount = 1;
    config.ResidentZoneCap = 8;
    config.Radius = 0.0;

    for (TopologyShape shape : { TopologyShape::Chain, TopologyShape::Grid })
    {
        std::size_t previous = 0;
        for (int zoneCount : kScales)
        {
            const TopologySpec spec{ .Shape = shape, .ZoneCount = zoneCount };
            const WorldPartitionManifest manifest = BuildTopology(spec);
            const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);

            // A focus in the interior, so it has its full complement of
            // neighbours and the smallest world in the sweep is not measuring a
            // boundary case the larger ones do not have.
            const ZoneId focus = StressZoneAt(zoneCount / 2);
            const std::vector<ZoneDemandRecord> demand =
                ComputeZoneDemand(manifest, index, focus, {}, config);

            EXPECT_LE(demand.size(), static_cast<std::size_t>(config.ResidentZoneCap))
                << ShapeName(shape) << " x" << zoneCount << " exceeded the cap";
            if (previous != 0)
            {
                EXPECT_EQ(demand.size(), previous)
                    << ShapeName(shape) << " demand changed between scales";
            }
            previous = demand.size();

            for (const ZoneDemandRecord& record : demand)
                EXPECT_FALSE(record.Reasons.empty())
                    << "a published record carried no reason";
        }
    }
}

// Hop ranks are the BFS behind demand; the same independence must hold there,
// otherwise a cap would be hiding unbounded traversal work upstream of it.
TEST(ZoneTopologyScaling, HopRankCountDependsOnHopsNotOnWorldSize)
{
    for (int hopCount : { 1, 2 })
    {
        std::size_t previous = 0;
        for (int zoneCount : kScales)
        {
            const TopologySpec spec{ .Shape = TopologyShape::Chain,
                                     .ZoneCount = zoneCount };
            const WorldPartitionManifest manifest = BuildTopology(spec);
            const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);

            const std::vector<ZoneHopRank> ranks = ComputeZoneHopRanks(
                manifest, index, StressZoneAt(zoneCount / 2), hopCount);

            // A chain interior reaches hopCount zones either way, plus the focus.
            EXPECT_EQ(ranks.size(), static_cast<std::size_t>(hopCount * 2 + 1))
                << "chain x" << zoneCount << " at " << hopCount << " hops";
            if (previous != 0)
            {
                EXPECT_EQ(ranks.size(), previous);
            }
            previous = ranks.size();
        }
    }
}

// Serialization is the cook's handoff to the runtime. A topology that survives
// a write and a read is the precondition for anything the runtime concludes
// from a cooked manifest being what the cook meant.
TEST(ZoneTopologyScaling, ManifestSurvivesWriteAndReadAtEveryScale)
{
    for (int zoneCount : kScales)
    {
        for (const TopologySpec& spec : ShapesAt(zoneCount))
        {
            const WorldPartitionManifest manifest = BuildTopology(spec);

            const JsonValue written = WriteWorldPartitionManifest(manifest);
            std::string error;
            const std::optional<WorldPartitionManifest> read =
                ReadWorldPartitionManifest(written, &error);

            ASSERT_TRUE(read.has_value())
                << Describe(spec) << " failed to read back: " << error;
            EXPECT_EQ(*read, manifest) << Describe(spec) << " changed across a round trip";
        }
    }
}
