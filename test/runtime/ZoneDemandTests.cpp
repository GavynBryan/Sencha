#include <gtest/gtest.h>

#include <zone/ZoneDemand.h>

#include <vector>

namespace
{

ZoneHeader MakeZone(uint64_t id)
{
    ZoneHeader header;
    header.Id = ZoneId{ id };
    header.Name = "Zone";
    return header;
}

TransitionRecord MakeEdge(uint64_t id, uint64_t from, uint64_t to, int32_t priority = 0)
{
    TransitionRecord record;
    record.Id = TransitionId{ id };
    record.From = ZoneId{ from };
    record.To = ZoneId{ to };
    record.PreloadPriority = priority;
    return record;
}

// A -> B -> C -> D chain with paired edges (every doorway is two directed
// records, the authored shape).
WorldPartitionManifest ChainManifest()
{
    WorldPartitionManifest manifest;
    manifest.Name = "Chain";
    manifest.Zones = { MakeZone(0xa1), MakeZone(0xa2), MakeZone(0xa3), MakeZone(0xa4) };
    manifest.Transitions = {
        MakeEdge(0xc1, 0xa1, 0xa2), MakeEdge(0xc2, 0xa2, 0xa1),
        MakeEdge(0xc3, 0xa2, 0xa3), MakeEdge(0xc4, 0xa3, 0xa2),
        MakeEdge(0xc5, 0xa3, 0xa4), MakeEdge(0xc6, 0xa4, 0xa3),
    };
    return manifest;
}

std::vector<ZoneDemandRecord> Demand(const WorldPartitionManifest& manifest, ZoneId focus,
                                     std::span<const ZonePin> pins,
                                     WorldPartitionStreamingConfig config)
{
    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    return ComputeZoneDemand(manifest, index, focus, pins, config);
}

const ZoneDemandRecord* FindRecord(const std::vector<ZoneDemandRecord>& records, uint64_t id)
{
    for (const ZoneDemandRecord& record : records)
        if (record.Zone == ZoneId{ id })
            return &record;
    return nullptr;
}

} // namespace

TEST(ZoneDemand, FocusAloneIsFullParticipation)
{
    WorldPartitionManifest manifest;
    manifest.Zones = { MakeZone(0xa1) };

    const auto records = Demand(manifest, ZoneId{ 0xa1 }, {}, {});

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Zone, ZoneId{ 0xa1 });
    EXPECT_TRUE(records[0].Desired.Visible);
    EXPECT_TRUE(records[0].Desired.Physics);
    EXPECT_TRUE(records[0].Desired.Logic);
    EXPECT_TRUE(records[0].Desired.Audio);
    EXPECT_TRUE(records[0].Sources.Focus);
    EXPECT_FALSE(records[0].Sources.Neighbor);
}

TEST(ZoneDemand, NeighborsWithinHopCountAreDormant)
{
    const auto records = Demand(ChainManifest(), ZoneId{ 0xa1 }, {},
                                WorldPartitionStreamingConfig{ .HopCount = 2 });

    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(FindRecord(records, 0xa4), nullptr);   // hop 3: out of range
    for (uint64_t id : { 0xa2ull, 0xa3ull })
    {
        const ZoneDemandRecord* record = FindRecord(records, id);
        ASSERT_NE(record, nullptr);
        EXPECT_FALSE(record->Desired.Any());   // dormant
        EXPECT_TRUE(record->Sources.Neighbor);
        EXPECT_FALSE(record->Sources.Focus);
    }
}

TEST(ZoneDemand, OneWayInboundEdgeDoesNotPreloadSource)
{
    WorldPartitionManifest manifest;
    manifest.Zones = { MakeZone(0xa1), MakeZone(0xa2) };
    // One directed edge INTO the focus; no outgoing edge from it.
    manifest.Transitions = { MakeEdge(0xc1, 0xa2, 0xa1) };

    const auto records = Demand(manifest, ZoneId{ 0xa1 }, {},
                                WorldPartitionStreamingConfig{ .HopCount = 1 });

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].Zone, ZoneId{ 0xa1 });
}

TEST(ZoneDemand, PinnedZoneCarriesItsMinimum)
{
    const ZonePin pins[] = {
        // Beyond hop range: appears with exactly its minimum.
        { ZoneId{ 0xa4 }, ZoneParticipation{ .Logic = true } },
        // On a hop-1 neighbor: ORs onto the dormant neighbor demand.
        { ZoneId{ 0xa2 }, ZoneParticipation{ .Audio = true } },
    };
    const auto records = Demand(ChainManifest(), ZoneId{ 0xa1 }, pins,
                                WorldPartitionStreamingConfig{ .HopCount = 1 });

    const ZoneDemandRecord* far = FindRecord(records, 0xa4);
    ASSERT_NE(far, nullptr);
    EXPECT_TRUE(far->Sources.Pinned);
    EXPECT_FALSE(far->Sources.Neighbor);
    EXPECT_TRUE(far->Desired.Logic);
    EXPECT_FALSE(far->Desired.Visible);

    const ZoneDemandRecord* near = FindRecord(records, 0xa2);
    ASSERT_NE(near, nullptr);
    EXPECT_TRUE(near->Sources.Pinned);
    EXPECT_TRUE(near->Sources.Neighbor);
    EXPECT_TRUE(near->Desired.Audio);
    EXPECT_FALSE(near->Desired.Visible);
}

TEST(ZoneDemand, CapEvictsByHopThenPriorityThenId)
{
    // Focus 0xa1; hop-1 neighbors 0xa2 (priority 5) and 0xa3 (priority 1);
    // hop-2 zones 0xa4 (via 0xa2, priority 2) and 0xa5 (via 0xa3, priority 0).
    WorldPartitionManifest manifest;
    manifest.Zones = { MakeZone(0xa1), MakeZone(0xa2), MakeZone(0xa3), MakeZone(0xa4),
                       MakeZone(0xa5) };
    manifest.Transitions = {
        MakeEdge(0xc1, 0xa1, 0xa2, 5),
        MakeEdge(0xc2, 0xa1, 0xa3, 1),
        MakeEdge(0xc3, 0xa2, 0xa4, 2),
        MakeEdge(0xc4, 0xa3, 0xa5, 0),
    };

    // Cap 4: one eviction, deepest hop first, lowest priority within it: 0xa5.
    auto records = Demand(manifest, ZoneId{ 0xa1 }, {},
                          WorldPartitionStreamingConfig{ .HopCount = 2, .ResidentZoneCap = 4 });
    ASSERT_EQ(records.size(), 4u);
    EXPECT_EQ(FindRecord(records, 0xa5), nullptr);
    EXPECT_NE(FindRecord(records, 0xa4), nullptr);

    // Cap 3: both hop-2 zones evicted.
    records = Demand(manifest, ZoneId{ 0xa1 }, {},
                     WorldPartitionStreamingConfig{ .HopCount = 2, .ResidentZoneCap = 3 });
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(FindRecord(records, 0xa4), nullptr);
    EXPECT_EQ(FindRecord(records, 0xa5), nullptr);

    // Cap 2: the hop-1 pair ties on hop; priority 1 evicts before priority 5.
    records = Demand(manifest, ZoneId{ 0xa1 }, {},
                     WorldPartitionStreamingConfig{ .HopCount = 2, .ResidentZoneCap = 2 });
    ASSERT_EQ(records.size(), 2u);
    EXPECT_NE(FindRecord(records, 0xa2), nullptr);
    EXPECT_EQ(FindRecord(records, 0xa3), nullptr);

    // Equal hop and priority: higher id evicts first.
    WorldPartitionManifest tie;
    tie.Zones = { MakeZone(0xa1), MakeZone(0xa2), MakeZone(0xa3) };
    tie.Transitions = { MakeEdge(0xc1, 0xa1, 0xa2), MakeEdge(0xc2, 0xa1, 0xa3) };
    records = Demand(tie, ZoneId{ 0xa1 }, {},
                     WorldPartitionStreamingConfig{ .HopCount = 1, .ResidentZoneCap = 2 });
    ASSERT_EQ(records.size(), 2u);
    EXPECT_NE(FindRecord(records, 0xa2), nullptr);
    EXPECT_EQ(FindRecord(records, 0xa3), nullptr);
}

TEST(ZoneDemand, PinsAndFocusExceedCap)
{
    const ZonePin pins[] = {
        { ZoneId{ 0xa3 }, ZoneParticipation{ .Logic = true } },
        { ZoneId{ 0xa4 }, ZoneParticipation{ .Logic = true } },
    };
    const auto records = Demand(ChainManifest(), ZoneId{ 0xa1 }, pins,
                                WorldPartitionStreamingConfig{ .HopCount = 0,
                                                               .ResidentZoneCap = 1 });

    // Focus plus both pins survive a cap of one: pins are explicit demands.
    ASSERT_EQ(records.size(), 3u);
    EXPECT_NE(FindRecord(records, 0xa1), nullptr);
    EXPECT_NE(FindRecord(records, 0xa3), nullptr);
    EXPECT_NE(FindRecord(records, 0xa4), nullptr);
}

TEST(ZoneDemand, RecordsAscendByZoneId)
{
    // Manifest authored out of id order; the records still ascend.
    WorldPartitionManifest manifest;
    manifest.Zones = { MakeZone(0xa9), MakeZone(0xa1), MakeZone(0xa5) };
    manifest.Transitions = { MakeEdge(0xc1, 0xa5, 0xa9), MakeEdge(0xc2, 0xa5, 0xa1) };

    const auto records = Demand(manifest, ZoneId{ 0xa5 }, {},
                                WorldPartitionStreamingConfig{ .HopCount = 1 });

    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].Zone, ZoneId{ 0xa1 });
    EXPECT_EQ(records[1].Zone, ZoneId{ 0xa5 });
    EXPECT_EQ(records[2].Zone, ZoneId{ 0xa9 });
}

TEST(ZoneDemand, InvalidFocusYieldsEmptyDemand)
{
    EXPECT_TRUE(Demand(ChainManifest(), ZoneId{}, {}, {}).empty());
    EXPECT_TRUE(Demand(ChainManifest(), ZoneId{ 0xff }, {}, {}).empty());
}
