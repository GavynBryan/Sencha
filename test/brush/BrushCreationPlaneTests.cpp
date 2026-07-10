#include "document/interactions/BrushCreationPlane.h"

#include <math/geometry/3d/Aabb3d.h>
#include <math/Vec.h>

#include <gtest/gtest.h>

// These cover the pure depth-axis placement math for brush creation. The full
// resolver (ResolveBrushCreationPlane) needs the viewport/picking/ImGui stack and
// is exercised manually; the math below is what regresses, so it is unit-tested.

// Rest-on-grid: bottom sits on the grid line, one cell tall, extruded toward the
// camera (the sign of towardCamera).
TEST(BrushDepthPlacement, RestOnGridSitsOnTheGridLine)
{
    const BrushDepthPlacement up = RestOnGridDepth(/*gridDepth*/ 0.0f, /*spacing*/ 1.0f, /*toward*/ 1.0f);
    EXPECT_FLOAT_EQ(up.Half, 0.5f);
    EXPECT_FLOAT_EQ(up.Center, 0.5f);
    EXPECT_FLOAT_EQ(up.Center - up.Half, 0.0f); // bottom on the grid
    EXPECT_FLOAT_EQ(up.Center + up.Half, 1.0f); // one cell tall

    const BrushDepthPlacement down = RestOnGridDepth(0.0f, 1.0f, -1.0f);
    EXPECT_FLOAT_EQ(down.Center, -0.5f);
    EXPECT_FLOAT_EQ(down.Center + down.Half, 0.0f); // top on the grid (extrudes away)
}

TEST(BrushDepthPlacement, RestOnGridScalesWithSpacingAndOffset)
{
    const BrushDepthPlacement p = RestOnGridDepth(/*gridDepth*/ 3.0f, /*spacing*/ 2.0f, /*toward*/ 1.0f);
    EXPECT_FLOAT_EQ(p.Half, 1.0f);                 // half a cell
    EXPECT_FLOAT_EQ(p.Center - p.Half, 3.0f);      // bottom on the (offset) grid line
    EXPECT_FLOAT_EQ(p.Center + p.Half, 5.0f);      // one cell (spacing) tall
}

// Rest-on-surface: flush against the hit point, extruded one cell along the face
// normal's direction.
TEST(BrushDepthPlacement, RestOnSurfaceSitsOnTheHitPoint)
{
    const BrushDepthPlacement above = RestOnSurfaceDepth(/*hitDepth*/ 3.0f, /*spacing*/ 1.0f, /*normal*/ 1.0f);
    EXPECT_FLOAT_EQ(above.Half, 0.5f);
    EXPECT_FLOAT_EQ(above.Center - above.Half, 3.0f); // bottom flush on the surface
    EXPECT_FLOAT_EQ(above.Center + above.Half, 4.0f); // extrudes outward along +normal

    const BrushDepthPlacement below = RestOnSurfaceDepth(3.0f, 1.0f, -1.0f);
    EXPECT_FLOAT_EQ(below.Center + below.Half, 3.0f); // flush on the surface
    EXPECT_FLOAT_EQ(below.Center - below.Half, 2.0f); // extrudes along -normal
}

// Copy-from-selection: match the selected brush's bounds on the depth axis.
TEST(BrushDepthPlacement, CopyFromBoundsMatchesSelectedExtent)
{
    const Aabb3d bounds(Vec3d(-1.0f, 2.0f, -5.0f), Vec3d(1.0f, 8.0f, 5.0f));

    const BrushDepthPlacement y = CopyDepthFromBounds(bounds, /*depthAxis*/ 1);
    EXPECT_FLOAT_EQ(y.Center, 5.0f); // (2 + 8) / 2
    EXPECT_FLOAT_EQ(y.Half, 3.0f);   // (8 - 2) / 2

    const BrushDepthPlacement x = CopyDepthFromBounds(bounds, /*depthAxis*/ 0);
    EXPECT_FLOAT_EQ(x.Center, 0.0f);
    EXPECT_FLOAT_EQ(x.Half, 1.0f);
}

// The rest sign records which side of the rest plane the extent grew toward, so
// a pending brush switched between primitive classes can keep resting there.
TEST(BrushDepthPlacement, RestSignFollowsTheExtrusionSide)
{
    EXPECT_FLOAT_EQ(RestOnGridDepth(0.0f, 1.0f, 1.0f).Sign, 1.0f);
    EXPECT_FLOAT_EQ(RestOnGridDepth(0.0f, 1.0f, -1.0f).Sign, -1.0f);
    EXPECT_FLOAT_EQ(RestOnSurfaceDepth(3.0f, 1.0f, 1.0f).Sign, 1.0f);
    EXPECT_FLOAT_EQ(RestOnSurfaceDepth(3.0f, 1.0f, -1.0f).Sign, -1.0f);
    // Copied bounds have no natural rest side; +1 by convention.
    const Aabb3d bounds(Vec3d(-1.0f, 2.0f, -5.0f), Vec3d(1.0f, 8.0f, 5.0f));
    EXPECT_FLOAT_EQ(CopyDepthFromBounds(bounds, 1).Sign, 1.0f);
}

// Pending regeneration: a solid keeps the drag's own depth (a thin copied depth
// must not be re-floored), takes the grid floor only when the drag authored zero
// depth, and a Plane is always a zero-thickness sheet.
TEST(BrushDepthPlacement, PendingDepthHalfKeepsDragDepthForSolids)
{
    EXPECT_FLOAT_EQ(PendingDepthHalf(/*solid*/ true, /*drag*/ 0.1f, /*floor*/ 0.5f), 0.1f);
    EXPECT_FLOAT_EQ(PendingDepthHalf(true, 2.0f, 0.5f), 2.0f);
}

TEST(BrushDepthPlacement, PendingDepthHalfFloorsZeroDepthDragOnly)
{
    EXPECT_FLOAT_EQ(PendingDepthHalf(true, 0.0f, 0.5f), 0.5f);
}

TEST(BrushDepthPlacement, PendingDepthHalfPlaneIsFlushZeroDepth)
{
    EXPECT_FLOAT_EQ(PendingDepthHalf(false, 2.0f, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(PendingDepthHalf(false, 0.0f, 0.5f), 0.0f);
}

TEST(BrushDepthPlacement, PendingDepthHalfRoundTripsPlaneSolidPlane)
{
    // Plane-authored drag: Plane -> Box -> Plane regenerates the same halves
    // each time (the transition shift is the caller's job; the half is pure).
    const float boxHalf = PendingDepthHalf(true, 0.0f, 0.5f);
    EXPECT_FLOAT_EQ(boxHalf, 0.5f);
    EXPECT_FLOAT_EQ(PendingDepthHalf(false, 0.0f, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(PendingDepthHalf(true, 0.0f, 0.5f), boxHalf);
}
