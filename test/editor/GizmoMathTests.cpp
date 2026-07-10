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

// Factor f places the bound at pivot + (bound - pivot) * f; the snapped factor
// lands that point on the nearest grid line. Bound 2 about pivot 0 dragged to
// factor 1.35 (bound at 2.7) snaps to 3.0, factor 1.5.
TEST(GizmoMath, SnapScaleFactorLandsBoundOnGridLine)
{
    EXPECT_NEAR(GizmoMath::SnapScaleFactor(1.35, 0.0, 2.0, 0.0, 1.0f), 1.5, 1e-9);
}

// A non-zero pivot measures the extent from the pivot, not from world zero:
// pivot 1, bound 3 (extent 2) at factor 1.2 puts the bound at 3.4 -> 3.0 -> 1.0.
TEST(GizmoMath, SnapScaleFactorMeasuresExtentFromPivot)
{
    EXPECT_NEAR(GizmoMath::SnapScaleFactor(1.2, 1.0, 3.0, 0.0, 1.0f), 1.0, 1e-9);
}

// The grid origin shifts the lines the bound snaps to.
TEST(GizmoMath, SnapScaleFactorRespectsGridOrigin)
{
    // bound 2 at factor 1.0 sits at 2.0; lines at 0.5 + k -> nearest is 2.5.
    EXPECT_NEAR(GizmoMath::SnapScaleFactor(1.1, 0.0, 2.0, 0.5, 1.0f), 1.25, 1e-9);
}

// Disabled spacing and a degenerate (bound == pivot) extent pass through.
TEST(GizmoMath, SnapScaleFactorPassesThroughWhenUndefined)
{
    EXPECT_NEAR(GizmoMath::SnapScaleFactor(1.3, 0.0, 2.0, 0.0, 0.0f), 1.3, 1e-12);
    EXPECT_NEAR(GizmoMath::SnapScaleFactor(1.3, 2.0, 2.0, 0.0, 1.0f), 1.3, 1e-12);
}

TEST(GizmoMath, OnGridLatticeDetectsLatticePoints)
{
    EXPECT_TRUE(GizmoMath::OnGridLattice(4.0, 0.0, 1.0f, 1e-3));
    EXPECT_TRUE(GizmoMath::OnGridLattice(-3.0, 0.0, 1.0f, 1e-3));
    EXPECT_FALSE(GizmoMath::OnGridLattice(4.3, 0.0, 1.0f, 1e-3));
    // Offset origin shifts the lattice.
    EXPECT_TRUE(GizmoMath::OnGridLattice(4.5, 0.5, 1.0f, 1e-3));
    EXPECT_FALSE(GizmoMath::OnGridLattice(4.0, 0.5, 1.0f, 1e-3));
    // Tolerance boundary: within counts, beyond does not.
    EXPECT_TRUE(GizmoMath::OnGridLattice(4.0009, 0.0, 1.0f, 1e-3));
    EXPECT_FALSE(GizmoMath::OnGridLattice(4.002, 0.0, 1.0f, 1e-3));
    // No lattice without spacing.
    EXPECT_FALSE(GizmoMath::OnGridLattice(4.0, 0.0, 0.0f, 1e-3));
}

TEST(GizmoMath, SnapAxisOffsetWithOnLatticeReferenceYieldsSpacingMultiples)
{
    // Reference on the lattice: every snapped offset is a whole number of
    // cells, so all on-lattice vertices stay on the lattice together.
    const double offset = GizmoMath::SnapAxisOffset(2.3, /*coord*/ 4.0, /*origin*/ 0.0, 1.0f);
    EXPECT_DOUBLE_EQ(offset, 2.0);
    const double back = GizmoMath::SnapAxisOffset(-0.6, 4.0, 0.0, 1.0f);
    EXPECT_DOUBLE_EQ(back, -1.0);
}
