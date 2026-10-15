// The navigation mesh as plain data for tools: pieces of walkable surface
// joined by portals, ending at borders, across tile seams, deterministic, and
// every polygon a region the queries accept.

#include "NavigationFixture.h"

#include <navigation/tooling/NavMeshSnapshot.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <vector>

namespace
{
    // How many separate pieces of walkable surface the portals join.
    std::size_t Pieces(const NavMeshSnapshot& snapshot)
    {
        std::vector<std::uint32_t> parent(snapshot.Polygons.size());
        std::iota(parent.begin(), parent.end(), 0u);
        const auto root = [&](std::uint32_t at) {
            while (parent[at] != at)
                at = parent[at] = parent[parent[at]];
            return at;
        };
        for (const NavSnapshotPortal& portal : snapshot.Portals)
            parent[root(portal.From)] = root(portal.To);
        std::size_t pieces = 0;
        for (std::uint32_t at = 0; at < parent.size(); ++at)
            pieces += root(at) == at ? 1 : 0;
        return pieces;
    }

    Vec3d Centroid(const NavMeshSnapshot& snapshot, const NavSnapshotPolygon& polygon)
    {
        Vec3d sum = Vec3d::Zero();
        for (std::uint32_t v = 0; v < polygon.VertexCount; ++v)
            sum = sum + snapshot.Vertices[polygon.FirstVertex + v];
        return sum * (1.0f / static_cast<float>(polygon.VertexCount));
    }
}

TEST(NavMeshSnapshot, TwoIslandsAreTwoPiecesEndingAtBorders)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(TwoIslands());
    const NavMeshSnapshot snapshot = SnapshotNavMesh(zone, 0);
    ASSERT_FALSE(snapshot.Polygons.empty());
    EXPECT_EQ(Pieces(snapshot), 2u);
    EXPECT_FALSE(snapshot.Borders.empty());
    for (const NavSnapshotPortal& portal : snapshot.Portals)
        EXPECT_LT(portal.From, portal.To);
}

TEST(NavMeshSnapshot, TileSeamsAreJoinedNotBorders)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(LongHall());
    const NavMeshSnapshot snapshot = SnapshotNavMesh(zone, 0);
    EXPECT_EQ(Pieces(snapshot), 1u) << "a 40 m hall over 8 m tiles is one piece";
    for (const NavSnapshotBorder& border : snapshot.Borders)
    {
        const Vec3d middle = (border.A + border.B) * 0.5f;
        const bool atTheEdge = middle.X < 1.0f || middle.X > 39.0f || middle.Z < 1.0f || middle.Z > 9.0f;
        EXPECT_TRUE(atTheEdge) << "a border inside the hall at x " << middle.X << ", z " << middle.Z;
    }
}

TEST(NavMeshSnapshot, ASnapshotIsTheSameEveryTime)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(WallRoom());
    const NavMeshSnapshot first = SnapshotNavMesh(zone, 0);
    const NavMeshSnapshot second = SnapshotNavMesh(zone, 0);
    ASSERT_EQ(first.Polygons.size(), second.Polygons.size());
    ASSERT_EQ(first.Portals.size(), second.Portals.size());
    ASSERT_EQ(first.Borders.size(), second.Borders.size());
    for (std::size_t i = 0; i < first.Polygons.size(); ++i)
        EXPECT_EQ(first.Polygons[i].Region.Ref, second.Polygons[i].Region.Ref);
    for (std::size_t i = 0; i < first.Portals.size(); ++i)
    {
        EXPECT_EQ(first.Portals[i].From, second.Portals[i].From);
        EXPECT_EQ(first.Portals[i].To, second.Portals[i].To);
    }
}

TEST(NavMeshSnapshot, EveryPolygonIsARegionTheQueriesAccept)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(WallRoom());
    const NavMeshSnapshot snapshot = SnapshotNavMesh(zone, 0);
    NavQueryContext context;
    for (const NavSnapshotPolygon& polygon : snapshot.Polygons)
    {
        const NavProjectResult inside = NavClosestPointInRegion(zone, context, polygon.Region, Centroid(snapshot, polygon));
        EXPECT_EQ(inside.Status, NavStatus::Success);
    }
}

TEST(NavMeshSnapshot, APolygonNamesItsArea)
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 20.0f, 12.0f);
    NavAreaVolume water;
    water.Footprint = { Vec3d(4, 0, -1), Vec3d(16, 0, -1), Vec3d(16, 0, 3), Vec3d(4, 0, 3) };
    water.MinY = -1.0f;
    water.MaxY = 1.0f;
    water.Area = 1;
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(g, {}, { water });
    const NavMeshSnapshot snapshot = SnapshotNavMesh(zone, 0);
    EXPECT_TRUE(std::ranges::any_of(snapshot.Polygons, [&](const NavSnapshotPolygon& p) { return p.Area == f.Water; }));
    EXPECT_TRUE(std::ranges::any_of(snapshot.Polygons, [&](const NavSnapshotPolygon& p) { return p.Area != f.Water; }));
}

TEST(NavMeshSnapshot, AProfileTheZoneLacksIsEmpty)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(TwoIslands());
    const NavMeshSnapshot snapshot = SnapshotNavMesh(zone, 7);
    EXPECT_TRUE(snapshot.Polygons.empty());
    EXPECT_EQ(snapshot.Zone, f.Zone);
}
