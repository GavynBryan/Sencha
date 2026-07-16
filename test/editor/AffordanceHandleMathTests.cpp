#include <gtest/gtest.h>

#include "editmodes/AffordanceHandleMath.h"
#include "editmodes/GizmoMath.h"

TEST(AffordanceHandleMath, AabbFacesEnforceThickness)
{
    const Aabb3d original = Aabb3d::FromMinMax({ -1, -2, -3 }, { 1, 2, 0 });
    const auto widened = AffordanceHandleMath::ResizeAabbFace(
        original, 0, true, 4.0);
    ASSERT_TRUE(widened.has_value());
    EXPECT_FLOAT_EQ(widened->Max.X, 4.0f);
    EXPECT_EQ(widened->Min, original.Min);

    const auto thin = AffordanceHandleMath::ResizeAabbFace(
        original, 1, false, 100.0);
    ASSERT_TRUE(thin.has_value());
    EXPECT_NEAR(thin->Max.Y - thin->Min.Y,
                AffordanceHandleMath::MinimumThickness, 1.0e-5);
}

TEST(AffordanceHandleMath, RectangleEdgeKeepsOppositeEdgeFixed)
{
    const auto width = AffordanceHandleMath::ResizeRectangleEdge(
        { 1, 2 }, 0, true, 3.5);
    ASSERT_TRUE(width.has_value());
    EXPECT_FLOAT_EQ(width->Min.X, -1.0f);
    EXPECT_FLOAT_EQ(width->Max.X, 3.5f);
    EXPECT_FLOAT_EQ(width->Min.Y, -2.0f);
    EXPECT_FLOAT_EQ(width->Max.Y, 2.0f);

    const auto minimum = AffordanceHandleMath::ResizeRectangleEdge(
        { 1, 2 }, 1, false, 100.0);
    ASSERT_TRUE(minimum.has_value());
    EXPECT_NEAR(minimum->Max.Y - minimum->Min.Y,
                AffordanceHandleMath::MinimumThickness, 1.0e-5);
    EXPECT_FLOAT_EQ(minimum->Max.Y, 2.0f);
}

TEST(AffordanceHandleMath, SharedAbsoluteSnapAnchorsMovedFaceToGrid)
{
    const double offset = GizmoMath::SnapAxisOffset(
        /*rawOffset*/ 1.2, /*pivotCoord*/ 0.3,
        /*originCoord*/ 0.0, /*spacing*/ 1.0f);
    EXPECT_DOUBLE_EQ(0.3 + offset, 2.0);
}
