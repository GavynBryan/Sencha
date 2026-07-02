#include "viewport/GridFrame.h"

#include <math/spatial/GridPlane.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace
{
constexpr float kTolerance = 1e-5f;

void ExpectOrthonormal(const GridSettings& settings)
{
    EXPECT_NEAR(settings.AxisU.Magnitude(), 1.0f, kTolerance);
    EXPECT_NEAR(settings.AxisV.Magnitude(), 1.0f, kTolerance);
    EXPECT_NEAR(settings.AxisU.Dot(settings.AxisV), 0.0f, kTolerance);
}
}

TEST(GridFrame, FromFaceBuildsOrthonormalBasisInFacePlane)
{
    GridSettings settings;
    const Vec3d normal = Vec3d{ 1.0f, 1.0f, 0.0f }.Normalized(); // slanted face
    const Vec3d edge = { 0.0f, 0.0f, 2.0f };

    ASSERT_TRUE(GridFrame::FromFace({ 3.0f, 2.0f, 1.0f }, normal, edge, settings));
    EXPECT_EQ(settings.Origin, (Vec3d{ 3.0f, 2.0f, 1.0f }));
    ExpectOrthonormal(settings);
    // Both axes lie in the face plane.
    EXPECT_NEAR(settings.AxisU.Dot(normal), 0.0f, kTolerance);
    EXPECT_NEAR(settings.AxisV.Dot(normal), 0.0f, kTolerance);
    // U follows the reference edge.
    EXPECT_NEAR(settings.AxisU.Dot(edge.Normalized()), 1.0f, kTolerance);
}

TEST(GridFrame, FromFaceProjectsSkewedEdgeIntoPlane)
{
    GridSettings settings;
    const Vec3d normal = { 0.0f, 1.0f, 0.0f };
    // Edge not in the plane: its in-plane component drives U.
    ASSERT_TRUE(GridFrame::FromFace({}, normal, { 1.0f, 5.0f, 0.0f }, settings));
    ExpectOrthonormal(settings);
    EXPECT_NEAR(settings.AxisU.Dot(normal), 0.0f, kTolerance);
    EXPECT_NEAR(settings.AxisU.X, 1.0f, kTolerance);
}

TEST(GridFrame, FromFaceRejectsDegenerateInputs)
{
    GridSettings settings;
    const GridSettings before = settings;
    EXPECT_FALSE(GridFrame::FromFace({}, {}, { 1.0f, 0.0f, 0.0f }, settings));
    // Edge parallel to the normal has no in-plane component.
    EXPECT_FALSE(GridFrame::FromFace({}, { 0.0f, 1.0f, 0.0f }, { 0.0f, 3.0f, 0.0f }, settings));
    EXPECT_EQ(settings.Origin, before.Origin);
    EXPECT_EQ(settings.AxisU, before.AxisU);
    EXPECT_EQ(settings.AxisV, before.AxisV);
}

TEST(GridFrame, LongestEdgeDirectionPicksTheLongestSide)
{
    const std::array<Vec3d, 4> corners = {
        Vec3d{ 0.0f, 0.0f, 0.0f },
        Vec3d{ 4.0f, 0.0f, 0.0f },
        Vec3d{ 4.0f, 0.0f, 1.0f },
        Vec3d{ 0.0f, 0.0f, 1.0f },
    };
    const Vec3d edge = GridFrame::LongestEdgeDirection(corners);
    EXPECT_NEAR(std::abs(edge.Normalized().X), 1.0f, kTolerance);
}

TEST(GridFrame, RotateInPlaneQuarterTurnSwapsAxes)
{
    GridSettings settings; // default XZ frame, normal -Y (U x V = X x Z = -Y)
    GridFrame::RotateInPlane(settings, 90.0f);
    ExpectOrthonormal(settings);
    // U rotated by 90 degrees about the plane normal lands on +/-Z.
    EXPECT_NEAR(std::abs(settings.AxisU.Z), 1.0f, kTolerance);
    EXPECT_NEAR(settings.AxisU.X, 0.0f, kTolerance);

    // Four quarter turns come back to the start without drift.
    GridFrame::RotateInPlane(settings, 90.0f);
    GridFrame::RotateInPlane(settings, 90.0f);
    GridFrame::RotateInPlane(settings, 90.0f);
    EXPECT_NEAR(settings.AxisU.X, 1.0f, kTolerance);
    EXPECT_NEAR(settings.AxisU.Z, 0.0f, kTolerance);
}

TEST(GridFrame, RotatedFrameSnapsOnItsOwnLattice)
{
    GridSettings settings;
    settings.Origin = { 10.0f, 0.0f, 0.0f };
    GridFrame::RotateInPlane(settings, 45.0f);

    GridPlane plane;
    plane.Origin = settings.Origin;
    plane.AxisU = settings.AxisU;
    plane.AxisV = settings.AxisV;
    plane.Spacing = 2.0f;

    // A point one-and-a-bit steps along the rotated U snaps to exactly one step.
    const Vec3d target = settings.Origin + settings.AxisU * 2.3f;
    const Vec3d snapped = plane.Snap(target);
    const Vec3d expected = settings.Origin + settings.AxisU * 2.0f;
    EXPECT_NEAR((snapped - expected).Magnitude(), 0.0f, 1e-4f);
}
