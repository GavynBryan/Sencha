#include "WorkspaceFixture.h"

#include "document/WorldMap.h"

#include <gtest/gtest.h>

// The world from above, fitted into the content: each zone with bounds is a
// plate at its footprint, north up, keeping the world's shape; the zone the
// camera is in and the one being edited are marked, and a point on the map
// names the zone under it.
namespace
{
class WorldMapTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        First = Workspace.World.FocusZone();
        Second = AddSecondZone();
        Bound(First, Aabb3d{ Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 4.0f, 10.0f) });
        Bound(Second, Aabb3d{ Vec3d(10.0f, 0.0f, 0.0f), Vec3d(30.0f, 4.0f, 10.0f) });
    }

    void Bound(ZoneId zone, Aabb3d bounds)
    {
        for (ZoneHeader& header : Workspace.World.Manifest().Zones)
            if (header.Id == zone)
            {
                header.Bounds = bounds;
                header.BoundsOverridden = true;
            }
    }

    [[nodiscard]] const WorldMapZone& ZoneOf(const WorldMap& map, ZoneId id) const
    {
        return *std::ranges::find(map.Zones, id, &WorldMapZone::Id);
    }

    [[nodiscard]] WorldMap Lay(std::optional<Vec3d> camera = {}, std::span<const ZoneId> problems = {}) const
    {
        return LayOutWorldMap(Workspace.World, {},
                              { .Area = Rect2d(0.0f, 0.0f, 600.0f, 400.0f), .Margin = 20.0f, .MinimumPlate = 12.0f,
                                .Camera = camera, .Problems = problems });
    }

    ZoneId First{};
    ZoneId Second{};
};
} // namespace

TEST_F(WorldMapTest, ZonesArePlatesAtTheirFootprintKeepingTheWorldsShape)
{
    const WorldMap map = Lay();
    ASSERT_EQ(map.Zones.size(), 2u);
    const Rect2d first = ZoneOf(map, First).Plate;
    const Rect2d second = ZoneOf(map, Second).Plate;
    EXPECT_LT(first.Max().X, second.Min().X + 0.5f) << "east of the first, the second is to its right";
    EXPECT_NEAR(second.Width() / first.Width(), 2.0f, 0.01f) << "one scale for the whole world";
    EXPECT_NEAR(first.Width(), first.Height(), 0.5f) << "a square zone stays square";
    EXPECT_NEAR(first.Min().X - 20.0f, 600.0f - 20.0f - second.Max().X, 0.5f) << "the world is centred";
    EXPECT_GE(first.Min().X, 20.0f);
    EXPECT_LE(second.Max().X, 580.0f);
}

TEST_F(WorldMapTest, WhatIsEditedWhereTheCameraIsAndWhatIsWrongAreMarked)
{
    const ZoneId problems[] = { Second };
    const WorldMap map = Lay(Vec3d(5.0f, 2.0f, 5.0f), problems);
    EXPECT_EQ(ZoneOf(map, First).State, WorldMapZoneState::Focus);
    EXPECT_EQ(ZoneOf(map, Second).State, WorldMapZoneState::Open);
    EXPECT_TRUE(ZoneOf(map, First).Here);
    EXPECT_FALSE(ZoneOf(map, Second).Here);
    EXPECT_TRUE(ZoneOf(map, Second).Problem);
    ASSERT_TRUE(map.Camera.has_value());
    EXPECT_TRUE(ZoneOf(map, First).Plate.Contains(*map.Camera));
}

TEST_F(WorldMapTest, APointNamesTheZoneUnderItAndAZoneWithoutBoundsHasNoPlace)
{
    const WorldMap map = Lay();
    EXPECT_EQ(ZoneAt(map, ZoneOf(map, Second).Plate.Center()), Second);
    EXPECT_EQ(ZoneAt(map, Vec2d(1.0f, 1.0f)), std::nullopt);

    Bound(Second, Aabb3d{});
    const WorldMap unbounded = Lay();
    EXPECT_EQ(unbounded.Zones.size(), 1u);
    EXPECT_EQ(unbounded.Unplaced, 1u);
}
