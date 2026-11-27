#include <gtest/gtest.h>

#include <math/Angle.h>
#include <math/geometry/3d/ApertureFrame.h>

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr float kClose = 1.0e-4f;

    Transform3f Pose(Vec3d position, Quatf rotation = Quatf::Identity())
    {
        return Transform3f{ position, rotation, Vec3d::One() };
    }

    ApertureFrame Frame(Vec3d position, Quatf rotation = Quatf::Identity(), Vec2d half = Vec2d{ 1.0f, 2.0f })
    {
        return MakeApertureFrame(Pose(position, rotation), half);
    }

    Quatf Yaw(float degrees) { return Quatf::FromAxisAngle(Vec3d::Up(), ToRadians(degrees)); }
    Quatf Pitch(float degrees) { return Quatf::FromAxisAngle(Vec3d::Right(), ToRadians(degrees)); }
    Quatf Roll(float radians) { return Quatf::FromAxisAngle(Vec3d::Forward(), radians); }

    void ExpectNear(const Vec3d& actual, const Vec3d& expected, const std::string& what, float tolerance = kClose)
    {
        EXPECT_NEAR(actual.X, expected.X, tolerance) << what;
        EXPECT_NEAR(actual.Y, expected.Y, tolerance) << what;
        EXPECT_NEAR(actual.Z, expected.Z, tolerance) << what;
    }
}

TEST(ApertureFrame, ForwardIsTheFrontNormalAndRightAndUpAreTheRectangleAxes)
{
    const ApertureFrame frame = Frame(Vec3d{ 1, 2, 3 }, Yaw(90.0f));
    ExpectNear(frame.Origin, Vec3d{ 1, 2, 3 }, "origin");
    ExpectNear(frame.Normal, Vec3d{ -1, 0, 0 }, "a quarter yaw turns forward from -Z to -X");
    ExpectNear(frame.Right, Vec3d{ 0, 0, -1 }, "right");
    ExpectNear(frame.Up, Vec3d{ 0, 1, 0 }, "up");
    EXPECT_GT(ApertureSignedDistance(frame, Vec3d{ 0, 2, 3 }), 0.0f) << "the side forward points to is the front";
    const Vec2d coordinates = ApertureCoordinates(frame, Vec3d{ 1, 3, 2.5f });
    EXPECT_NEAR(coordinates.X, 0.5f, kClose);
    EXPECT_NEAR(coordinates.Y, 1.0f, kClose);
}

TEST(ApertureFrame, PlaneSideToleranceSplitsFrontOnAndBack)
{
    const ApertureFrame frame = Frame(Vec3d::Zero());
    constexpr float tolerance = ApertureTolerance::PlaneSide;
    struct Case
    {
        float Distance;
        ApertureSide Expected;
    };
    const Case cases[] = {
        { 2.0f * tolerance, ApertureSide::Front },  { tolerance, ApertureSide::On },
        { 0.5f * tolerance, ApertureSide::On },     { 0.0f, ApertureSide::On },
        { -tolerance, ApertureSide::On },           { -2.0f * tolerance, ApertureSide::Back },
    };
    for (const Case& c : cases)
    {
        const Vec3d point = frame.Normal * c.Distance;
        EXPECT_EQ(ClassifyApertureSide(frame, point), c.Expected) << "distance " << c.Distance;
    }
}

TEST(ApertureFrame, RectangleEdgeToleranceIsExplicit)
{
    const ApertureFrame frame = Frame(Vec3d::Zero());
    constexpr float tolerance = ApertureTolerance::RectangleEdge;
    struct Case
    {
        Vec2d Coordinates;
        bool Inside;
    };
    const Case cases[] = {
        { Vec2d{ 0.0f, 0.0f }, true },
        { Vec2d{ 1.0f, 2.0f }, true },
        { Vec2d{ -1.0f, -2.0f }, true },
        { Vec2d{ 1.0f + 0.5f * tolerance, 0.0f }, true },
        { Vec2d{ 1.0f + tolerance, 0.0f }, true },
        { Vec2d{ 1.0f + 2.0f * tolerance, 0.0f }, false },
        { Vec2d{ 0.0f, -2.0f - 2.0f * tolerance }, false },
        { Vec2d{ 1.0f + 2.0f * tolerance, 2.0f + 2.0f * tolerance }, false },
    };
    for (const Case& c : cases)
        EXPECT_EQ(InsideApertureRectangle(frame, c.Coordinates, tolerance), c.Inside)
            << c.Coordinates.X << ", " << c.Coordinates.Y;
    EXPECT_FALSE(InsideApertureRectangle(frame, Vec2d{ 1.5f, 0.0f }, 0.0f));
    EXPECT_TRUE(InsideApertureRectangle(frame, Vec2d{ 1.5f, 0.0f }, 0.5f)) << "the caller's tolerance is the one used";
}

TEST(ApertureFrame, SupportIntervalsSpanTheShapeAlongTheNormal)
{
    const ApertureFrame frame = Frame(Vec3d::Zero());
    const ApertureInterval sphere = SphereSupportInterval(frame, frame.Normal * 3.0f, 1.0f);
    EXPECT_NEAR(sphere.Near, 2.0f, kClose);
    EXPECT_NEAR(sphere.Far, 4.0f, kClose);

    const ApertureInterval capsule = CapsuleSupportInterval(frame, frame.Normal * 1.0f, frame.Normal * -2.0f, 0.5f);
    EXPECT_NEAR(capsule.Near, -2.5f, kClose);
    EXPECT_NEAR(capsule.Far, 1.5f, kClose);

    // An upright capsule against a floor aperture spans its height.
    const ApertureFrame floor = Frame(Vec3d::Zero(), Pitch(90.0f));
    const ApertureInterval standing =
        CapsuleSupportInterval(floor, Vec3d{ 0, 1.0f, 0 }, Vec3d{ 0, 2.0f, 0 }, 0.4f);
    EXPECT_NEAR(standing.Near, 0.6f, kClose);
    EXPECT_NEAR(standing.Far, 2.4f, kClose);
}

TEST(ApertureFrame, SegmentCrossings)
{
    const ApertureFrame wall = Frame(Vec3d::Zero());
    const ApertureFrame floor = Frame(Vec3d::Zero(), Pitch(90.0f));
    const Vec3d front = wall.Normal;
    constexpr float edge = ApertureTolerance::RectangleEdge;
    struct Case
    {
        const char* Name;
        const ApertureFrame* Frame;
        Vec3d From;
        Vec3d To;
        std::optional<float> Fraction;
        ApertureCrossingDirection Direction = ApertureCrossingDirection::FrontToBack;
    };
    const Case cases[] = {
        { "head on", &wall, front, -front, 0.5f },
        { "oblique", &wall, Vec3d{ -0.5f, 0.0f, 0.0f } + front * 1.0f, Vec3d{ 0.5f, 1.0f, 0.0f } - front * 3.0f, 0.25f },
        { "back to front", &wall, -front * 2.0f, front * 2.0f, 0.5f, ApertureCrossingDirection::BackToFront },
        { "vertical drop", &floor, Vec3d{ 0.2f, 3.0f, 0.1f }, Vec3d{ 0.2f, -1.0f, 0.1f }, 0.75f },
        { "upside down is the floor from below", &floor, Vec3d{ 0, -1.0f, 0 }, Vec3d{ 0, 1.0f, 0 }, 0.5f,
          ApertureCrossingDirection::BackToFront },
        { "near parallel", &wall, Vec3d{ -0.9f, 0, 0 } + front * 1.0e-3f, Vec3d{ 0.9f, 0, 0 } - front * 1.0e-3f, 0.5f },
        { "high speed", &wall, front * 1000.0f, -front * 1000.0f, 0.5f },
        { "edge just inside", &wall, Vec3d{ 1.0f + 0.5f * edge, 0, 0 } + front, Vec3d{ 1.0f + 0.5f * edge, 0, 0 } - front,
          0.5f },
        { "edge just outside", &wall, Vec3d{ 1.0f + 4.0f * edge, 0, 0 } + front,
          Vec3d{ 1.0f + 4.0f * edge, 0, 0 } - front, std::nullopt },
        { "corner", &wall, Vec3d{ 1.0f, 2.0f, 0 } + front, Vec3d{ 1.0f, 2.0f, 0 } - front, 0.5f },
        { "beside the rectangle", &wall, Vec3d{ 3.0f, 0, 0 } + front, Vec3d{ 3.0f, 0, 0 } - front, std::nullopt },
        { "starts on the plane, goes behind", &wall, Vec3d::Zero(), -front, 0.0f },
        { "starts on the plane, stays in front", &wall, Vec3d::Zero(), front, std::nullopt },
        { "ends on the plane from the front", &wall, front, Vec3d::Zero(), std::nullopt },
        { "ends on the plane from behind", &wall, -front, Vec3d::Zero(), 1.0f,
          ApertureCrossingDirection::BackToFront },
        { "lies in the plane", &wall, Vec3d{ -0.5f, 0, 0 }, Vec3d{ 0.5f, 0, 0 }, std::nullopt },
        { "stays in front", &wall, front * 2.0f, front, std::nullopt },
    };
    for (const Case& c : cases)
    {
        const std::optional<ApertureCrossing> crossing = CrossAperture(*c.Frame, c.From, c.To);
        ASSERT_EQ(crossing.has_value(), c.Fraction.has_value()) << c.Name;
        if (!crossing)
            continue;
        EXPECT_NEAR(crossing->Fraction, *c.Fraction, kClose) << c.Name;
        EXPECT_EQ(crossing->Direction, c.Direction) << c.Name;
        ExpectNear(crossing->Point, c.From + (c.To - c.From) * crossing->Fraction, c.Name);
        EXPECT_NEAR(ApertureSignedDistance(*c.Frame, crossing->Point), 0.0f, 1.0e-3f) << c.Name;
    }
}

TEST(ApertureFrame, AFacingPairMapsByTheExactHalfTurn)
{
    // B stands ten metres along X facing the same way as A: entering A's front
    // and leaving B's front turns a traveller half around about up.
    const ApertureFrame a = Frame(Vec3d::Zero());
    const ApertureFrame b = Frame(Vec3d{ 10, 0, 0 });
    const Transform3f aToB = ApertureMapping(a, b);
    EXPECT_TRUE(aToB.Rotation.NearlyEquals(Quatf{ 0, 1, 0, 0 }, kClose)
                || aToB.Rotation.NearlyEquals(Quatf{ 0, -1, 0, 0 }, kClose));
    ExpectNear(aToB.Position, Vec3d{ 10, 0, 0 }, "translation");
    ExpectNear(aToB.Scale, Vec3d::One(), "rigid");

    const Vec3d behindA{ 0.2f, 0.5f, 0.3f };
    ExpectNear(aToB.TransformPoint(behindA), Vec3d{ 9.8f, 0.5f, -0.3f }, "behind A lands in front of B, mirrored");
    EXPECT_NEAR(ApertureSignedDistance(b, aToB.TransformPoint(behindA)), -ApertureSignedDistance(a, behindA), kClose);
}

TEST(ApertureFrame, MappingCarriesEntryOntoExitInEveryOrientation)
{
    struct Case
    {
        const char* Name;
        Transform3f A;
        Transform3f B;
        bool Upright;
    };
    const Case cases[] = {
        { "facing each other", Pose(Vec3d::Zero()), Pose(Vec3d{ 0, 0, -10 }, Yaw(180.0f)), true },
        { "a quarter yaw", Pose(Vec3d{ 1, 0, 2 }), Pose(Vec3d{ 30, 4, 0 }, Yaw(90.0f)), true },
        { "a half yaw", Pose(Vec3d{ 0, 0, 5 }, Yaw(180.0f)), Pose(Vec3d{ 50, 0, 5 }), true },
        { "wall to floor", Pose(Vec3d::Zero()), Pose(Vec3d{ 0, -20, 0 }, Pitch(90.0f)), false },
        { "floor to wall", Pose(Vec3d::Zero(), Pitch(90.0f)), Pose(Vec3d{ 7, 0, 0 }, Yaw(45.0f)), false },
        { "upside down", Pose(Vec3d::Zero()), Pose(Vec3d{ 0, 10, 0 }, Roll(ToRadians(180.0f))), false },
        { "nearly parallel planes", Pose(Vec3d::Zero()), Pose(Vec3d{ 3, 0, 0 }, Yaw(0.01f)), true },
    };
    for (const Case& c : cases)
    {
        const ApertureFrame a = MakeApertureFrame(c.A, Vec2d{ 1, 2 });
        const ApertureFrame b = MakeApertureFrame(c.B, Vec2d{ 1, 2 });
        const Transform3f aToB = ApertureMapping(a, b);
        const Transform3f bToA = ApertureMapping(b, a);
        const Quatf& turn = aToB.Rotation;
        ExpectNear(aToB.TransformPoint(a.Origin), b.Origin, std::string(c.Name) + ": origin onto origin");
        ExpectNear(turn.RotateVector(-a.Normal), b.Normal, std::string(c.Name) + ": into A becomes out of B");
        ExpectNear(turn.RotateVector(a.Up), b.Up, std::string(c.Name) + ": up onto up");
        ExpectNear(turn.RotateVector(a.Right), -b.Right, std::string(c.Name) + ": right onto left");

        const Vec3d x = turn.RotateVector(Vec3d::Right());
        const Vec3d y = turn.RotateVector(Vec3d::Up());
        ExpectNear(x.Cross(y), turn.RotateVector(Vec3d::Backward()), std::string(c.Name) + ": right handed");

        const Vec3d point = a.Origin + a.Right * 0.3f + a.Up * -0.7f - a.Normal * 0.25f;
        const Vec3d mapped = aToB.TransformPoint(point);
        EXPECT_NEAR(ApertureSignedDistance(b, mapped), 0.25f, kClose) << c.Name;
        const Vec2d before = ApertureCoordinates(a, point);
        const Vec2d after = ApertureCoordinates(b, mapped);
        EXPECT_NEAR(after.X, -before.X, kClose) << c.Name;
        EXPECT_NEAR(after.Y, before.Y, kClose) << c.Name;

        ExpectNear(bToA.TransformPoint(mapped), point, std::string(c.Name) + ": inverse round trip", 1.0e-3f);
        EXPECT_TRUE((bToA.Rotation * turn).NearlyEquals(Quatf::Identity(), kClose)
                    || (bToA.Rotation * turn).NearlyEquals(-Quatf::Identity(), kClose))
            << c.Name;
        EXPECT_EQ(IsUprightMapping(aToB), c.Upright) << c.Name;
        EXPECT_EQ(IsUprightMapping(bToA), c.Upright) << c.Name;
    }
}

TEST(ApertureFrame, VelocitiesAndOrientationTurnWithTheMapping)
{
    const ApertureFrame a = Frame(Vec3d::Zero());
    const ApertureFrame b = Frame(Vec3d{ 20, 0, 0 }, Yaw(90.0f));
    const Transform3f aToB = ApertureMapping(a, b);

    const Vec3d velocity = -a.Normal * 5.0f + a.Up * 1.0f;
    ExpectNear(aToB.Rotation.RotateVector(velocity), b.Normal * 5.0f + b.Up * 1.0f, "linear velocity");

    const Vec3d spinAboutUp = a.Up * 2.0f;
    ExpectNear(aToB.Rotation.RotateVector(spinAboutUp), b.Up * 2.0f, "angular velocity about up");
    const Vec3d tumble = a.Right * 3.0f;
    ExpectNear(aToB.Rotation.RotateVector(tumble), -b.Right * 3.0f, "angular velocity about right");

    // Facing into A, a traveller faces out of B.
    const Quatf facingIntoA = Yaw(180.0f);
    ExpectNear(facingIntoA.RotateVector(Vec3d::Forward()), -a.Normal, "facing into A");
    ExpectNear((aToB.Rotation * facingIntoA).RotateVector(Vec3d::Forward()), b.Normal, "orientation");
}

TEST(ApertureFrame, UprightToleranceIsJudgedOnTheImageOfUp)
{
    // Rolling B about its normal tilts the image of up by the roll angle.
    constexpr float tolerance = ApertureTolerance::Upright;
    const ApertureFrame a = Frame(Vec3d::Zero());
    const auto rolled = [&](float radians) { return ApertureMapping(a, Frame(Vec3d{ 5, 0, 0 }, Roll(radians))); };
    EXPECT_TRUE(IsUprightMapping(rolled(0.0f)));
    EXPECT_TRUE(IsUprightMapping(rolled(0.9f * tolerance)));
    EXPECT_FALSE(IsUprightMapping(rolled(1.1f * tolerance)));
    const Transform3f atTolerance = rolled(2.0f * std::asin(0.5f * tolerance));
    const float tilt = (atTolerance.Rotation.RotateVector(Vec3d::Up()) - Vec3d::Up()).Magnitude();
    EXPECT_NEAR(tilt, tolerance, 1.0e-5f) << "the tilt is measured as the length the test sets";
}

TEST(ApertureFrame, ValidFramesAreFiniteRigidAndHavePositiveExtents)
{
    constexpr float scale = ApertureTolerance::UnitScale;
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    struct Case
    {
        const char* Name;
        Transform3f World;
        Vec2d Half;
        ApertureFrameFault Expected;
    };
    Transform3f scaledInside = Pose(Vec3d::Zero());
    scaledInside.Scale = Vec3d{ 1.0f + 0.5f * scale, 1.0f - 0.5f * scale, 1.0f };
    Transform3f scaledOutside = Pose(Vec3d::Zero());
    scaledOutside.Scale = Vec3d{ 1.0f, 1.0f + 2.0f * scale, 1.0f };
    Transform3f unnormalized = Pose(Vec3d::Zero());
    unnormalized.Rotation = Yaw(30.0f) * 3.0f;
    Transform3f zeroRotation = Pose(Vec3d::Zero());
    zeroRotation.Rotation = Quatf{ 0, 0, 0, 0 };
    Transform3f nanRotation = Pose(Vec3d::Zero());
    nanRotation.Rotation = Quatf{ nan, 0, 0, 1 };
    const Case cases[] = {
        { "identity", Pose(Vec3d::Zero()), Vec2d{ 1, 1 }, ApertureFrameFault::None },
        { "smallest extent", Pose(Vec3d::Zero()), Vec2d{ 1.0e-6f, 1.0e-6f }, ApertureFrameFault::None },
        { "very wide", Pose(Vec3d::Zero()), Vec2d{ 1.0e4f, 0.5f }, ApertureFrameFault::None },
        { "scale just inside", scaledInside, Vec2d{ 1, 1 }, ApertureFrameFault::None },
        { "scale just outside", scaledOutside, Vec2d{ 1, 1 }, ApertureFrameFault::ScaleNotUnit },
        { "unnormalized rotation", unnormalized, Vec2d{ 1, 1 }, ApertureFrameFault::None },
        { "zero rotation", zeroRotation, Vec2d{ 1, 1 }, ApertureFrameFault::DegenerateRotation },
        { "nan rotation", nanRotation, Vec2d{ 1, 1 }, ApertureFrameFault::NonFiniteTransform },
        { "infinite position", Pose(Vec3d{ inf, 0, 0 }), Vec2d{ 1, 1 }, ApertureFrameFault::NonFiniteTransform },
        { "zero extent", Pose(Vec3d::Zero()), Vec2d{ 0, 1 }, ApertureFrameFault::InvalidExtents },
        { "negative extent", Pose(Vec3d::Zero()), Vec2d{ 1, -1 }, ApertureFrameFault::InvalidExtents },
        { "nan extent", Pose(Vec3d::Zero()), Vec2d{ nan, 1 }, ApertureFrameFault::InvalidExtents },
        { "infinite extent", Pose(Vec3d::Zero()), Vec2d{ 1, inf }, ApertureFrameFault::InvalidExtents },
    };
    for (const Case& c : cases)
        EXPECT_EQ(CheckApertureFrame(c.World, c.Half), c.Expected) << c.Name;

    const ApertureFrame frame = MakeApertureFrame(unnormalized, Vec2d{ 1, 1 });
    EXPECT_NEAR(frame.Normal.Magnitude(), 1.0f, kClose) << "the frame is built from the normalized rotation";
    ExpectNear(frame.Normal, Yaw(30.0f).RotateVector(Vec3d::Forward()), "normal");
}
