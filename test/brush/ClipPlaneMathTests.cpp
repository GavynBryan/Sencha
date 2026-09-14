#include "viewport/ClipPlaneMath.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

namespace
{
constexpr float kTol = 1e-4f;

void ExpectOnPlane(const Plane& plane, Vec3d p)
{
    EXPECT_NEAR(plane.SignedDistanceTo(p), 0.0f, kTol);
}
}

TEST(ClipPlaneMath, ALineStandsAPlaneAlongTheDirection)
{
    const Vec3d a{ 0, 0, 0 };
    const Vec3d b{ 2, 0, 1 };
    const Vec3d view{ 0, -1, 0 }; // looking down
    const std::optional<Plane> plane = ClipPlaneMath::ThroughLineAndDirection(a, b, view);
    ASSERT_TRUE(plane.has_value());
    ExpectOnPlane(*plane, a);
    ExpectOnPlane(*plane, b);
    ExpectOnPlane(*plane, a + view * 5.0f);
    EXPECT_NEAR(plane->Normal.Dot(view), 0.0f, kTol);
    EXPECT_NEAR(plane->Normal.Magnitude(), 1.0f, kTol);
    // Drawing the other way flips front and back.
    const std::optional<Plane> reversed = ClipPlaneMath::ThroughLineAndDirection(b, a, view);
    ASSERT_TRUE(reversed.has_value());
    EXPECT_NEAR(reversed->Normal.Dot(plane->Normal), -1.0f, kTol);
}

TEST(ClipPlaneMath, DegenerateLinesStandNoPlane)
{
    EXPECT_FALSE(ClipPlaneMath::ThroughLineAndDirection({ 0, 0, 0 }, { 0, 0, 0 }, { 0, -1, 0 }).has_value());
    // Along the view: no plane is perpendicular to the screen through it.
    EXPECT_FALSE(ClipPlaneMath::ThroughLineAndDirection({ 0, 0, 0 }, { 0, -3, 0 }, { 0, -1, 0 }).has_value());
}

namespace
{
// Every transform a brush can carry: the local plane must contain the local
// image of every world point on the plane, and world Front must stay Front.
void ExpectLocalPlaneFaithful(const Transform3f& transform)
{
    const Plane world = Plane::FromNormalAndPoint(Vec3d{ 1, 0, 0 }, Vec3d{ 0.5f, 0, 0 });
    const Vec3d front{ 3.0f, 0.2f, -0.4f };
    const Plane local = ClipPlaneMath::InLocal(world, front, transform);
    for (const Vec3d& p : { Vec3d{ 0.5f, 0, 0 }, Vec3d{ 0.5f, 2, 0 }, Vec3d{ 0.5f, -1, 3 } })
        ExpectOnPlane(local, InverseTransformPoint(transform, p));
    EXPECT_GT(local.SignedDistanceTo(InverseTransformPoint(transform, front)), 0.0f);
    EXPECT_LT(local.SignedDistanceTo(InverseTransformPoint(transform, Vec3d{ -3, 0, 0 })), 0.0f);
}
}

TEST(ClipPlaneMath, TheLocalPlaneIsFaithfulUnderRotationAndScale)
{
    Transform3f rotated = Transform3f::Identity();
    rotated.Position = Vec3d{ 1, 2, 3 };
    rotated.Rotation = Quatf::FromAxisAngle(Vec3d{ 0, 1, 0 }, std::numbers::pi_v<float> * 0.3f);
    rotated.Scale = Vec3d{ 2.0f, 0.5f, 3.0f };
    ExpectLocalPlaneFaithful(rotated);
}

TEST(ClipPlaneMath, AMirroredBrushKeepsWorldFrontAsFront)
{
    Transform3f mirrored = Transform3f::Identity();
    mirrored.Scale = Vec3d{ -1.0f, 1.0f, 1.0f };
    ExpectLocalPlaneFaithful(mirrored);
    Transform3f mirroredTwice = Transform3f::Identity();
    mirroredTwice.Scale = Vec3d{ -2.0f, 1.0f, -0.5f };
    mirroredTwice.Rotation = Quatf::FromAxisAngle(Vec3d{ 1, 0, 0 }, 0.7f);
    ExpectLocalPlaneFaithful(mirroredTwice);
}
