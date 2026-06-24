#include "overlay/SelectionLabels.h"

#include <math/geometry/3d/Aabb3d.h>
#include <math/Vec.h>

#include <gtest/gtest.h>

namespace
{
constexpr Vec4 kColor{ 1.0f, 1.0f, 1.0f, 1.0f };
}

TEST(FormatUnits, WholeNumbersHaveNoDecimal)
{
    EXPECT_EQ(FormatUnits(512.0), "512");
    EXPECT_EQ(FormatUnits(64.0), "64");
    EXPECT_EQ(FormatUnits(2.0), "2");
    EXPECT_EQ(FormatUnits(0.0), "0");
}

TEST(FormatUnits, FractionalValuesGetTwoPlaces)
{
    EXPECT_EQ(FormatUnits(1.5), "1.50");
    EXPECT_EQ(FormatUnits(0.333), "0.33");
}

TEST(SelectionDimensionLabels, OneLabelPerAxisAnchoredAtMinCornerEdgeMidpoint)
{
    const Aabb3d bounds = Aabb3d::FromMinMax(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(512.0f, 64.0f, 640.0f));
    const std::vector<LabelRequest> labels = SelectionDimensionLabels(bounds, kColor);

    ASSERT_EQ(labels.size(), 3u);

    // X axis: text is the extent, anchor sits at the midpoint of the min-corner
    // edge, and the label is tagged with its world axis (for per-view culling).
    EXPECT_EQ(labels[0].Text, "512");
    EXPECT_EQ(labels[0].Axis, 0);
    EXPECT_FLOAT_EQ(labels[0].World.X, 256.0f);
    EXPECT_FLOAT_EQ(labels[0].World.Y, 0.0f);
    EXPECT_FLOAT_EQ(labels[0].World.Z, 0.0f);

    EXPECT_EQ(labels[1].Text, "64");
    EXPECT_EQ(labels[1].Axis, 1);
    EXPECT_FLOAT_EQ(labels[1].World.X, 0.0f);
    EXPECT_FLOAT_EQ(labels[1].World.Y, 32.0f);
    EXPECT_FLOAT_EQ(labels[1].World.Z, 0.0f);

    EXPECT_EQ(labels[2].Text, "640");
    EXPECT_EQ(labels[2].Axis, 2);
    EXPECT_FLOAT_EQ(labels[2].World.X, 0.0f);
    EXPECT_FLOAT_EQ(labels[2].World.Y, 0.0f);
    EXPECT_FLOAT_EQ(labels[2].World.Z, 320.0f);
}

TEST(SelectionDimensionLabels, DegenerateAxisIsSkipped)
{
    // Zero-thickness box on Y: only X and Z get labels.
    const Aabb3d bounds = Aabb3d::FromMinMax(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 0.0f, 20.0f));
    const std::vector<LabelRequest> labels = SelectionDimensionLabels(bounds, kColor);

    ASSERT_EQ(labels.size(), 2u);
    EXPECT_EQ(labels[0].Text, "10");
    EXPECT_EQ(labels[0].Axis, 0);
    EXPECT_EQ(labels[1].Text, "20");
    EXPECT_EQ(labels[1].Axis, 2);
}

TEST(SelectionDimensionLabels, InvalidBoundsProduceNoLabels)
{
    EXPECT_TRUE(SelectionDimensionLabels(Aabb3d::Empty(), kColor).empty());
}
