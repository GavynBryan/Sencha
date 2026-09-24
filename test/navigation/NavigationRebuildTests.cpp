// Runtime navigation geometry: a tagged collider added, moved, or removed at
// runtime rebuilds only the tiles it touches, deterministically, within a
// per-tick budget, and invalidates only what depended on those tiles.

#include "NavigationRuntimeHarness.h"

#include <jobs/JobSystem.h>

#include <gtest/gtest.h>

#include <array>

namespace
{
    constexpr ZoneId kZone{ 0x77 };
}

TEST(NavigationRebuild, AWalkableSurfaceCanAppearAndDisappear)
{
    NavRuntimeHarness h;
    h.Attach(kZone, TwoIslands());
    EXPECT_EQ(h.Reachable(kZone, Vec3d(2, 0, 5), Vec3d(18, 0, 5)), NavStatus::NoPath);

    // A bridge extends across the gap, its deck flush with both floors.
    const EntityId bridge = h.AddBox(Vec3d(10.0f, -0.25f, 5.0f), Vec3d(3.0f, 0.25f, 2.0f));
    h.Settle();
    EXPECT_GT(h.System.LastRebuildStats().RebuiltTiles, 0u);
    EXPECT_EQ(h.Reachable(kZone, Vec3d(2, 0, 5), Vec3d(18, 0, 5)), NavStatus::Success);

    // It retracts.
    h.Runtime.Entities().DestroyEntity(bridge);
    h.Settle();
    EXPECT_EQ(h.Reachable(kZone, Vec3d(2, 0, 5), Vec3d(18, 0, 5)), NavStatus::NoPath);
}

TEST(NavigationRebuild, AnObstacleBlocksAndReleasesACorridor)
{
    NavRuntimeHarness h;
    h.Attach(kZone, WallRoom());
    EXPECT_EQ(h.Reachable(kZone, Vec3d(5, 0, 2), Vec3d(15, 0, 2)), NavStatus::Success);

    const EntityId crate = h.AddBox(Vec3d(10.0f, 1.0f, 8.5f), Vec3d(0.5f, 1.0f, 1.6f));
    h.Settle();
    EXPECT_EQ(h.Reachable(kZone, Vec3d(5, 0, 2), Vec3d(15, 0, 2)), NavStatus::NoPath);

    h.Move(crate, Vec3d(3.0f, 1.0f, 8.5f));
    h.Settle();
    EXPECT_EQ(h.Reachable(kZone, Vec3d(5, 0, 2), Vec3d(15, 0, 2)), NavStatus::Success);
}

TEST(NavigationRebuild, OnlyWhatTouchedARebuiltTileGoesStale)
{
    NavRuntimeHarness h;
    h.Attach(kZone, LongHall());
    const NavLocation west = h.Project(kZone, Vec3d(1, 0, 5));
    const NavLocation westEnd = h.Project(kZone, Vec3d(6, 0, 5));
    const NavLocation east = h.Project(kZone, Vec3d(37, 0, 5));
    NavRouteBuffer route;
    ASSERT_EQ(h.System.Queries().FindRoute(h.Context, h.Request(kZone), west, westEnd, route),
              NavStatus::Success);

    // A crate at the far east end rebuilds only eastern tiles.
    h.AddBox(Vec3d(36.0f, 0.5f, 5.0f), Vec3d(0.5f, 0.5f, 0.5f));
    h.Settle();
    EXPECT_EQ(h.System.Queries().ValidateRoute(route), NavRouteValidity::Valid);
    EXPECT_EQ(h.System.Queries().Reachable(h.Context, h.Request(kZone), west, westEnd),
              NavStatus::Success);
    EXPECT_EQ(h.System.Queries().Reachable(h.Context, h.Request(kZone), west, east),
              NavStatus::StaleLocation);

    // A crate in the west rebuilds the route's own tile.
    h.AddBox(Vec3d(3.0f, 0.5f, 9.0f), Vec3d(0.5f, 0.5f, 0.5f));
    h.Settle();
    EXPECT_EQ(h.System.Queries().ValidateRoute(route), NavRouteValidity::TileChanged);
}

TEST(NavigationRebuild, LinksReprojectWhenTheirTileRebuilds)
{
    NavRuntimeHarness h;
    h.Attach(kZone, TwoIslands(), { NavZoneFixture::Link(0x42, "navigation.traversal.jump",
                                                         Vec3d(7, 0, 5), Vec3d(13, 0, 5)) });
    const std::array capabilities{ h.Tag("navigation.traversal.jump") };
    const NavQueryRequest request = h.Request(kZone, capabilities);
    const NavigationQuery query = h.System.Queries();
    const ZoneNavigation* zone = query.FindZone(kZone);
    const std::uint32_t link = *zone->FindLink(NavLinkId{ 0x42 });
    const std::uint32_t revision = zone->Link(link).Revision;

    // An obstacle in the entry's tile, away from the entry itself.
    h.AddBox(Vec3d(2.0f, 0.5f, 9.0f), Vec3d(0.5f, 0.5f, 0.5f));
    h.Settle();
    EXPECT_GT(h.System.LastRebuildStats().LinksReprojected, 0u);
    EXPECT_NE(zone->Link(link).Revision, revision);

    // The link still works against the rebuilt tile.
    const NavLocation a = h.Project(kZone, Vec3d(2, 0, 5));
    const NavLocation b = h.Project(kZone, Vec3d(18, 0, 5));
    EXPECT_EQ(query.Reachable(h.Context, request, a, b), NavStatus::Success);
}

TEST(NavigationRebuild, BudgetSpreadsTilesAcrossTicksInOrder)
{
    NavRuntimeHarness h;
    h.Attach(kZone, LongHall());
    // A long beam touches every tile of the hall.
    h.AddBox(Vec3d(20.0f, 0.5f, 9.0f), Vec3d(19.5f, 0.5f, 0.3f));
    h.System.DetectGeometryChanges();
    const std::size_t dirty = h.System.PendingDirtyTiles();
    ASSERT_GT(dirty, 4u);
    h.System.RebuildDirtyTiles();
    EXPECT_EQ(h.System.LastRebuildStats().RebuiltTiles, 4u);
    EXPECT_EQ(h.System.PendingDirtyTiles(), dirty - 4);
    while (h.System.PendingDirtyTiles() > 0)
        h.System.RebuildDirtyTiles();
}

TEST(NavigationRebuild, SmallMovesAccumulateUntilTheyCrossTheThreshold)
{
    NavRuntimeHarness h;
    h.Attach(kZone, LongHall());
    const EntityId crate = h.AddBox(Vec3d(20.0f, 0.5f, 5.0f), Vec3d(0.5f, 0.5f, 0.5f));
    h.Settle();

    h.Move(crate, Vec3d(20.02f, 0.5f, 5.0f));
    h.System.DetectGeometryChanges();
    EXPECT_EQ(h.System.PendingDirtyTiles(), 0u);
    h.Move(crate, Vec3d(20.04f, 0.5f, 5.0f));
    h.System.DetectGeometryChanges();
    EXPECT_EQ(h.System.PendingDirtyTiles(), 0u);
    // Drift is measured from the last rebuild, not the last tick.
    h.Move(crate, Vec3d(20.06f, 0.5f, 5.0f));
    h.System.DetectGeometryChanges();
    EXPECT_GT(h.System.PendingDirtyTiles(), 0u);
}

TEST(NavigationRebuild, MeshCollidersAreReportedNotIgnored)
{
    NavRuntimeHarness h;
    h.Attach(kZone, LongHall());
    const EntityId entity = h.AddBox(Vec3d(20.0f, 0.5f, 5.0f), Vec3d(0.5f, 0.5f, 0.5f));
    h.Runtime.Entities().TryGet<Collider>(entity)->Mesh = CollisionShapeHandle{ 3 };
    h.System.DetectGeometryChanges();
    EXPECT_EQ(h.System.LastRebuildStats().SkippedMeshContributors, 1u);
    EXPECT_EQ(h.System.PendingDirtyTiles(), 0u);
}

TEST(NavigationRebuild, AZoneAttachedLaterReceivesExistingGeometry)
{
    NavRuntimeHarness h;
    // The bridge exists before the zone streams in.
    h.AddBox(Vec3d(10.0f, -0.25f, 5.0f), Vec3d(3.0f, 0.25f, 2.0f));
    h.Attach(kZone, TwoIslands());
    EXPECT_EQ(h.Reachable(kZone, Vec3d(2, 0, 5), Vec3d(18, 0, 5)), NavStatus::NoPath);
    h.Settle();
    EXPECT_EQ(h.Reachable(kZone, Vec3d(2, 0, 5), Vec3d(18, 0, 5)), NavStatus::Success);
}

TEST(NavigationRebuild, ParallelRebuildMatchesTheSerialReference)
{
    const auto run = [](JobSystem& jobs)
    {
        NavRuntimeHarness h(&jobs);
        h.Attach(kZone, LongHall());
        h.AddBox(Vec3d(20.0f, 0.5f, 5.0f), Vec3d(15.0f, 0.5f, 0.4f));
        h.AddBox(Vec3d(10.0f, 1.0f, 2.0f), Vec3d(0.6f, 1.0f, 0.6f));
        h.Settle();
        std::vector<float> costs;
        for (float z : { 1.0f, 3.0f, 7.0f, 9.0f })
        {
            const NavCostResult cost = h.System.Queries().TravelCost(
                h.Context, h.Request(kZone), h.Project(kZone, Vec3d(1, 0, z)), h.Project(kZone, Vec3d(39, 0, z)));
            costs.push_back(cost.Status == NavStatus::Success ? cost.Cost : -1.0f);
        }
        const ZoneNavigation* zone = h.System.Queries().FindZone(kZone);
        costs.push_back(static_cast<float>(zone->Mesh(0).PolygonCount()));
        return costs;
    };
    JobSystem serial(0);
    JobSystem parallel(4);
    EXPECT_EQ(run(serial), run(parallel));
}
