#include "NavigationFixture.h"

#include <gtest/gtest.h>

namespace
{
    NavTileMesh LoadSingleTile(const NavBuildProfile& profile, NavTileCoord tile,
                               const std::vector<std::byte>& data)
    {
        NavTileMesh mesh;
        EXPECT_TRUE(mesh.Init(profile, 16));
        EXPECT_TRUE(mesh.SetTile(tile, data));
        return mesh;
    }
}

TEST(NavTileBuild, FloorProducesWalkablePolygonsInsetByRadius)
{
    NavTestGeometry geometry;
    geometry.AddFloor(0.5f, 0.5f, 7.5f, 7.5f);
    const NavBuildProfile profile = TestProfile();

    std::vector<std::byte> data;
    ASSERT_EQ(BuildNavTile(TileInput(geometry, profile, { 0, 0 }), data),
              NavTileBuildResult::Built);
    NavTileMesh mesh = LoadSingleTile(profile, { 0, 0 }, data);
    EXPECT_EQ(mesh.TileCount(), 1u);
    EXPECT_GT(mesh.PolygonCount(), 0u);

    const std::optional<Vec3d> center =
        mesh.ProjectForTooling(Vec3d(4.0f, 0.5f, 4.0f), Vec3d(0.5f, 1.0f, 0.5f));
    ASSERT_TRUE(center.has_value());
    EXPECT_NEAR(center->Y, 0.0f, 0.2f);

    // The floor's edge is 0.5 m in; walkable space stops a radius further in, so
    // a point at the edge projects inward by at least most of the radius.
    const std::optional<Vec3d> edge =
        mesh.ProjectForTooling(Vec3d(0.5f, 0.5f, 4.0f), Vec3d(1.0f, 1.0f, 1.0f));
    ASSERT_TRUE(edge.has_value());
    EXPECT_GE(edge->X, 0.5f + profile.Radius - profile.CellSize);
}

TEST(NavTileBuild, AgentHeightAndSlopeDecideWalkableSpace)
{
    const NavBuildProfile profile = TestProfile();

    // A floor under a ceiling lower than the agent is not walkable. (The
    // ceiling slab's own top is, so probe at floor height.)
    NavTestGeometry lowCeiling;
    lowCeiling.AddFloor(0.5f, 0.5f, 7.5f, 7.5f);
    lowCeiling.AddBox(Vec3d(0.0f, 1.2f, 0.0f), Vec3d(8.0f, 1.5f, 8.0f));
    const Vec3d floorPoint(4.0f, 0.0f, 4.0f);
    const Vec3d floorExtents(0.5f, 0.3f, 0.5f);
    std::vector<std::byte> data;
    ASSERT_EQ(BuildNavTile(TileInput(lowCeiling, profile, { 0, 0 }), data),
              NavTileBuildResult::Built);
    EXPECT_FALSE(LoadSingleTile(profile, { 0, 0 }, data)
                     .ProjectForTooling(floorPoint, floorExtents).has_value());

    // Relaxing the profile height restores it: agent geometry decides space.
    NavBuildProfile shortAgent = profile;
    shortAgent.Height = 1.0f;
    ASSERT_EQ(BuildNavTile(TileInput(lowCeiling, shortAgent, { 0, 0 }), data),
              NavTileBuildResult::Built);
    EXPECT_TRUE(LoadSingleTile(shortAgent, { 0, 0 }, data)
                    .ProjectForTooling(floorPoint, floorExtents).has_value());

    // A 26.6 degree ramp: walkable under a 45 degree limit, not under 20.
    NavTestGeometry ramp;
    ramp.Positions = { Vec3d(1.0f, 0.0f, 1.0f), Vec3d(7.0f, 0.0f, 1.0f),
                       Vec3d(7.0f, 3.0f, 7.0f), Vec3d(1.0f, 3.0f, 7.0f) };
    ramp.Indices = { 0, 2, 1, 0, 3, 2 };
    const Vec3d rampPoint(4.0f, 1.5f, 4.0f);
    const Vec3d rampExtents(0.5f, 0.5f, 0.5f);
    ASSERT_EQ(BuildNavTile(TileInput(ramp, profile, { 0, 0 }), data),
              NavTileBuildResult::Built);
    EXPECT_TRUE(LoadSingleTile(profile, { 0, 0 }, data)
                    .ProjectForTooling(rampPoint, rampExtents).has_value());
    NavBuildProfile flatOnly = profile;
    flatOnly.MaxSlopeDegrees = 20.0f;
    EXPECT_EQ(BuildNavTile(TileInput(ramp, flatOnly, { 0, 0 }), data),
              NavTileBuildResult::Empty);
}

TEST(NavTileBuild, EmptyTileHasNoTriangles)
{
    NavTestGeometry geometry;
    geometry.AddFloor(0.5f, 0.5f, 7.5f, 7.5f);
    std::vector<std::byte> data;
    EXPECT_EQ(BuildNavTile(TileInput(geometry, TestProfile(), { 5, 5 }), data),
              NavTileBuildResult::Empty);
    EXPECT_TRUE(data.empty());
}

TEST(NavTileBuild, IdenticalInputProducesIdenticalBytes)
{
    NavTestGeometry geometry;
    geometry.AddFloor(-3.0f, -3.0f, 11.0f, 11.0f);
    geometry.AddBox(Vec3d(3.0f, 0.0f, 1.0f), Vec3d(3.5f, 2.5f, 6.0f));
    const NavBuildProfile profile = TestProfile();

    std::vector<std::byte> first;
    std::vector<std::byte> second;
    ASSERT_EQ(BuildNavTile(TileInput(geometry, profile, { 0, 0 }), first),
              NavTileBuildResult::Built);
    ASSERT_EQ(BuildNavTile(TileInput(geometry, profile, { 0, 0 }), second),
              NavTileBuildResult::Built);
    EXPECT_EQ(first, second);
}

TEST(NavTileBuild, AreaVolumeMarksPolygons)
{
    NavTestGeometry geometry;
    geometry.AddFloor(0.5f, 0.5f, 7.5f, 7.5f);
    NavAreaVolume water;
    water.Footprint = { Vec3d(2.0f, 0.0f, 2.0f), Vec3d(6.0f, 0.0f, 2.0f),
                        Vec3d(6.0f, 0.0f, 6.0f), Vec3d(2.0f, 0.0f, 6.0f) };
    water.MinY = -1.0f;
    water.MaxY = 1.0f;
    water.Area = 3;

    NavTileBuildInput input = TileInput(geometry, TestProfile(), { 0, 0 });
    std::vector<std::byte> plain;
    ASSERT_EQ(BuildNavTile(input, plain), NavTileBuildResult::Built);
    input.Areas = std::span<const NavAreaVolume>(&water, 1);
    std::vector<std::byte> marked;
    ASSERT_EQ(BuildNavTile(input, marked), NavTileBuildResult::Built);
    EXPECT_NE(plain, marked);
}

TEST(NavTileMesh, ReplacingATileChangesOnlyItsRevision)
{
    NavTestGeometry geometry;
    geometry.AddFloor(0.5f, 0.5f, 15.5f, 7.5f);
    const NavBuildProfile profile = TestProfile();
    std::vector<std::byte> left;
    std::vector<std::byte> right;
    ASSERT_EQ(BuildNavTile(TileInput(geometry, profile, { 0, 0 }), left),
              NavTileBuildResult::Built);
    ASSERT_EQ(BuildNavTile(TileInput(geometry, profile, { 1, 0 }), right),
              NavTileBuildResult::Built);

    NavTileMesh mesh;
    ASSERT_TRUE(mesh.Init(profile, 16));
    ASSERT_TRUE(mesh.SetTile({ 0, 0 }, left));
    ASSERT_TRUE(mesh.SetTile({ 1, 0 }, right));
    const std::uint64_t leftBefore = mesh.TileRevision({ 0, 0 });
    const std::uint64_t rightBefore = mesh.TileRevision({ 1, 0 });
    ASSERT_NE(leftBefore, 0u);

    ASSERT_TRUE(mesh.SetTile({ 1, 0 }, right));
    EXPECT_EQ(mesh.TileRevision({ 0, 0 }), leftBefore);
    EXPECT_NE(mesh.TileRevision({ 1, 0 }), rightBefore);
    EXPECT_NE(mesh.TileRevision({ 1, 0 }), 0u);

    // Data built for another coordinate is refused.
    EXPECT_FALSE(mesh.SetTile({ 2, 0 }, right));
    ASSERT_TRUE(mesh.SetTile({ 1, 0 }, {}));
    EXPECT_EQ(mesh.TileRevision({ 1, 0 }), 0u);
    EXPECT_EQ(mesh.TileCount(), 1u);
}
