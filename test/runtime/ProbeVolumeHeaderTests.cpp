#include <math/spatial/GridTransform3d.h>
#include <render/RenderLight.h>

#include <gtest/gtest.h>

namespace
{
    // Mirrors the shader: uvw = worldPos * scale + bias.
    Vec3d HeaderUvw(const GpuProbeVolume& header, const Vec3d& world)
    {
        return Vec3d(world.X * header.ScaleChannelBase.X + header.BiasPriority.X,
                     world.Y * header.ScaleChannelBase.Y + header.BiasPriority.Y,
                     world.Z * header.ScaleChannelBase.Z + header.BiasPriority.Z);
    }
}

TEST(ProbeVolumeHeader, MapsLatticeCornersToTexelCenters)
{
    GridTransform3d grid;
    grid.Origin = Vec3d(-4.0f, 1.0f, 2.0f);
    grid.CellSize = 2.0f;
    grid.DimsX = 5;
    grid.DimsY = 3;
    grid.DimsZ = 4;

    const GpuProbeVolume header = MakeGpuProbeVolume(grid, 7, 2, 3);

    // First lattice point lands on the first texel center, which is also the
    // header's contains-min; the last point lands on the last texel center.
    const Vec3d atOrigin = HeaderUvw(header, grid.Origin);
    EXPECT_NEAR(atOrigin.X, 0.5f / 5.0f, 1e-6f);
    EXPECT_NEAR(atOrigin.Y, 0.5f / 3.0f, 1e-6f);
    EXPECT_NEAR(atOrigin.Z, 0.5f / 4.0f, 1e-6f);
    EXPECT_NEAR(atOrigin.X, header.UvwMinVolume.X, 1e-6f);

    const Vec3d last = grid.PointPosition(4, 2, 3);
    const Vec3d atLast = HeaderUvw(header, last);
    EXPECT_NEAR(atLast.X, 4.5f / 5.0f, 1e-6f);
    EXPECT_NEAR(atLast.Y, 2.5f / 3.0f, 1e-6f);
    EXPECT_NEAR(atLast.Z, 3.5f / 4.0f, 1e-6f);
    EXPECT_NEAR(atLast.Z, header.UvwMaxStableIndex.Z, 1e-6f);

    // A mid-lattice point maps to its own texel center: sampling there reads
    // that probe exactly, no neighbor bleed.
    const Vec3d mid = grid.PointPosition(2, 1, 1);
    const Vec3d atMid = HeaderUvw(header, mid);
    EXPECT_NEAR(atMid.X, 2.5f / 5.0f, 1e-6f);
    EXPECT_NEAR(atMid.Y, 1.5f / 3.0f, 1e-6f);
    EXPECT_NEAR(atMid.Z, 1.5f / 4.0f, 1e-6f);
}

TEST(ProbeVolumeHeader, CarriesSelectionFieldsAndChannelBase)
{
    GridTransform3d grid;
    grid.Origin = Vec3d(0.0f, 0.0f, 0.0f);
    grid.CellSize = 1.0f;
    grid.DimsX = 3;
    grid.DimsY = 3;
    grid.DimsZ = 3;

    const GpuProbeVolume header = MakeGpuProbeVolume(grid, -2, 5, 4);

    // Slot 4 begins at binding element 12 (three channel textures per slot).
    EXPECT_FLOAT_EQ(header.ScaleChannelBase.W, 12.0f);
    EXPECT_FLOAT_EQ(header.BiasPriority.W, -2.0f);
    EXPECT_FLOAT_EQ(header.UvwMaxStableIndex.W, 5.0f);
    // 3 points per axis at cell 1 span a 2x2x2 world box.
    EXPECT_FLOAT_EQ(header.UvwMinVolume.W, 8.0f);
}

TEST(ProbeVolumeHeader, LightSetCapsProbeVolumes)
{
    RenderLightSet lights;
    lights.Reset();

    GridTransform3d grid;
    grid.DimsX = grid.DimsY = grid.DimsZ = 2;
    const GpuProbeVolume header = MakeGpuProbeVolume(grid, 0, 0, 0);

    for (std::uint32_t i = 0; i < kMaxActiveProbeVolumes; ++i)
        EXPECT_TRUE(lights.AddProbeVolume(header));
    EXPECT_FALSE(lights.AddProbeVolume(header));
    EXPECT_EQ(lights.ProbeVolumeCount, kMaxActiveProbeVolumes);

    lights.Reset();
    EXPECT_EQ(lights.ProbeVolumeCount, 0u);
}
