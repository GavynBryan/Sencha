// Which casters fit a frame, which tile each owns, what projection fits them,
// and which items receive the shadow -- all arithmetic, all pinned here.

#include <gtest/gtest.h>

#include <render/CameraProjection.h>
#include <render/ProjectedShadowFramePolicy.h>

namespace
{

ProjectedShadowCaster CasterAt(Vec<3> center, std::uint32_t entityIndex)
{
    ProjectedShadowCaster caster;
    caster.Key = RenderEntityKey{ .Entity = EntityId{ entityIndex, 1 } };
    caster.WorldBounds =
        Aabb3d::FromCenterHalfExtent(center, Vec3d(0.5f, 1.0f, 0.5f));
    return caster;
}

RenderQueueItem ItemAt(Vec<3> center, Vec<3> halfExtent)
{
    RenderQueueItem item;
    item.WorldBounds = Aabb3d::FromCenterHalfExtent(center, halfExtent);
    return item;
}

} // namespace

TEST(ProjectedShadowFramePolicy, RanksNearestFirstAndCountsTheDropped)
{
    ProjectedShadowSet set;
    set.Casters.push_back(CasterAt(Vec<3>(30.0f, 0.0f, 0.0f), 1));
    set.Casters.push_back(CasterAt(Vec<3>(5.0f, 0.0f, 0.0f), 2));
    set.Casters.push_back(CasterAt(Vec<3>(15.0f, 0.0f, 0.0f), 3));

    const std::uint32_t dropped =
        RankAndClampProjectedCasters(set, Vec<3>(0.0f, 0.0f, 0.0f), 2);

    EXPECT_EQ(dropped, 1u);
    ASSERT_EQ(set.Casters.size(), 2u);
    EXPECT_EQ(set.Casters[0].Key.Entity.Index, 2u);
    EXPECT_EQ(set.Casters[1].Key.Entity.Index, 3u);
}

TEST(ProjectedShadowFramePolicy, EquidistantCastersOrderByKey)
{
    ProjectedShadowSet set;
    set.Casters.push_back(CasterAt(Vec<3>(0.0f, 0.0f, 10.0f), 9));
    set.Casters.push_back(CasterAt(Vec<3>(0.0f, 0.0f, -10.0f), 4));

    RankAndClampProjectedCasters(set, Vec<3>(0.0f, 0.0f, 0.0f), 2);

    EXPECT_EQ(set.Casters[0].Key.Entity.Index, 4u);
    EXPECT_EQ(set.Casters[1].Key.Entity.Index, 9u);
}

TEST(ProjectedShadowFramePolicy, TheTileGridCoversTheBudgetWithUniformTiles)
{
    const ProjectedShadowTileGrid grid = MakeProjectedShadowTileGrid(16, 128);
    EXPECT_EQ(grid.TilesPerRow, 4u);
    EXPECT_EQ(grid.AtlasExtent, 512u);

    // A non-square budget still fits: 5 casters need a 3x3 grid.
    EXPECT_EQ(MakeProjectedShadowTileGrid(5, 64).TilesPerRow, 3u);

    const ProjectedShadowTileRect rect = ProjectedShadowTileRectFor(grid, 6);
    EXPECT_EQ(rect.X, 256);
    EXPECT_EQ(rect.Y, 128);
    EXPECT_EQ(rect.Extent, 128u);

    // The UV remap agrees with the pixel rect.
    const Vec4 uv = ProjectedShadowTileUvScaleBias(grid, 6);
    EXPECT_NEAR(uv.X, 0.25f, 1e-6f);
    EXPECT_NEAR(uv.Z, 0.5f, 1e-6f);
    EXPECT_NEAR(uv.W, 0.25f, 1e-6f);
}

TEST(ProjectedShadowFramePolicy, TheProjectionContainsEveryCasterCorner)
{
    // Two shapes: a diagonal direction and a near-vertical one (the basis
    // flips its up hint at |Y| ~ 1, and both sides must still contain the
    // caster).
    for (const Vec<3> direction : { Vec<3>(0.6f, -0.7f, 0.4f), Vec<3>(0.0f, -1.0f, 0.01f) })
    {
        ProjectedShadowCaster caster = CasterAt(Vec<3>(3.0f, 1.5f, -2.0f), 1);
        Vec<3> unit = direction;
        const float length = std::sqrt(unit.Dot(unit));
        unit = unit * (1.0f / length);
        caster.Direction = unit;

        const Mat4 viewProjection =
            FitProjectedShadowProjection(caster, 6.0f).ViewProjection;

        const Vec<3>& lo = caster.WorldBounds.Min;
        const Vec<3>& hi = caster.WorldBounds.Max;
        for (int corner = 0; corner < 8; ++corner)
        {
            const Vec4 clip = viewProjection
                * Vec4((corner & 1) != 0 ? hi.X : lo.X,
                       (corner & 2) != 0 ? hi.Y : lo.Y,
                       (corner & 4) != 0 ? hi.Z : lo.Z, 1.0f);
            // Orthographic: w stays 1, NDC xy in [-1,1], Vulkan depth in [0,1].
            EXPECT_GE(clip.X, -1.0f); EXPECT_LE(clip.X, 1.0f);
            EXPECT_GE(clip.Y, -1.0f); EXPECT_LE(clip.Y, 1.0f);
            EXPECT_GE(clip.Z, 0.0f);  EXPECT_LE(clip.Z, 1.0f);
        }
    }
}

TEST(ProjectedShadowFramePolicy, TheSweptBoundsReachExactlyTheProjectionDistance)
{
    ProjectedShadowCaster caster = CasterAt(Vec<3>(0.0f, 2.0f, 0.0f), 1);
    caster.Direction = Vec<3>(0.0f, -1.0f, 0.0f);

    const Aabb3d swept = ProjectedShadowSweptBounds(caster, 6.0f);

    EXPECT_NEAR(swept.Max.Y, 3.0f, 1e-5f);  // caster top unchanged
    EXPECT_NEAR(swept.Min.Y, -5.0f, 1e-5f); // bottom swept down by 6
}

TEST(ProjectedShadowFramePolicy, ReceiversExcludeSkinnedItemsAndHonorTheCap)
{
    ProjectedShadowCaster caster = CasterAt(Vec<3>(0.0f, 2.0f, 0.0f), 1);
    caster.Direction = Vec<3>(0.0f, -1.0f, 0.0f);
    const Aabb3d swept = ProjectedShadowSweptBounds(caster, 6.0f);

    std::vector<RenderQueueItem> items;
    items.push_back(ItemAt(Vec<3>(0.0f, -1.0f, 0.0f), Vec<3>(4.0f, 0.5f, 4.0f))); // floor: receives
    RenderQueueItem other = ItemAt(Vec<3>(0.5f, 0.5f, 0.0f), Vec<3>(0.5f, 1.0f, 0.5f));
    other.SkinnedMesh = SkinnedMeshHandle{ 3, 1 };
    items.push_back(other);                                                        // character: never
    items.push_back(ItemAt(Vec<3>(40.0f, 0.0f, 0.0f), Vec<3>(1.0f, 1.0f, 1.0f)));  // far: outside
    items.push_back(ItemAt(Vec<3>(0.0f, -3.0f, 0.0f), Vec<3>(1.0f, 0.5f, 1.0f)));  // below: receives

    std::vector<std::uint32_t> receivers;
    const std::uint32_t excluded =
        GatherProjectedShadowReceivers(items, swept, 1, receivers);

    ASSERT_EQ(receivers.size(), 1u);
    EXPECT_EQ(receivers[0], 0u); // queue order: the floor came first
    EXPECT_EQ(excluded, 1u);     // the below-item hit the cap
}

TEST(ProjectedShadowFramePolicy, TheScreenRectBoundsTheVolumeAndClampsToTheTarget)
{
    // A view from above, looking down -Z at a box around the origin.
    const Mat4 view = Mat4::MakeLookAt(Vec3d(0.0f, 2.0f, 10.0f),
                                       Vec3d(0.0f, 2.0f, 0.0f),
                                       Vec3d(0.0f, 1.0f, 0.0f));
    const Mat4 projection = MakeVulkanPerspective(1.2f, 16.0f / 9.0f, 0.1f, 100.0f);
    const Mat4 viewProjection = projection * view;

    const Aabb3d centered =
        Aabb3d::FromCenterHalfExtent(Vec3d(0.0f, 2.0f, 0.0f), Vec3d(1.0f, 1.0f, 1.0f));
    const ProjectedShadowScreenRect rect =
        ComputeProjectedShadowScreenRect(centered, viewProjection, 1280, 720);

    // On screen, roughly centered, and a small fraction of the target.
    ASSERT_GT(rect.Width, 0u);
    EXPECT_GT(rect.X, 300);
    EXPECT_LT(rect.X + static_cast<std::int32_t>(rect.Width), 1000);
    EXPECT_LT(rect.Width, 700u);

    // Fully off screen: empty rect, the caster is skipped for this view.
    const Aabb3d behind =
        Aabb3d::FromCenterHalfExtent(Vec3d(0.0f, 2.0f, 40.0f), Vec3d(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(ComputeProjectedShadowScreenRect(behind, viewProjection, 1280, 720).Width, 0u);

    // Crossing the near plane: conservative full-target rect, never a
    // truncated one that would clip visible shadow.
    const Aabb3d straddling =
        Aabb3d::FromCenterHalfExtent(Vec3d(0.0f, 2.0f, 10.0f), Vec3d(1.0f, 1.0f, 1.0f));
    const ProjectedShadowScreenRect full =
        ComputeProjectedShadowScreenRect(straddling, viewProjection, 1280, 720);
    EXPECT_EQ(full.Width, 1280u);
    EXPECT_EQ(full.Height, 720u);
}

// The composite's scissor: union of the per-caster rects, with empty rects
// contributing nothing.
TEST(ProjectedShadowFramePolicy, ScreenRectUnionCoversAllAndIgnoresEmpty)
{
    struct Case
    {
        const char* Name;
        std::vector<ProjectedShadowScreenRect> Rects;
        ProjectedShadowScreenRect Expected;
    };
    const Case cases[] = {
        { "empty input", {}, {} },
        { "single", { { 10, 20, 30, 40 } }, { 10, 20, 30, 40 } },
        { "disjoint pair",
          { { 0, 0, 10, 10 }, { 100, 50, 20, 20 } },
          { 0, 0, 120, 70 } },
        { "contained", { { 0, 0, 100, 100 }, { 10, 10, 5, 5 } },
          { 0, 0, 100, 100 } },
        { "empty rects ignored",
          { { 5, 5, 0, 7 }, { 40, 40, 8, 8 }, { 9, 9, 3, 0 } },
          { 40, 40, 8, 8 } },
        { "all empty", { { 1, 2, 0, 0 }, { 3, 4, 0, 5 } }, {} },
        { "negative origins",
          { { -20, -10, 30, 15 }, { 5, 2, 10, 10 } },
          { -20, -10, 35, 22 } },
    };
    for (const Case& c : cases)
    {
        const ProjectedShadowScreenRect got =
            UnionProjectedShadowScreenRects(c.Rects);
        EXPECT_EQ(got.X, c.Expected.X) << c.Name;
        EXPECT_EQ(got.Y, c.Expected.Y) << c.Name;
        EXPECT_EQ(got.Width, c.Expected.Width) << c.Name;
        EXPECT_EQ(got.Height, c.Expected.Height) << c.Name;
    }
}

// The fit's depth range is what converts the world-unit occlusion bias into
// normalized shadow depth; it must cover the caster's own span plus the
// projection reach, and grow with that reach.
TEST(ProjectedShadowFramePolicy, FitDepthRangeCoversCasterAndReach)
{
    ProjectedShadowCaster caster;
    caster.WorldBounds = Aabb3d::FromCenterHalfExtent(
        Vec<3>(2.0f, 1.0f, 0.0f), Vec3d(1.0f, 1.0f, 1.0f));
    caster.Direction = Vec<3>(0.0f, -1.0f, 0.0f);

    const ProjectedShadowProjectionFit near =
        FitProjectedShadowProjection(caster, 2.0f);
    const ProjectedShadowProjectionFit far =
        FitProjectedShadowProjection(caster, 10.0f);

    // At least the bounds' own extent along the direction plus the reach.
    EXPECT_GT(near.DepthRange, 2.0f + 2.0f - 0.5f);
    EXPECT_GT(far.DepthRange, near.DepthRange + 7.9f);
}
