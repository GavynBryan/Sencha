#include <gtest/gtest.h>

#include <zone/ZoneDemand.h>

#include <algorithm>
#include <initializer_list>
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

void SetZoneBounds(ZoneHeader& zone, Aabb3d bounds)
{
    zone.Bounds = bounds;
}

LinkEndpoint MakeEdge(uint64_t id, uint64_t from, uint64_t to)
{
    LinkEndpoint endpoint;
    endpoint.Id = LinkId{ id };
    endpoint.OwnerZone = ZoneId{ from };
    endpoint.OtherZone = ZoneId{ to };
    endpoint.Side = DockSide::A;
    endpoint.Directions = 1;
    return endpoint;
}

void SetEdges(WorldPartitionManifest& manifest, std::initializer_list<LinkEndpoint> endpoints)
{
    for (const LinkEndpoint& endpoint : endpoints)
    {
        const auto zone = std::find_if(manifest.Zones.begin(), manifest.Zones.end(),
                                       [&](const ZoneHeader& header)
                                       { return header.Id == endpoint.OwnerZone; });
        ASSERT_NE(zone, manifest.Zones.end());
        zone->Links.push_back(endpoint);
    }
}

// A -> B -> C -> D chain with endpoint pairs.
WorldPartitionManifest ChainManifest()
{
    WorldPartitionManifest manifest;
    manifest.Name = "Chain";
    manifest.Zones = { MakeZone(0xa1), MakeZone(0xa2), MakeZone(0xa3), MakeZone(0xa4) };
    SetEdges(manifest, {
        MakeEdge(0xc1, 0xa1, 0xa2), MakeEdge(0xc2, 0xa2, 0xa1),
        MakeEdge(0xc3, 0xa2, 0xa3), MakeEdge(0xc4, 0xa3, 0xa2),
        MakeEdge(0xc5, 0xa3, 0xa4), MakeEdge(0xc6, 0xa4, 0xa3),
    });
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

TEST(ZoneDemand, NeighborsPreloadVisibleByDefault)
{
    const auto records = Demand(ChainManifest(), ZoneId{ 0xa1 }, {},
                                WorldPartitionStreamingConfig{ .HopCount = 2 });

    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(FindRecord(records, 0xa4), nullptr);   // hop 3: out of range
    for (uint64_t id : { 0xa2ull, 0xa3ull })
    {
        const ZoneDemandRecord* record = FindRecord(records, id);
        ASSERT_NE(record, nullptr);
        // Render preload: visible with static collision, no logic or audio.
        EXPECT_TRUE(record->Desired.Visible);
        EXPECT_TRUE(record->Desired.Physics);
        EXPECT_FALSE(record->Desired.Logic);
        EXPECT_FALSE(record->Desired.Audio);
        EXPECT_TRUE(record->Sources.Neighbor);
        EXPECT_FALSE(record->Sources.Focus);
    }
}

TEST(ZoneDemand, NeighborConfigOffKeepsDormant)
{
    const auto records = Demand(ChainManifest(), ZoneId{ 0xa1 }, {},
                                WorldPartitionStreamingConfig{
                                    .HopCount = 1,
                                    .NeighborVisible = false,
                                    .NeighborPhysics = false });

    const ZoneDemandRecord* neighbor = FindRecord(records, 0xa2);
    ASSERT_NE(neighbor, nullptr);
    EXPECT_FALSE(neighbor->Desired.Any());
}

TEST(ZoneDemand, OneWayInboundEdgeDoesNotPreloadSource)
{
    WorldPartitionManifest manifest;
    manifest.Zones = { MakeZone(0xa1), MakeZone(0xa2) };
    // One directed edge INTO the focus; no outgoing edge from it.
    SetEdges(manifest, { MakeEdge(0xc1, 0xa2, 0xa1) });

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
    EXPECT_TRUE(near->Desired.Audio);       // the pin's minimum, OR-ed on
    EXPECT_TRUE(near->Desired.Visible);     // the neighbor render preload
}

TEST(ZoneDemand, CapEvictsByHopThenDerivedCostThenId)
{
    // Graph hops have equal derived cost, so ties are deterministic by id.
    WorldPartitionManifest manifest;
    manifest.Zones = { MakeZone(0xa1), MakeZone(0xa2), MakeZone(0xa3), MakeZone(0xa4),
                       MakeZone(0xa5) };
    SetEdges(manifest, {
        MakeEdge(0xc1, 0xa1, 0xa2),
        MakeEdge(0xc2, 0xa1, 0xa3),
        MakeEdge(0xc3, 0xa2, 0xa4),
        MakeEdge(0xc4, 0xa3, 0xa5),
    });

    // Cap 4: one eviction, deepest hop and highest id first: 0xa5.
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

    // Cap 2: the hop-1 pair ties on hop and cost; higher id evicts first.
    records = Demand(manifest, ZoneId{ 0xa1 }, {},
                     WorldPartitionStreamingConfig{ .HopCount = 2, .ResidentZoneCap = 2 });
    ASSERT_EQ(records.size(), 2u);
    EXPECT_NE(FindRecord(records, 0xa2), nullptr);
    EXPECT_EQ(FindRecord(records, 0xa3), nullptr);

    // Equal hop and cost: higher id evicts first.
    WorldPartitionManifest tie;
    tie.Zones = { MakeZone(0xa1), MakeZone(0xa2), MakeZone(0xa3) };
    SetEdges(tie, { MakeEdge(0xc1, 0xa1, 0xa2), MakeEdge(0xc2, 0xa1, 0xa3) });
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
    SetEdges(manifest, { MakeEdge(0xc1, 0xa5, 0xa9), MakeEdge(0xc2, 0xa5, 0xa1) });

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

TEST(ZoneDemand, ResolveFocusZonePrefersContainmentWithHysteresis)
{
    WorldPartitionManifest manifest;
    ZoneHeader big = MakeZone(0xa1);
    SetZoneBounds(big, Aabb3d::FromMinMax(Vec3d{ -10, 0, -10 }, Vec3d{ 10, 4, 10 }));
    ZoneHeader small = MakeZone(0xa2);
    SetZoneBounds(small, Aabb3d::FromMinMax(Vec3d{ 0, 0, 0 }, Vec3d{ 4, 4, 4 }));
    manifest.Zones = { big, small };

    // No previous focus: the smallest containing volume wins.
    EXPECT_EQ(ResolveFocusZone(manifest, Vec3d{ 2, 1, 2 }, ZoneId{}), ZoneId{ 0xa2 });
    // Hysteresis: the previous focus holds while it still contains the point.
    EXPECT_EQ(ResolveFocusZone(manifest, Vec3d{ 2, 1, 2 }, ZoneId{ 0xa1 }), ZoneId{ 0xa1 });
    // A point in no zone resolves to the nearest bounds, not the previous
    // focus: (100, 0, 0) is 90 from big's box, 96 from small's.
    EXPECT_EQ(ResolveFocusZone(manifest, Vec3d{ 100, 0, 0 }, ZoneId{ 0xa2 }), ZoneId{ 0xa1 });
}

TEST(ZoneDemand, ResolveFocusZoneFallsToNearestOutsideAllBounds)
{
    // Derived bounds hug authored geometry: two floor slabs one unit tall,
    // the shape a cook derives for content-only cells. A pawn whose transform
    // rides above the slab must still focus the cell it stands on.
    WorldPartitionManifest manifest;
    ZoneHeader west = MakeZone(0xa1);
    SetZoneBounds(west, Aabb3d::FromMinMax(Vec3d{ 0, 0, 0 }, Vec3d{ 10, 1, 10 }));
    ZoneHeader east = MakeZone(0xa2);
    SetZoneBounds(east, Aabb3d::FromMinMax(Vec3d{ 10, 0, 0 }, Vec3d{ 20, 1, 10 }));
    manifest.Zones = { west, east };

    EXPECT_EQ(ResolveFocusZone(manifest, Vec3d{ 5, 2, 5 }, ZoneId{ 0xa2 }), ZoneId{ 0xa1 });
    EXPECT_EQ(ResolveFocusZone(manifest, Vec3d{ 15, 2, 5 }, ZoneId{ 0xa1 }), ZoneId{ 0xa2 });
    // Equidistant on the shared face: equal volumes, so the lower id wins.
    EXPECT_EQ(ResolveFocusZone(manifest, Vec3d{ 10, 2, 5 }, ZoneId{}), ZoneId{ 0xa1 });

    // Previous survives only when no zone has valid bounds.
    WorldPartitionManifest broken;
    ZoneHeader inverted = MakeZone(0xa3);
    inverted.Bounds = Aabb3d::FromMinMax(Vec3d{ 1, 1, 1 }, Vec3d{ -1, -1, -1 });
    broken.Zones = { inverted };
    EXPECT_EQ(ResolveFocusZone(broken, Vec3d{ 0, 0, 0 }, ZoneId{ 0xa7 }), ZoneId{ 0xa7 });
}

namespace
{

// A 1x3 strip of bounded zones with no endpoints: proximity is the only
// demand path.
WorldPartitionManifest StripManifest()
{
    WorldPartitionManifest manifest;
    ZoneHeader a = MakeZone(0xa1);
    SetZoneBounds(a, Aabb3d::FromMinMax(Vec3d{ 0, 0, 0 }, Vec3d{ 10, 4, 10 }));
    ZoneHeader b = MakeZone(0xa2);
    SetZoneBounds(b, Aabb3d::FromMinMax(Vec3d{ 10, 0, 0 }, Vec3d{ 20, 4, 10 }));
    ZoneHeader c = MakeZone(0xa3);
    SetZoneBounds(c, Aabb3d::FromMinMax(Vec3d{ 100, 0, 0 }, Vec3d{ 110, 4, 10 }));
    manifest.Zones = { a, b, c };
    return manifest;
}

} // namespace

TEST(ZoneDemand, ZonesWithinRadiusJoinDemand)
{
    const WorldPartitionManifest manifest = StripManifest();
    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    const Vec3d position{ 5, 1, 5 };

    // Radius 0: graph only, and there is no graph.
    auto records = ComputeZoneDemand(manifest, index, ZoneId{ 0xa1 }, {},
                                     WorldPartitionStreamingConfig{}, &position);
    ASSERT_EQ(records.size(), 1u);

    // Radius 8 reaches the adjacent strip cell (closest point at x=10, distance
    // 5) but not the far one (distance 95). Closest-point matters: b's CENTER
    // is 10+ away.
    records = ComputeZoneDemand(manifest, index, ZoneId{ 0xa1 }, {},
                                WorldPartitionStreamingConfig{ .Radius = 8.0 }, &position);
    ASSERT_EQ(records.size(), 2u);
    const ZoneDemandRecord* nearZone = FindRecord(records, 0xa2);
    ASSERT_NE(nearZone, nullptr);
    EXPECT_TRUE(nearZone->Sources.Spatial);
    EXPECT_FALSE(nearZone->Sources.Neighbor);
    EXPECT_TRUE(nearZone->Desired.Visible);
    EXPECT_EQ(FindRecord(records, 0xa3), nullptr);

    // No position supplied: spatial demand is inert regardless of radius.
    records = ComputeZoneDemand(manifest, index, ZoneId{ 0xa1 }, {},
                                WorldPartitionStreamingConfig{ .Radius = 8.0 });
    ASSERT_EQ(records.size(), 1u);
}

TEST(ZoneDemand, SpatialEvictsAfterGraphNeighbors)
{
    // One graph neighbor plus one spatial-only zone, cap 2: the spatial zone
    // (ranked one hop past the horizon) evicts first.
    WorldPartitionManifest manifest = StripManifest();
    SetEdges(manifest, { MakeEdge(0xc1, 0xa1, 0xa3) });
    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    const Vec3d position{ 5, 1, 5 };

    const auto records = ComputeZoneDemand(manifest, index, ZoneId{ 0xa1 }, {},
                                           WorldPartitionStreamingConfig{
                                               .ResidentZoneCap = 2, .Radius = 8.0 },
                                           &position);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_NE(FindRecord(records, 0xa3), nullptr);   // the graph neighbor survives
    EXPECT_EQ(FindRecord(records, 0xa2), nullptr);   // the spatial zone evicts
}

TEST(ZoneDemand, ClosedGateDoesNotEraseTopologyOrResidencyDemand)
{
    // Gate state is a traversal concern stored outside topology. Demand sees
    // the connection and keeps the destination ready even while a gate is closed.
    const WorldPartitionManifest manifest = ChainManifest();
    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    const auto records = ComputeZoneDemand(manifest, index, ZoneId{ 0xa1 }, {},
                                           WorldPartitionStreamingConfig{ .HopCount = 1 });
    ASSERT_EQ(records.size(), 2u);
    EXPECT_NE(FindRecord(records, 0xa2), nullptr);
}

TEST(ZoneDemand, GraphHopPolicyAloneControlsNeighborhoodDepth)
{
    const WorldPartitionManifest manifest = ChainManifest();
    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    auto records = ComputeZoneDemand(manifest, index, ZoneId{ 0xa1 }, {},
                                     WorldPartitionStreamingConfig{ .HopCount = 1 });
    EXPECT_NE(FindRecord(records, 0xa2), nullptr);
    EXPECT_EQ(FindRecord(records, 0xa3), nullptr);

    records = ComputeZoneDemand(manifest, index, ZoneId{ 0xa1 }, {},
                                WorldPartitionStreamingConfig{ .HopCount = 3 });
    EXPECT_NE(FindRecord(records, 0xa4), nullptr);
}

TEST(ZoneDemand, ResolverOverridesPresentFieldsAndInheritsAbsent)
{
    WorldPartitionManifest manifest = ChainManifest();
    GraphRecord graph{ GraphId{ 0xb1 }, "Fields" };
    graph.Streaming.HopCount = 3;
    graph.Streaming.ResidentZoneCap = 16;
    manifest.Graphs.push_back(graph);
    manifest.Zones[0].Graph = GraphId{ 0xb1 };

    WorldPartitionStreamingConfig base;
    base.HopCount = 1;
    base.Radius = 50.0;
    base.ResidentZoneCap = 8;
    base.LingerSeconds = 7.0;

    const auto resolved = ResolveGraphStreamingConfig(manifest, ZoneId{ 0xa1 }, base);
    EXPECT_EQ(resolved.HopCount, 3);
    EXPECT_EQ(resolved.Radius, 50.0);
    EXPECT_EQ(resolved.ResidentZoneCap, 16);
    EXPECT_EQ(resolved.LingerSeconds, 7.0);
}

TEST(ZoneDemand, ResolverReturnsBaseForUnknownOrGraphlessFocus)
{
    WorldPartitionManifest manifest = ChainManifest();
    GraphRecord graph{ GraphId{ 0xb1 }, "Fields" };
    graph.Streaming.HopCount = 3;
    manifest.Graphs.push_back(graph);
    manifest.Zones[0].Graph = GraphId{ 0xb1 };

    const WorldPartitionStreamingConfig base;

    // Focus outside the manifest entirely.
    EXPECT_EQ(ResolveGraphStreamingConfig(manifest, ZoneId{ 0xff }, base), base);
    // Focus in a zone whose graph reference resolves to no graph record.
    EXPECT_EQ(ResolveGraphStreamingConfig(manifest, ZoneId{ 0xa2 }, base), base);
    // Invalid focus.
    EXPECT_EQ(ResolveGraphStreamingConfig(manifest, ZoneId{}, base), base);
}

TEST(ZoneDemand, CrossGraphDockSeedsDestinationGraphPolicy)
{
    WorldPartitionManifest manifest;
    manifest.Graphs = { GraphRecord{ .Id = GraphId{ 0xb1 }, .Name = "Rooms" },
                        GraphRecord{ .Id = GraphId{ 0xb2 }, .Name = "Exterior" } };
    manifest.Graphs[1].Streaming.HopCount = 1;
    ZoneHeader a = MakeZone(0xa1);
    ZoneHeader b = MakeZone(0xa2);
    ZoneHeader c = MakeZone(0xa3);
    a.Graph = manifest.Graphs[0].Id;
    b.Graph = manifest.Graphs[1].Id;
    c.Graph = manifest.Graphs[1].Id;
    DockEndpoint endpoint{
        .Id = DockId{ 0xd1 },
        .OwnerZone = a.Id,
        .OtherZone = b.Id,
        .Side = DockSide::A,
    };
    a.Docks.push_back(endpoint);
    endpoint.OwnerZone = b.Id;
    endpoint.OtherZone = a.Id;
    endpoint.Side = DockSide::B;
    endpoint.Normal = -endpoint.Normal;
    endpoint.Right = -endpoint.Right;
    b.Docks.push_back(endpoint);
    b.Links.push_back(MakeEdge(0xc1, b.Id.Value, c.Id.Value));
    manifest.Zones = { a, b, c };

    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    const Vec3d focusPosition{};
    const auto records = ComputeZoneDemand(
        manifest, index, a.Id, {}, WorldPartitionStreamingConfig{}, &focusPosition);
    const ZoneDemandRecord* entry = FindRecord(records, b.Id.Value);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->Sources.CrossGraphEntry);
    EXPECT_NE(FindRecord(records, c.Id.Value), nullptr);
    EXPECT_TRUE(std::any_of(entry->Reasons.begin(), entry->Reasons.end(),
                            [](const ZoneDemandReasonRecord& reason)
                            {
                                return reason.Reason == ZoneDemandReason::CrossGraphEntry
                                    && reason.SourceEndpoint == 0xd1;
                            }));
}
