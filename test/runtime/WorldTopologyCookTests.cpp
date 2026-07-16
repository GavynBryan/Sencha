#include <gtest/gtest.h>

#include <zone/WorldTopologyCook.h>

namespace
{

WorldPartitionManifest MakeManifest()
{
    WorldPartitionManifest manifest;
    manifest.Graphs = {
        GraphRecord{ GraphId{ 0xb1 }, "Interior" },
        GraphRecord{ GraphId{ 0xb2 }, "Exterior" },
    };
    ZoneHeader a;
    a.Id = ZoneId{ 0xa1 };
    a.Graph = GraphId{ 0xb1 };
    ZoneHeader b;
    b.Id = ZoneId{ 0xa2 };
    b.Graph = GraphId{ 0xb2 };
    manifest.Zones = { a, b };
    return manifest;
}

WorldDock MakeDock()
{
    WorldDock dock;
    dock.Id = DockId{ 0xd1 };
    dock.ZoneA = ZoneId{ 0xa1 };
    dock.ZoneB = ZoneId{ 0xa2 };
    dock.PreloadPriority = 7;
    dock.PreloadDepth = 2;
    dock.DemandCondition.Assign("quest.open, player.ready");
    return dock;
}

} // namespace

TEST(WorldTopologyCook, EmitsReciprocalCrossGraphDockEndpoints)
{
    WorldPartitionManifest manifest = MakeManifest();
    AuthoredDockCookInput input{ .Dock = MakeDock(), .AuthoredEntity = 42 };
    input.Transform.Position = Vec3d{ 5, 2, 9 };

    std::string error;
    ASSERT_TRUE(CookWorldTopology(manifest, std::span{ &input, 1 }, {}, &error)) << error;
    ASSERT_EQ(manifest.Zones[0].Docks.size(), 1u);
    ASSERT_EQ(manifest.Zones[1].Docks.size(), 1u);
    const DockEndpoint& a = manifest.Zones[0].Docks[0];
    const DockEndpoint& b = manifest.Zones[1].Docks[0];
    EXPECT_EQ(a.Id, b.Id);
    EXPECT_EQ(a.OwnerZone, b.OtherZone);
    EXPECT_EQ(a.OtherZone, b.OwnerZone);
    EXPECT_EQ(a.OwnerGraph, b.OtherGraph);
    EXPECT_EQ(a.OtherGraph, b.OwnerGraph);
    EXPECT_EQ(a.Side, DockSide::A);
    EXPECT_EQ(b.Side, DockSide::B);
    EXPECT_EQ(a.Normal, -b.Normal);
    EXPECT_EQ(a.PreloadPriority, 7);
    EXPECT_EQ(a.RequiredTags,
              (std::vector<std::string>{ "quest.open", "player.ready" }));
    ASSERT_EQ(manifest.DockDebugMap.size(), 1u);
    EXPECT_EQ(manifest.DockDebugMap[0].AuthoredEntity, 42u);
}

TEST(WorldTopologyCook, EmitsReciprocalLinkEndpoints)
{
    WorldPartitionManifest manifest = MakeManifest();
    WorldLink link;
    link.Id = LinkId{ 0xe1 };
    link.ZoneA = ZoneId{ 0xa1 };
    link.ZoneB = ZoneId{ 0xa2 };
    link.Directions = DockDirectionAToB;
    const AuthoredLinkCookInput input{ .Link = link, .AuthoredEntity = 77 };

    std::string error;
    ASSERT_TRUE(CookWorldTopology(manifest, {}, std::span{ &input, 1 }, &error)) << error;
    ASSERT_EQ(manifest.Zones[0].Links.size(), 1u);
    ASSERT_EQ(manifest.Zones[1].Links.size(), 1u);
    EXPECT_EQ(manifest.Zones[0].Links[0].OtherZone, ZoneId{ 0xa2 });
    EXPECT_EQ(manifest.Zones[1].Links[0].OtherZone, ZoneId{ 0xa1 });
    EXPECT_EQ(manifest.Zones[0].Links[0].Directions, DockDirectionAToB);
    ASSERT_EQ(manifest.LinkDebugMap.size(), 1u);
    EXPECT_EQ(manifest.LinkDebugMap[0].AuthoredEntity, 77u);
}

TEST(WorldTopologyCook, OutputIsStableAcrossAuthoredInputOrder)
{
    WorldPartitionManifest first = MakeManifest();
    WorldPartitionManifest second = MakeManifest();
    WorldDock low = MakeDock();
    WorldDock high = MakeDock();
    high.Id = DockId{ 0xd2 };
    const AuthoredDockCookInput ascending[] = {
        { .Dock = low, .AuthoredEntity = 10 },
        { .Dock = high, .AuthoredEntity = 20 },
    };
    const AuthoredDockCookInput descending[] = {
        { .Dock = high, .AuthoredEntity = 20 },
        { .Dock = low, .AuthoredEntity = 10 },
    };

    ASSERT_TRUE(CookWorldTopology(first, ascending, {}));
    ASSERT_TRUE(CookWorldTopology(second, descending, {}));
    EXPECT_EQ(first, second);
}

TEST(WorldTopologyCook, DuplicateIdsFailWithoutAmbiguousEndpoints)
{
    WorldPartitionManifest manifest = MakeManifest();
    const WorldPartitionManifest before = manifest;
    const AuthoredDockCookInput inputs[] = {
        { .Dock = MakeDock(), .AuthoredEntity = 10 },
        { .Dock = MakeDock(), .AuthoredEntity = 20 },
    };
    std::string error;

    EXPECT_FALSE(CookWorldTopology(manifest, inputs, {}, &error));
    EXPECT_NE(error.find("unique"), std::string::npos);
    EXPECT_EQ(manifest, before);
}
