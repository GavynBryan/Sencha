#include <gtest/gtest.h>

#include "viewport/WorldViewSettings.h"

namespace
{

ZoneHeader MakeZone(uint64_t id, Aabb3d bounds)
{
    ZoneHeader header;
    header.Id = ZoneId{ id };
    header.Bounds = bounds;
    return header;
}

// Two room-height cells side by side: the fly camera above them is outside
// both bounds, the usual editing vantage.
WorldPartitionManifest TwoCellManifest()
{
    WorldPartitionManifest manifest;
    manifest.Zones = {
        MakeZone(0xa1, Aabb3d{ { -16.0f, 0.0f, -8.0f }, { 0.0f, 7.0f, 8.0f } }),
        MakeZone(0xa2, Aabb3d{ { 0.0f, 0.0f, -8.0f }, { 16.0f, 7.0f, 8.0f } }),
    };
    return manifest;
}

} // namespace

TEST(NearestPreviewFocusZone, FollowsTheCellUnderTheCamera)
{
    const WorldPartitionManifest manifest = TwoCellManifest();

    EXPECT_EQ(NearestPreviewFocusZone(manifest, Vec3d{ -8.0f, 20.0f, 0.0f }), ZoneId{ 0xa1 });
    EXPECT_EQ(NearestPreviewFocusZone(manifest, Vec3d{ 8.0f, 20.0f, 0.0f }), ZoneId{ 0xa2 });
    // Far east of both cells: the nearer bounds still wins by closest point.
    EXPECT_EQ(NearestPreviewFocusZone(manifest, Vec3d{ 40.0f, 20.0f, 0.0f }), ZoneId{ 0xa2 });
}

TEST(NearestPreviewFocusZone, TiesBreakBySmallerVolumeThenId)
{
    WorldPartitionManifest manifest = TwoCellManifest();
    // A camera on the shared face (x = 0) is equidistant to both; shrink the
    // second cell so volume decides, then equalize so the lower id decides.
    EXPECT_EQ(NearestPreviewFocusZone(manifest, Vec3d{ 0.0f, 20.0f, 0.0f }), ZoneId{ 0xa1 });

    manifest.Zones[1].Bounds = Aabb3d{ { 0.0f, 0.0f, -4.0f }, { 16.0f, 7.0f, 4.0f } };
    EXPECT_EQ(NearestPreviewFocusZone(manifest, Vec3d{ 0.0f, 20.0f, 0.0f }), ZoneId{ 0xa2 });
}

TEST(NearestPreviewFocusZone, ContainedPositionPicksTheContainingZone)
{
    const WorldPartitionManifest manifest = TwoCellManifest();
    EXPECT_EQ(NearestPreviewFocusZone(manifest, Vec3d{ 8.0f, 3.0f, 0.0f }), ZoneId{ 0xa2 });
}

TEST(NearestPreviewFocusZone, InvalidWhenNoZoneHasValidBounds)
{
    WorldPartitionManifest manifest;
    manifest.Zones = {
        MakeZone(0xa1, Aabb3d{ { 1.0f, 1.0f, 1.0f }, { -1.0f, -1.0f, -1.0f } }),
    };
    EXPECT_FALSE(NearestPreviewFocusZone(manifest, Vec3d{}).IsValid());
}
