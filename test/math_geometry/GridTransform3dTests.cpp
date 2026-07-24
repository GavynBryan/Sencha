#include <math/spatial/GridTransform3d.h>

#include <gtest/gtest.h>

TEST(GridTransform3d, PointPositionsSpanBoundsAtCellSize)
{
    GridTransform3d grid;
    grid.Origin = Vec3d(1.0f, 2.0f, 3.0f);
    grid.CellSize = 0.5f;
    grid.DimsX = 3;
    grid.DimsY = 2;
    grid.DimsZ = 4;

    EXPECT_EQ(grid.PointCount(), 24u);

    const Vec3d first = grid.PointPosition(0, 0, 0);
    EXPECT_FLOAT_EQ(first.X, 1.0f);
    EXPECT_FLOAT_EQ(first.Y, 2.0f);
    EXPECT_FLOAT_EQ(first.Z, 3.0f);

    // Point convention: Dims points span Dims - 1 cells.
    const Vec3d last = grid.PointPosition(2, 1, 3);
    EXPECT_FLOAT_EQ(last.X, 2.0f);
    EXPECT_FLOAT_EQ(last.Y, 2.5f);
    EXPECT_FLOAT_EQ(last.Z, 4.5f);

    const Aabb3d bounds = grid.Bounds();
    EXPECT_FLOAT_EQ(bounds.Min.X, 1.0f);
    EXPECT_FLOAT_EQ(bounds.Max.Z, 4.5f);
}

TEST(GridTransform3d, FromBoundsCoversTheBox)
{
    const Aabb3d bounds{ Vec3d(0.0f, 0.0f, 0.0f), Vec3d(4.0f, 2.0f, 3.0f) };
    const GridTransform3d grid = GridTransform3d::FromBounds(bounds, 1.0f);

    EXPECT_EQ(grid.DimsX, 5u);
    EXPECT_EQ(grid.DimsY, 3u);
    EXPECT_EQ(grid.DimsZ, 4u);

    // The lattice reaches at least to the box's far corner.
    const Vec3d last = grid.PointPosition(grid.DimsX - 1, grid.DimsY - 1, grid.DimsZ - 1);
    EXPECT_GE(last.X, bounds.Max.X - 1e-4f);
    EXPECT_GE(last.Y, bounds.Max.Y - 1e-4f);
    EXPECT_GE(last.Z, bounds.Max.Z - 1e-4f);
}

TEST(GridTransform3d, FromBoundsDegenerateAxisStillGetsOneCell)
{
    const Aabb3d flat{ Vec3d(0.0f, 0.0f, 0.0f), Vec3d(2.0f, 0.0f, 2.0f) };
    const GridTransform3d grid = GridTransform3d::FromBounds(flat, 1.0f);
    EXPECT_EQ(grid.DimsY, 2u);
}

TEST(GridTransform3d, TrilinearWeightsInterpolateAndSumToOne)
{
    GridTransform3d grid;
    grid.Origin = Vec3d(0.0f, 0.0f, 0.0f);
    grid.CellSize = 2.0f;
    grid.DimsX = 3;
    grid.DimsY = 3;
    grid.DimsZ = 3;

    // Exactly on a grid point: all weight lands on that corner.
    {
        const TrilinearSample3d s = TrilinearSampleAt(grid, Vec3d(2.0f, 2.0f, 2.0f));
        float onPoint = 0.0f;
        float total = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            total += s.Weight[i];
            if (s.Corner[i] == grid.FlatIndex(1, 1, 1))
                onPoint += s.Weight[i];
        }
        EXPECT_NEAR(total, 1.0f, 1e-5f);
        EXPECT_NEAR(onPoint, 1.0f, 1e-5f);
    }

    // Mid-cell along one axis: an even split between the two bracketing points.
    {
        const TrilinearSample3d s = TrilinearSampleAt(grid, Vec3d(1.0f, 0.0f, 0.0f));
        float w000 = 0.0f;
        float w100 = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            if (s.Corner[i] == grid.FlatIndex(0, 0, 0))
                w000 += s.Weight[i];
            if (s.Corner[i] == grid.FlatIndex(1, 0, 0))
                w100 += s.Weight[i];
        }
        EXPECT_NEAR(w000, 0.5f, 1e-5f);
        EXPECT_NEAR(w100, 0.5f, 1e-5f);
    }
}

TEST(GridTransform3d, TrilinearClampsOutsidePositionsToTheBoundary)
{
    GridTransform3d grid;
    grid.Origin = Vec3d(0.0f, 0.0f, 0.0f);
    grid.CellSize = 1.0f;
    grid.DimsX = 2;
    grid.DimsY = 2;
    grid.DimsZ = 2;

    const TrilinearSample3d s = TrilinearSampleAt(grid, Vec3d(-5.0f, 10.0f, 0.5f));
    float total = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        total += s.Weight[i];
        EXPECT_LT(s.Corner[i], grid.PointCount());
    }
    EXPECT_NEAR(total, 1.0f, 1e-5f);

    // Clamped to x = 0, y = 1: only those corners carry weight.
    for (int i = 0; i < 8; ++i)
    {
        if (s.Weight[i] <= 0.0f)
            continue;
        const std::uint32_t index = s.Corner[i];
        EXPECT_TRUE(index == grid.FlatIndex(0, 1, 0) || index == grid.FlatIndex(0, 1, 1));
    }
}

TEST(GridTransform3d, FarBoundaryPositionWeightsTheLastPoint)
{
    GridTransform3d grid;
    grid.Origin = Vec3d(0.0f, 0.0f, 0.0f);
    grid.CellSize = 1.0f;
    grid.DimsX = 4;
    grid.DimsY = 1;
    grid.DimsZ = 1;

    const TrilinearSample3d s = TrilinearSampleAt(grid, Vec3d(3.0f, 0.0f, 0.0f));
    float onLast = 0.0f;
    for (int i = 0; i < 8; ++i)
        if (s.Corner[i] == grid.FlatIndex(3, 0, 0))
            onLast += s.Weight[i];
    EXPECT_NEAR(onLast, 1.0f, 1e-5f);
}
