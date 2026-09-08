#include <gtest/gtest.h>

#include "TankSteeringSystem.h"

#include <numbers>

// Tank controls, on their own: the vertical axis walks along the facing, the
// horizontal axis turns in place, and neither cares where the camera is.
TEST(TankSteering, ForwardWalksAlongTheFacing)
{
    const TankStep step = TankSteer(0.0f, Vec2d{ 0.0f, 1.0f }, 2.0f, 1.0f / 60.0f);
    EXPECT_NEAR(step.Wish.Z, -1.0f, 1e-5f) << "facing yaw zero is -Z";
    EXPECT_NEAR(step.Wish.X, 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(step.Yaw, 0.0f);
}

TEST(TankSteering, BackWalksBackwards)
{
    const TankStep step = TankSteer(0.0f, Vec2d{ 0.0f, -1.0f }, 2.0f, 1.0f / 60.0f);
    EXPECT_NEAR(step.Wish.Z, 1.0f, 1e-5f);
}

TEST(TankSteering, RightTurnsInPlaceByRateTimesTime)
{
    const TankStep step = TankSteer(0.0f, Vec2d{ 1.0f, 0.0f }, 2.0f, 0.5f);
    EXPECT_NEAR(step.Yaw, -1.0f, 1e-5f) << "a right push is a clockwise turn";
    EXPECT_NEAR(step.Wish.Magnitude(), 0.0f, 1e-5f) << "turning is not walking";
}

TEST(TankSteering, WalkingWhileTurningFollowsTheNewFacing)
{
    const float quarter = std::numbers::pi_v<float> * 0.5f;
    const TankStep step = TankSteer(quarter, Vec2d{ 0.0f, 1.0f }, 2.0f, 0.0f);
    EXPECT_NEAR(step.Wish.X, -1.0f, 1e-5f);
    EXPECT_NEAR(step.Wish.Z, 0.0f, 1e-5f);
}
