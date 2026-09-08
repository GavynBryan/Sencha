#include <gtest/gtest.h>

#include "CameraRelativeSteeringSystem.h"

#include <cmath>
#include <numbers>

// The platformer's steering rule, on its own: up on the stick runs away from
// the camera, and the body faces where it runs.
TEST(PlatformerSteering, UpRunsAwayFromTheCamera)
{
    const Vec3d wish = CameraRelativeWish(0.0f, Vec2d{ 0.0f, 1.0f });
    EXPECT_NEAR(wish.X, 0.0f, 1e-5f);
    EXPECT_NEAR(wish.Z, -1.0f, 1e-5f) << "forward is -Z";
    EXPECT_NEAR(wish.Y, 0.0f, 1e-5f) << "steering is planar";
}

TEST(PlatformerSteering, TurningTheCameraTurnsTheWish)
{
    const float quarter = std::numbers::pi_v<float> * 0.5f;
    const Vec3d wish = CameraRelativeWish(quarter, Vec2d{ 0.0f, 1.0f });
    // A camera turned a quarter turn about up sends "forward" along -X.
    EXPECT_NEAR(wish.X, -1.0f, 1e-5f);
    EXPECT_NEAR(wish.Z, 0.0f, 1e-5f);
}

TEST(PlatformerSteering, ADiagonalIsClampedToUnitLength)
{
    const Vec3d wish = CameraRelativeWish(0.0f, Vec2d{ 1.0f, 1.0f });
    EXPECT_NEAR(wish.Magnitude(), 1.0f, 1e-5f);
}

TEST(PlatformerSteering, TheBodyFacesWhereItRuns)
{
    EXPECT_NEAR(FacingYawFor(Vec3d{ 0.0f, 0.0f, -1.0f }, 1.0f), 0.0f, 1e-5f);
    const float quarter = std::numbers::pi_v<float> * 0.5f;
    EXPECT_NEAR(FacingYawFor(Vec3d{ -1.0f, 0.0f, 0.0f }, 0.0f), quarter, 1e-5f);
}

TEST(PlatformerSteering, NoInputKeepsTheCurrentFacing)
{
    EXPECT_FLOAT_EQ(FacingYawFor(Vec3d::Zero(), 0.7f), 0.7f);
}
