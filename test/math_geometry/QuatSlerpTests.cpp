#include <math/Quat.h>
#include <math/Vec.h>
#include <math/geometry/3d/Transform3d.h>

#include <gtest/gtest.h>

#include <numbers>

namespace
{
    constexpr float kPi = std::numbers::pi_v<float>;

    void ExpectQuatNear(const Quatf& actual, const Quatf& expected, float tolerance = 1e-5f)
    {
        // q and -q are the same orientation, so compare on the closer sign.
        const float sign = actual.Dot(expected) < 0.0f ? -1.0f : 1.0f;
        EXPECT_NEAR(actual.X * sign, expected.X, tolerance);
        EXPECT_NEAR(actual.Y * sign, expected.Y, tolerance);
        EXPECT_NEAR(actual.Z * sign, expected.Z, tolerance);
        EXPECT_NEAR(actual.W * sign, expected.W, tolerance);
    }
}

TEST(QuatSlerp, ReturnsEndpoints)
{
    const Quatf from = Quatf::FromAxisAngle(Vec3d::Up(), 0.0f);
    const Quatf to = Quatf::FromAxisAngle(Vec3d::Up(), kPi * 0.5f);

    ExpectQuatNear(Quatf::Slerp(from, to, 0.0f), from);
    ExpectQuatNear(Quatf::Slerp(from, to, 1.0f), to);
}

TEST(QuatSlerp, HalfwayIsHalfTheRotation)
{
    const Quatf from = Quatf::FromAxisAngle(Vec3d::Up(), 0.0f);
    const Quatf to = Quatf::FromAxisAngle(Vec3d::Up(), kPi * 0.5f);

    const Quatf mid = Quatf::Slerp(from, to, 0.5f);

    ExpectQuatNear(mid, Quatf::FromAxisAngle(Vec3d::Up(), kPi * 0.25f));
    EXPECT_NEAR(mid.Length(), 1.0f, 1e-5f);
}

TEST(QuatSlerp, TakesShortestPathAcrossNegatedInput)
{
    const Quatf from = Quatf::FromAxisAngle(Vec3d::Up(), 0.0f);
    // Same orientation as a small positive rotation, expressed with the
    // opposite sign. Without the shortest-path flip this blends the long way.
    const Quatf small = Quatf::FromAxisAngle(Vec3d::Up(), kPi * 0.25f);
    const Quatf negated = small * -1.0f;

    const Quatf mid = Quatf::Slerp(from, negated, 0.5f);

    ExpectQuatNear(mid, Quatf::FromAxisAngle(Vec3d::Up(), kPi * 0.125f));
}

TEST(QuatSlerp, NearParallelInputsStayNormalized)
{
    const Quatf from = Quatf::FromAxisAngle(Vec3d::Up(), 0.0f);
    const Quatf to = Quatf::FromAxisAngle(Vec3d::Up(), 1e-4f);

    const Quatf mid = Quatf::Slerp(from, to, 0.5f);

    EXPECT_NEAR(mid.Length(), 1.0f, 1e-5f);
    ExpectQuatNear(mid, Quatf::FromAxisAngle(Vec3d::Up(), 5e-5f), 1e-4f);
}

TEST(QuatSlerp, RotatesVectorsProgressively)
{
    const Quatf from = Quatf::FromAxisAngle(Vec3d::Up(), 0.0f);
    const Quatf to = Quatf::FromAxisAngle(Vec3d::Up(), kPi);

    const Vec3d start = Quatf::Slerp(from, to, 0.0f).RotateVector(Vec3d::Forward());
    const Vec3d quarter = Quatf::Slerp(from, to, 0.25f).RotateVector(Vec3d::Forward());
    const Vec3d half = Quatf::Slerp(from, to, 0.5f).RotateVector(Vec3d::Forward());

    // A quarter of a half-turn is 45 degrees off the start, half is 90.
    EXPECT_NEAR(start.Dot(quarter), std::cos(kPi * 0.25f), 1e-4f);
    EXPECT_NEAR(start.Dot(half), std::cos(kPi * 0.5f), 1e-4f);
}

TEST(Transform3dInterpolate, BlendsTranslationRotationAndScale)
{
    Transform3f from;
    from.Position = Vec3d(0.0f, 0.0f, 0.0f);
    from.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), 0.0f);
    from.Scale = Vec3d(1.0f, 1.0f, 1.0f);

    Transform3f to;
    to.Position = Vec3d(10.0f, 4.0f, -2.0f);
    to.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), kPi * 0.5f);
    to.Scale = Vec3d(3.0f, 3.0f, 3.0f);

    const Transform3f mid = Transform3f::Interpolate(from, to, 0.5f);

    EXPECT_NEAR(mid.Position.X, 5.0f, 1e-5f);
    EXPECT_NEAR(mid.Position.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(mid.Position.Z, -1.0f, 1e-5f);
    EXPECT_NEAR(mid.Scale.X, 2.0f, 1e-5f);
    ExpectQuatNear(mid.Rotation, Quatf::FromAxisAngle(Vec3d::Up(), kPi * 0.25f));
}

TEST(Transform3dInterpolate, ReturnsEndpointsExactly)
{
    Transform3f from;
    from.Position = Vec3d(1.0f, 2.0f, 3.0f);
    Transform3f to;
    to.Position = Vec3d(9.0f, 8.0f, 7.0f);

    const Transform3f atZero = Transform3f::Interpolate(from, to, 0.0f);
    const Transform3f atOne = Transform3f::Interpolate(from, to, 1.0f);

    EXPECT_NEAR(atZero.Position.X, 1.0f, 1e-6f);
    EXPECT_NEAR(atOne.Position.X, 9.0f, 1e-6f);
}
