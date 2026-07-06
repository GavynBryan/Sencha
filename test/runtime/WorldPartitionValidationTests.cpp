#include <gtest/gtest.h>

#include <zone/WorldPartitionValidation.h>

#include <algorithm>

namespace
{

constexpr uint64_t RegionB1 = 0xb1;
constexpr uint64_t ZoneA1 = 0xa1;
constexpr uint64_t ZoneA2 = 0xa2;
constexpr uint64_t ZoneA3 = 0xa3;

ZoneHeader MakeZone(uint64_t id, std::string name, Aabb3d bounds)
{
    ZoneHeader zone;
    zone.Id = ZoneId{ id };
    zone.Name = std::move(name);
    zone.Region = RegionId{ RegionB1 };
    zone.SceneRef = "levels/" + zone.Name + ".level.json";
    zone.Bounds = bounds;
    return zone;
}

TransitionRecord MakeTransition(uint64_t id, uint64_t from, uint64_t to,
                                TransitionTopology topology = TransitionTopology::Doorway)
{
    TransitionRecord transition;
    transition.Id = TransitionId{ id };
    transition.From = ZoneId{ from };
    transition.To = ZoneId{ to };
    transition.Topology = topology;
    return transition;
}

// Internally consistent three-zone world: no rule fires. Adjacent zones sit
// face-to-face (east kisses hub at x=8, north kisses hub at z=8); a shared
// boundary is contact, not overlap, so the overlap rule stays silent.
WorldPartitionManifest MakeCleanManifest()
{
    WorldPartitionManifest manifest;
    manifest.Name = "TestWorld";
    manifest.StartZone = ZoneId{ ZoneA1 };
    manifest.Regions = { RegionRecord{ RegionId{ RegionB1 }, "Ruins" } };
    manifest.Zones = {
        MakeZone(ZoneA1, "hub", Aabb3d{ { -8.0f, 0.0f, -8.0f }, { 8.0f, 4.0f, 8.0f } }),
        MakeZone(ZoneA2, "east", Aabb3d{ { 8.0f, 0.0f, -4.0f }, { 24.0f, 4.0f, 4.0f } }),
        MakeZone(ZoneA3, "north", Aabb3d{ { -8.0f, 0.0f, 8.0f }, { 8.0f, 4.0f, 20.0f } }),
    };
    manifest.Transitions = {
        MakeTransition(0xc1, ZoneA1, ZoneA2),
        MakeTransition(0xc2, ZoneA2, ZoneA1),
        MakeTransition(0xc3, ZoneA1, ZoneA3),
        MakeTransition(0xc4, ZoneA3, ZoneA1),
        MakeTransition(0xc5, ZoneA2, ZoneA3, TransitionTopology::Teleport),
    };
    return manifest;
}

std::vector<ContentRiskRecord> Validate(const WorldPartitionManifest& manifest)
{
    return ValidateWorldPartitionManifest(manifest, WorldPartitionIndex::Build(manifest));
}

void ExpectSingleRecord(const std::vector<ContentRiskRecord>& records,
                        const char* ruleId,
                        ContentRiskSeverity severity,
                        ContentRiskSourceKind kind,
                        uint64_t sourceId)
{
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].RuleId, ruleId);
    EXPECT_EQ(records[0].Severity, severity);
    EXPECT_EQ(records[0].Kind, kind);
    EXPECT_EQ(records[0].SourceId, sourceId);
    EXPECT_FALSE(records[0].Message.empty());
}

} // namespace

TEST(WorldPartitionValidation, CleanFixtureEmitsNothing)
{
    EXPECT_TRUE(Validate(MakeCleanManifest()).empty());
}

TEST(WorldPartitionValidation, DuplicateIdFires)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Regions.push_back(RegionRecord{ RegionId{ RegionB1 }, "Ruins Copy" });

    ExpectSingleRecord(Validate(manifest), "partition.id.duplicate",
                       ContentRiskSeverity::Error, ContentRiskSourceKind::Region, RegionB1);
}

TEST(WorldPartitionValidation, RegionMissingFires)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Zones[2].Region = RegionId{ 0xdead };

    ExpectSingleRecord(Validate(manifest), "partition.zone.region_missing",
                       ContentRiskSeverity::Error, ContentRiskSourceKind::Zone, ZoneA3);
}

TEST(WorldPartitionValidation, EndpointMissingFires)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Transitions[4].To = ZoneId{ 0xff };

    ExpectSingleRecord(Validate(manifest), "partition.transition.endpoint_missing",
                       ContentRiskSeverity::Error, ContentRiskSourceKind::Transition, 0xc5);
}

TEST(WorldPartitionValidation, SelfLoopFires)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Transitions[4].To = ZoneId{ ZoneA2 };

    ExpectSingleRecord(Validate(manifest), "partition.transition.self_loop",
                       ContentRiskSeverity::Error, ContentRiskSourceKind::Transition, 0xc5);
}

TEST(WorldPartitionValidation, UnpairedDoorwayFires)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Transitions.erase(manifest.Transitions.begin() + 1);

    ExpectSingleRecord(Validate(manifest), "partition.transition.unpaired",
                       ContentRiskSeverity::Warning, ContentRiskSourceKind::Transition, 0xc1);
}

TEST(WorldPartitionValidation, TeleportNeedsNoPair)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Transitions[0].Topology = TransitionTopology::Teleport;
    manifest.Transitions.erase(manifest.Transitions.begin() + 1);

    EXPECT_TRUE(Validate(manifest).empty());
}

TEST(WorldPartitionValidation, SceneMissingFires)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Zones[2].SceneRef.clear();

    ExpectSingleRecord(Validate(manifest), "partition.zone.scene_missing",
                       ContentRiskSeverity::Error, ContentRiskSourceKind::Zone, ZoneA3);
}

TEST(WorldPartitionValidation, BoundsInvalidFires)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Zones[2].Bounds = Aabb3d{ { 1.0f, 1.0f, 1.0f }, { -1.0f, -1.0f, -1.0f } };

    ExpectSingleRecord(Validate(manifest), "partition.zone.bounds_invalid",
                       ContentRiskSeverity::Error, ContentRiskSourceKind::Zone, ZoneA3);
}

TEST(WorldPartitionValidation, BoundsOverlapFiresOncePerPair)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Zones[1].Bounds = Aabb3d{ { 0.0f, 0.0f, -4.0f }, { 24.0f, 4.0f, 4.0f } };

    const auto records = Validate(manifest);
    ExpectSingleRecord(records, "partition.bounds.overlap",
                       ContentRiskSeverity::Warning, ContentRiskSourceKind::Zone, ZoneA1);
    // The message reads by authored name, not the opaque hex id.
    EXPECT_NE(records[0].Message.find("hub"), std::string::npos);
    EXPECT_NE(records[0].Message.find("east"), std::string::npos);
    EXPECT_EQ(records[0].Message.find("00000000000000a1"), std::string::npos);
}

TEST(WorldPartitionValidation, KissingFacesDoNotOverlap)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    // east's -x face lies exactly on hub's +x face (x = 8): contact, not overlap.
    manifest.Zones[1].Bounds = Aabb3d{ { 8.0f, 0.0f, -4.0f }, { 24.0f, 4.0f, 4.0f } };
    EXPECT_TRUE(Validate(manifest).empty());
}

TEST(WorldPartitionValidation, TouchingCornersDoNotOverlap)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    // east shares only the single corner (8, 4, 8) with hub's max corner: zero
    // overlap depth on every axis.
    manifest.Zones[1].Bounds = Aabb3d{ { 8.0f, 4.0f, 8.0f }, { 24.0f, 8.0f, 20.0f } };
    EXPECT_TRUE(Validate(manifest).empty());
}

TEST(WorldPartitionValidation, MessageFallsBackToHexWhenZoneUnnamed)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Zones[0].Name.clear();
    manifest.Zones[1].Bounds = Aabb3d{ { 0.0f, 0.0f, -4.0f }, { 24.0f, 4.0f, 4.0f } };

    const auto records = Validate(manifest);
    ExpectSingleRecord(records, "partition.bounds.overlap",
                       ContentRiskSeverity::Warning, ContentRiskSourceKind::Zone, ZoneA1);
    // Unnamed zone shows its hex id; the named partner still shows its name.
    EXPECT_NE(records[0].Message.find("00000000000000a1"), std::string::npos);
    EXPECT_NE(records[0].Message.find("east"), std::string::npos);
}

TEST(WorldPartitionValidation, UnreachableZoneFires)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    // Remove every transition touching the third zone.
    std::erase_if(manifest.Transitions, [](const TransitionRecord& transition)
                  { return transition.From == ZoneId{ ZoneA3 } || transition.To == ZoneId{ ZoneA3 }; });

    ExpectSingleRecord(Validate(manifest), "partition.graph.unreachable",
                       ContentRiskSeverity::Warning, ContentRiskSourceKind::Zone, ZoneA3);
}

TEST(WorldPartitionValidation, NoStartZoneFiresAndSuppressesReachability)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.StartZone = ZoneId{};
    std::erase_if(manifest.Transitions, [](const TransitionRecord& transition)
                  { return transition.From == ZoneId{ ZoneA3 } || transition.To == ZoneId{ ZoneA3 }; });

    ExpectSingleRecord(Validate(manifest), "partition.world.no_start_zone",
                       ContentRiskSeverity::Warning, ContentRiskSourceKind::World, 0);
}

TEST(WorldPartitionValidation, RecordsAreDeterministicallyOrdered)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Regions.push_back(RegionRecord{ RegionId{ RegionB1 }, "Ruins Copy" });
    manifest.Zones[2].SceneRef.clear();
    manifest.Zones[1].Bounds = Aabb3d{ { 0.0f, 0.0f, -4.0f }, { 24.0f, 4.0f, 4.0f } };

    const auto first = Validate(manifest);
    const auto second = Validate(manifest);

    ASSERT_EQ(first.size(), 3u);
    EXPECT_EQ(first[0].RuleId, "partition.id.duplicate");
    EXPECT_EQ(first[1].RuleId, "partition.zone.scene_missing");
    EXPECT_EQ(first[2].RuleId, "partition.bounds.overlap");

    ASSERT_EQ(second.size(), first.size());
    for (size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_EQ(first[i].RuleId, second[i].RuleId);
        EXPECT_EQ(first[i].SourceId, second[i].SourceId);
        EXPECT_EQ(first[i].Severity, second[i].Severity);
        EXPECT_EQ(first[i].Kind, second[i].Kind);
        EXPECT_EQ(first[i].Message, second[i].Message);
    }
}

namespace
{

constexpr uint64_t RegionB2 = 0xb2;
constexpr uint64_t ZoneA4 = 0xa4;
constexpr uint64_t ZoneA5 = 0xa5;

// The clean fixture plus a proximity-streamed region west of the hub: two
// field cells with no edge between them, connected (or not) to the graph by
// an entrance edge pair the test adds.
WorldPartitionManifest MakeGridRegionManifest()
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    RegionRecord grid{ RegionId{ RegionB2 }, "Fields" };
    grid.Streaming.Radius = 100.0;
    manifest.Regions.push_back(grid);

    ZoneHeader cellA = MakeZone(ZoneA4, "field_a",
                                Aabb3d{ { -24.0f, 0.0f, -8.0f }, { -8.0f, 4.0f, 8.0f } });
    ZoneHeader cellB = MakeZone(ZoneA5, "field_b",
                                Aabb3d{ { -40.0f, 0.0f, -8.0f }, { -24.0f, 4.0f, 8.0f } });
    cellA.Region = RegionId{ RegionB2 };
    cellB.Region = RegionId{ RegionB2 };
    manifest.Zones.push_back(cellA);
    manifest.Zones.push_back(cellB);
    return manifest;
}

} // namespace

TEST(WorldPartitionValidation, StreamingInvalidFiresPerBadField)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Regions[0].Streaming.HopCount = -1;
    manifest.Regions[0].Streaming.Radius = -5.0;
    manifest.Regions[0].Streaming.ResidentZoneCap = 0;

    const auto records = Validate(manifest);
    ASSERT_EQ(records.size(), 3u);
    for (const ContentRiskRecord& record : records)
    {
        EXPECT_EQ(record.RuleId, "partition.region.streaming_invalid");
        EXPECT_EQ(record.Severity, ContentRiskSeverity::Error);
        EXPECT_EQ(record.Kind, ContentRiskSourceKind::Region);
        EXPECT_EQ(record.SourceId, RegionB1);
        EXPECT_FALSE(record.Message.empty());
    }
}

TEST(WorldPartitionValidation, StreamingBoundaryValuesAreClean)
{
    WorldPartitionManifest manifest = MakeCleanManifest();
    manifest.Regions[0].Streaming.HopCount = 0;
    manifest.Regions[0].Streaming.Radius = 0.0;
    manifest.Regions[0].Streaming.ResidentZoneCap = 1;

    EXPECT_TRUE(Validate(manifest).empty());
}

TEST(WorldPartitionValidation, RadiusRegionCellsReachableThroughOneEntranceEdge)
{
    WorldPartitionManifest manifest = MakeGridRegionManifest();
    // One entrance pair into the region; field_b has no edge at all yet stays
    // quiet: an explicit-radius region streams by proximity, not edges.
    manifest.Transitions.push_back(MakeTransition(0xc6, ZoneA1, ZoneA4));
    manifest.Transitions.push_back(MakeTransition(0xc7, ZoneA4, ZoneA1));

    EXPECT_TRUE(Validate(manifest).empty());
}

TEST(WorldPartitionValidation, IslandRadiusRegionStillWarnsEveryCell)
{
    const WorldPartitionManifest manifest = MakeGridRegionManifest();

    const auto records = Validate(manifest);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].RuleId, "partition.graph.unreachable");
    EXPECT_EQ(records[0].SourceId, ZoneA4);
    EXPECT_EQ(records[1].RuleId, "partition.graph.unreachable");
    EXPECT_EQ(records[1].SourceId, ZoneA5);
}

TEST(WorldPartitionValidation, StartZoneInsideRadiusRegionReachesItsSiblings)
{
    WorldPartitionManifest manifest = MakeGridRegionManifest();
    manifest.StartZone = ZoneId{ ZoneA4 };
    // The graph region is now the island: no edge connects it to the fields.
    const auto records = Validate(manifest);
    ASSERT_EQ(records.size(), 3u);
    for (const ContentRiskRecord& record : records)
        EXPECT_EQ(record.RuleId, "partition.graph.unreachable");
    EXPECT_EQ(records[0].SourceId, ZoneA1);
    EXPECT_EQ(records[1].SourceId, ZoneA2);
    EXPECT_EQ(records[2].SourceId, ZoneA3);
}
