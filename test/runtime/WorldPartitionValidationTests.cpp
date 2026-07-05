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

// Internally consistent three-zone world: no rule fires. Zone bounds keep a
// gap because Aabb3d::Intersects is closed and touching boxes count as overlap.
WorldPartitionManifest MakeCleanManifest()
{
    WorldPartitionManifest manifest;
    manifest.Name = "TestWorld";
    manifest.StartZone = ZoneId{ ZoneA1 };
    manifest.Regions = { RegionRecord{ RegionId{ RegionB1 }, "Ruins" } };
    manifest.Zones = {
        MakeZone(ZoneA1, "hub", Aabb3d{ { -8.0f, 0.0f, -8.0f }, { 8.0f, 4.0f, 8.0f } }),
        MakeZone(ZoneA2, "east", Aabb3d{ { 9.0f, 0.0f, -4.0f }, { 24.0f, 4.0f, 4.0f } }),
        MakeZone(ZoneA3, "north", Aabb3d{ { -8.0f, 0.0f, 9.0f }, { 8.0f, 4.0f, 20.0f } }),
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
