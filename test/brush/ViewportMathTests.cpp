#include "editmodes/GizmoMath.h"
#include "viewport/ViewportMath.h"

#include <math/Mat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Ray3d.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
// Perpendicular distance from a point to an (infinite) ray with a unit direction.
double DistanceToRay(Vec3d point, const Ray3d& ray)
{
    const Vec3d w = point - ray.Origin;
    const Vec3d proj = ray.Direction * static_cast<float>(w.Dot(ray.Direction));
    return std::sqrt((w - proj).SqrMagnitude());
}
}

// WorldToPixel then RayThroughPixel must produce a ray that passes through the
// original world point — the round-trip that the picking/gizmo paths rely on.
TEST(ViewportMath, WorldToPixelAndBackIsConsistent)
{
    const Mat4 identity = Mat4::Identity();
    const ViewportMath::Rect region{ Vec2d(0.0f, 0.0f), Vec2d(200.0f, 100.0f) };

    const Vec3d worldPoints[] = {
        Vec3d(0.0f, 0.0f, 0.0f),
        Vec3d(0.5f, -0.25f, 3.0f),
        Vec3d(-0.4f, 0.8f, -2.0f),
    };

    for (const Vec3d& world : worldPoints)
    {
        const auto projected = ViewportMath::WorldToPixel(identity, region, world);
        ASSERT_TRUE(projected.has_value());
        const Ray3d ray = ViewportMath::RayThroughPixel(identity, region, projected->Pixel);
        EXPECT_LT(DistanceToRay(world, ray), 1.0e-4);
    }
}

TEST(ViewportMath, WorldToPixelCentersOriginUnderIdentity)
{
    const ViewportMath::Rect region{ Vec2d(0.0f, 0.0f), Vec2d(200.0f, 100.0f) };
    const auto projected = ViewportMath::WorldToPixel(Mat4::Identity(), region, Vec3d(0.0f, 0.0f, 0.0f));
    ASSERT_TRUE(projected.has_value());
    EXPECT_NEAR(projected->Pixel.X, 100.0f, 1.0e-3f); // center of 0..200
    EXPECT_NEAR(projected->Pixel.Y, 50.0f, 1.0e-3f);  // center of 0..100
}

TEST(ViewportMath, PointToSegmentDistanceClampsToEnds)
{
    EXPECT_NEAR(ViewportMath::DistancePointToSegment(Vec2d(5, 5), Vec2d(0, 0), Vec2d(10, 0)), 5.0f, 1e-4f);
    EXPECT_NEAR(ViewportMath::DistancePointToSegment(Vec2d(5, 0), Vec2d(0, 0), Vec2d(10, 0)), 0.0f, 1e-4f);
    EXPECT_NEAR(ViewportMath::DistancePointToSegment(Vec2d(-5, 0), Vec2d(0, 0), Vec2d(10, 0)), 5.0f, 1e-4f);
}

// The sign regression: a drag toward +axis must yield a positive parameter.
TEST(GizmoMath, ClosestAxisParamCarriesAxisSign)
{
    const Vec3d pivot(0.0f, 0.0f, 0.0f);
    const Vec3d xAxis(1.0f, 0.0f, 0.0f);

    // Ray dropping straight down -Z, crossing the X axis at x = +5.
    const Ray3d positive(Vec3d(5.0f, 0.0f, 5.0f), Vec3d(0.0f, 0.0f, -1.0f));
    const auto sPositive = GizmoMath::ClosestAxisParam(pivot, xAxis, positive);
    ASSERT_TRUE(sPositive.has_value());
    EXPECT_NEAR(*sPositive, 5.0, 1e-4);

    // Mirror at x = -5 must give a negative parameter (not inverted).
    const Ray3d negative(Vec3d(-5.0f, 0.0f, 5.0f), Vec3d(0.0f, 0.0f, -1.0f));
    const auto sNegative = GizmoMath::ClosestAxisParam(pivot, xAxis, negative);
    ASSERT_TRUE(sNegative.has_value());
    EXPECT_NEAR(*sNegative, -5.0, 1e-4);
}

TEST(GizmoMath, ClosestAxisParamRejectsParallelRay)
{
    const Vec3d pivot(0.0f, 0.0f, 0.0f);
    const Vec3d xAxis(1.0f, 0.0f, 0.0f);
    const Ray3d alongAxis(Vec3d(0.0f, 1.0f, 0.0f), Vec3d(1.0f, 0.0f, 0.0f)); // parallel to X
    EXPECT_FALSE(GizmoMath::ClosestAxisParam(pivot, xAxis, alongAxis).has_value());
}

// Absolute snap: the result lands the pivot on a grid line, not a relative step.
TEST(GizmoMath, SnapAxisOffsetIsAbsoluteToGrid)
{
    // pivot at 0.3, drag +0.4 -> target 0.7 -> nearest line 1.0 -> offset 0.7.
    EXPECT_NEAR(GizmoMath::SnapAxisOffset(0.4, 0.3, 0.0, 1.0f), 0.7, 1e-6);
    // pivot at 0.3, drag +0.1 -> target 0.4 -> nearest line 0.0 -> offset -0.3.
    EXPECT_NEAR(GizmoMath::SnapAxisOffset(0.1, 0.3, 0.0, 1.0f), -0.3, 1e-6);
    // Grid origin offset by 0.25: lands on 0.25, not 0.0.
    EXPECT_NEAR(GizmoMath::SnapAxisOffset(0.4, 0.0, 0.25, 1.0f), 0.25, 1e-6);
    // Spacing <= 0 disables snapping.
    EXPECT_NEAR(GizmoMath::SnapAxisOffset(0.37, 0.3, 0.0, 0.0f), 0.37, 1e-6);
}
