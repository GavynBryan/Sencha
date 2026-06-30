// Headless tests for the gizmo-drag math (editor/src/editmodes/GizmoMath.h).
//
// These guard two historical editor bug families: reversed gizmo drag (the
// closest-point-on-axis sign) and relative-vs-absolute grid snap.
// The kernel is pure and header-only, so it is tested directly without driving
// the viewport or ImGui. ViewportMathTests already covers the X-axis sign and
// one snap case; this extends to Y/Z and to the absolute-snap-with-origin and
// rawOffset==0 cases a relative implementation would get wrong.

#include <editmodes/GizmoMath.h>

#include <gtest/gtest.h>

#include <math/Vec.h>
#include <math/geometry/3d/Ray3d.h>

namespace
{
// Build a ray without the out-of-line Ray3d ctor so the kernel can be tested
// against headers alone.
Ray3d MakeRay(Vec3d origin, Vec3d direction)
{
    Ray3d ray;
    ray.Origin = origin;
    ray.Direction = direction;
    return ray;
}
} // namespace

// A drag toward the +axis side must yield a positive parameter, on every axis.
// The opposite convention is the backwards-drag bug.
TEST(GizmoMath, ClosestAxisParamCarriesSignOnX)
{
    const auto s = GizmoMath::ClosestAxisParam(
        Vec3d(0, 0, 0), Vec3d(1, 0, 0), MakeRay(Vec3d(2, 1, 0), Vec3d(0, -1, 0)));
    ASSERT_TRUE(s.has_value());
    EXPECT_NEAR(*s, 2.0, 1e-9);
}

TEST(GizmoMath, ClosestAxisParamNegativeOnX)
{
    const auto s = GizmoMath::ClosestAxisParam(
        Vec3d(0, 0, 0), Vec3d(1, 0, 0), MakeRay(Vec3d(-3, 1, 0), Vec3d(0, -1, 0)));
    ASSERT_TRUE(s.has_value());
    EXPECT_NEAR(*s, -3.0, 1e-9);
}

TEST(GizmoMath, ClosestAxisParamCarriesSignOnY)
{
    const auto s = GizmoMath::ClosestAxisParam(
        Vec3d(0, 0, 0), Vec3d(0, 1, 0), MakeRay(Vec3d(1, 2, 0), Vec3d(-1, 0, 0)));
    ASSERT_TRUE(s.has_value());
    EXPECT_NEAR(*s, 2.0, 1e-9);
}

TEST(GizmoMath, ClosestAxisParamCarriesSignOnZ)
{
    const auto s = GizmoMath::ClosestAxisParam(
        Vec3d(0, 0, 0), Vec3d(0, 0, 1), MakeRay(Vec3d(0, 1, 4), Vec3d(0, -1, 0)));
    ASSERT_TRUE(s.has_value());
    EXPECT_NEAR(*s, 4.0, 1e-9);
}

// A ray parallel to the axis is ill-conditioned: no usable parameter.
TEST(GizmoMath, ClosestAxisParamRejectsParallelRay)
{
    const auto s = GizmoMath::ClosestAxisParam(
        Vec3d(0, 0, 0), Vec3d(1, 0, 0), MakeRay(Vec3d(0, 1, 0), Vec3d(1, 0, 0)));
    EXPECT_FALSE(s.has_value());
}

// Snapping is absolute (to grid lines), not relative (to grid-sized steps): even
// a zero drag pulls an off-grid pivot onto the nearest line.
TEST(GizmoMath, SnapAxisOffsetIsAbsoluteAtZeroDrag)
{
    EXPECT_NEAR(GizmoMath::SnapAxisOffset(0.0, 0.3, 0.0, 1.0f), -0.3, 1e-9);
}

TEST(GizmoMath, SnapAxisOffsetLandsOnGridLine)
{
    // pivot 0.3 + drag 0.9 == 1.2, nearest line is 1.0, so the offset is 0.7.
    EXPECT_NEAR(GizmoMath::SnapAxisOffset(0.9, 0.3, 0.0, 1.0f), 0.7, 1e-9);
}

// The grid origin shifts the lines: with origin 0.25 a pivot at 0 snaps to 0.25.
TEST(GizmoMath, SnapAxisOffsetRespectsGridOrigin)
{
    EXPECT_NEAR(GizmoMath::SnapAxisOffset(0.0, 0.0, 0.25, 1.0f), 0.25, 1e-9);
}

// Non-positive spacing disables snapping: the raw offset passes through.
TEST(GizmoMath, SnapAxisOffsetDisabledPassesThrough)
{
    EXPECT_NEAR(GizmoMath::SnapAxisOffset(0.7, 5.0, 0.0, 0.0f), 0.7, 1e-12);
}
