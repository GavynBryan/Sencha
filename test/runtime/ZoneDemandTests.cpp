#include <gtest/gtest.h>

#include <zone/ZoneDemand.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <span>
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
    ASSERT_EQ(records[0].Reasons.size(), 1u);
    EXPECT_EQ(records[0].Reasons[0].Reason, ZoneDemandReason::Focus);
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
        ASSERT_EQ(record->Reasons.size(), 1u);
        EXPECT_EQ(record->Reasons[0].Reason, ZoneDemandReason::SameGraphHop);
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
    ASSERT_EQ(far->Reasons.size(), 1u);
    EXPECT_EQ(far->Reasons[0].Reason, ZoneDemandReason::ExplicitPin);
    EXPECT_TRUE(far->Desired.Logic);
    EXPECT_FALSE(far->Desired.Visible);

    const ZoneDemandRecord* near = FindRecord(records, 0xa2);
    ASSERT_NE(near, nullptr);
    EXPECT_TRUE(IsDemandedFor(*near, ZoneDemandReason::ExplicitPin));
    EXPECT_TRUE(IsDemandedFor(*near, ZoneDemandReason::SameGraphHop));
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
    ASSERT_EQ(nearZone->Reasons.size(), 1u);
    EXPECT_EQ(nearZone->Reasons[0].Reason, ZoneDemandReason::SpatialRadius);
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
    EXPECT_NE(FindRecord(records, c.Id.Value), nullptr);
    EXPECT_TRUE(std::any_of(entry->Reasons.begin(), entry->Reasons.end(),
                            [](const ZoneDemandReasonRecord& reason)
                            {
                                return reason.Reason == ZoneDemandReason::CrossGraphEntry
                                    && reason.SourceEndpoint == 0xd1;
                            }));
}

//=============================================================================
// Several focus sources at once
//
// An authority streams around every connected player, so its residency is the
// union of their neighborhoods rather than one player's. The merge is the whole
// contract, and these are the parts of it that are easy to get subtly wrong: a
// zone near one player and far from another must be treated as near, a zone
// somebody is standing in must survive the cap, and one source must go on
// producing exactly what it produced before any of this existed.
//=============================================================================

namespace
{
    std::vector<ZoneDemandRecord> DemandFor(
        const WorldPartitionManifest& manifest,
        std::span<const ZoneFocusSource> sources,
        WorldPartitionStreamingConfig config)
    {
        const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
        return ComputeZoneDemand(manifest, index, sources, {}, config);
    }

    ZoneFocusSource At(uint32_t source, uint64_t zone)
    {
        return ZoneFocusSource{ FocusSourceId{ source }, ZoneId{ zone }, std::nullopt };
    }

    // A1 -> A2 -> A3 -> A4 -> A5, bilateral. Long enough that a middle zone is
    // genuinely far from both ends, which is what the merge has to be able to
    // tell apart from a zone that is next door to one of them.
    WorldPartitionManifest LongChainManifest()
    {
        WorldPartitionManifest manifest;
        manifest.Name = "LongChain";
        manifest.Zones = { MakeZone(0xa1), MakeZone(0xa2), MakeZone(0xa3),
                           MakeZone(0xa4), MakeZone(0xa5) };
        SetEdges(manifest, {
            MakeEdge(0xc1, 0xa1, 0xa2), MakeEdge(0xc2, 0xa2, 0xa1),
            MakeEdge(0xc3, 0xa2, 0xa3), MakeEdge(0xc4, 0xa3, 0xa2),
            MakeEdge(0xc5, 0xa3, 0xa4), MakeEdge(0xc6, 0xa4, 0xa3),
            MakeEdge(0xc7, 0xa4, 0xa5), MakeEdge(0xc8, 0xa5, 0xa4),
        });
        return manifest;
    }
}

TEST(ZoneDemandSources, SingleSourceIsByteIdenticalToToday)
{
    const WorldPartitionManifest manifest = ChainManifest();
    WorldPartitionStreamingConfig config;
    config.HopCount = 2;

    const std::vector<ZoneDemandRecord> single =
        Demand(manifest, ZoneId{ 0xa1 }, {}, config);
    const ZoneFocusSource one = At(1, 0xa1);
    const std::vector<ZoneDemandRecord> spanned =
        DemandFor(manifest, std::span(&one, 1), config);

    ASSERT_EQ(single.size(), spanned.size());
    for (std::size_t i = 0; i < single.size(); ++i)
    {
        EXPECT_EQ(single[i].Zone, spanned[i].Zone);
        EXPECT_EQ(single[i].Desired, spanned[i].Desired);
        // Reasons too, in accumulation order: a record that agreed about
        // participation and disagreed about why would still change what the
        // streaming inspector shows and what eviction ranks on.
        EXPECT_EQ(single[i].Reasons, spanned[i].Reasons)
            << "zone " << single[i].Zone.Value;
    }
}

// A zone one hop from somebody is one hop away, not as far as the furthest
// player who can also see it.
//
// Asserted through eviction because that is the only place the merged rank is
// observable: a published record carries participation and reasons, and the
// reasons carry every source's rank, so reading them back cannot tell which one
// the merge chose. Eviction is what the rank is *for*, and a first version of
// this test that read the reasons passed against a merge taking the maximum.
//
// Five zones, players at both ends, room for one non-focus zone to be dropped.
// By minimum the middle zone is the far one and goes; by maximum the two zones
// next door to a player rank as the far ones and one of them goes instead.
TEST(ZoneDemandSources, TwoSourcesMergeByMinimumHop)
{
    const WorldPartitionManifest manifest = LongChainManifest();
    WorldPartitionStreamingConfig config;
    config.HopCount = 4;
    config.ResidentZoneCap = 4;

    const std::array<ZoneFocusSource, 2> sources{ At(1, 0xa1), At(2, 0xa5) };
    const std::vector<ZoneDemandRecord> records =
        DemandFor(manifest, sources, config);

    ASSERT_EQ(records.size(), 4u);
    EXPECT_EQ(FindRecord(records, 0xa3), nullptr)
        << "the zone furthest from every player survived the cap";
    EXPECT_NE(FindRecord(records, 0xa2), nullptr)
        << "a zone next door to a player was ranked by how far the other player "
           "is from it, and evicted for it";
    EXPECT_NE(FindRecord(records, 0xa4), nullptr)
        << "a zone next door to a player was ranked by how far the other player "
           "is from it, and evicted for it";
}

// A player standing in a zone the cap would otherwise drop is a player the
// authority stops simulating around, which is the failure this immunity exists
// to make impossible.
TEST(ZoneDemandSources, EachSourceFocusIsFullAndUnevictable)
{
    const WorldPartitionManifest manifest = ChainManifest();
    WorldPartitionStreamingConfig config;
    config.HopCount = 3;
    // Tighter than the four zones two players between them demand.
    config.ResidentZoneCap = 2;

    const std::array<ZoneFocusSource, 2> sources{ At(1, 0xa1), At(2, 0xa4) };
    const std::vector<ZoneDemandRecord> records =
        DemandFor(manifest, sources, config);

    const ZoneDemandRecord* first = FindRecord(records, 0xa1);
    const ZoneDemandRecord* second = FindRecord(records, 0xa4);
    ASSERT_NE(first, nullptr) << "the cap evicted a zone a player is standing in";
    ASSERT_NE(second, nullptr) << "the cap evicted a zone a player is standing in";

    for (const ZoneDemandRecord* focus : { first, second })
    {
        EXPECT_TRUE(focus->Desired.Visible);
        EXPECT_TRUE(focus->Desired.Physics);
        EXPECT_TRUE(focus->Desired.Logic)
            << "a focus zone without Logic is a player the authority does not "
               "simulate around";
        EXPECT_TRUE(focus->Desired.Audio);
    }
}

// Peers connect in whatever order they connect in. If that leaked into the
// merge, two servers running the same session would hold different zones.
TEST(ZoneDemandSources, MergedEvictionIsDeterministic)
{
    const WorldPartitionManifest manifest = ChainManifest();
    WorldPartitionStreamingConfig config;
    config.HopCount = 3;
    config.ResidentZoneCap = 3;

    const std::array<ZoneFocusSource, 2> forward{ At(1, 0xa1), At(2, 0xa4) };
    std::array<ZoneFocusSource, 2> reversed{ At(1, 0xa4), At(2, 0xa1) };

    const std::vector<ZoneDemandRecord> a = DemandFor(manifest, forward, config);
    const std::vector<ZoneDemandRecord> b = DemandFor(manifest, reversed, config);

    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(a[i].Zone, b[i].Zone) << "at " << i;
        EXPECT_EQ(a[i].Desired, b[i].Desired) << "at " << i;
    }
}

// A source that has not resolved a focus yet contributes nothing rather than
// voiding the sources that have. Before a peer's pawn exists there is nothing
// to stream around it, and that must not stop the authority streaming around
// everybody else.
TEST(ZoneDemandSources, AnUnresolvedSourceDoesNotSilenceTheOthers)
{
    const WorldPartitionManifest manifest = ChainManifest();
    WorldPartitionStreamingConfig config;
    config.HopCount = 1;

    const std::array<ZoneFocusSource, 2> sources{
        At(1, 0xa1), At(2, 0x0),  // the second has no zone yet
    };
    const std::vector<ZoneDemandRecord> records =
        DemandFor(manifest, sources, config);

    EXPECT_NE(FindRecord(records, 0xa1), nullptr)
        << "one source with nothing to say silenced a source that had something";
}
