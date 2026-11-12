#include <physics/MotionPrediction.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsWorld.h>

#include <gtest/gtest.h>

namespace
{
    void Box(PhysicsWorld& world, const Vec3d& centre, const Vec3d& half)
    {
        BodyDesc body;
        body.Shape = CollisionShape::MakeBox(half);
        body.Position = centre;
        body.Motion = BodyMotion::Static;
        body.Layer = CollisionLayer::Static;
        (void)world.AddBody(body);
    }
}

TEST(MotionPrediction, WalksOnAlongOpenGround)
{
    PhysicsWorld world;
    Box(world, Vec3d(0.0f, -0.5f, 0.0f), Vec3d(50.0f, 0.5f, 50.0f));
    const Vec3d ahead = PredictMotion(PhysicsQueries(world), Vec3d(0.0f, 1.0f, 0.0f), Vec3d(4.0f, 0.0f, 0.0f), 1.0f, {});
    EXPECT_NEAR(ahead.X, 4.0f, 1.0e-3f);
}

TEST(MotionPrediction, StopsShortOfAWallByItsBodyRadius)
{
    PhysicsWorld world;
    Box(world, Vec3d(0.0f, -0.5f, 0.0f), Vec3d(50.0f, 0.5f, 50.0f));
    Box(world, Vec3d(3.5f, 1.0f, 0.0f), Vec3d(0.5f, 2.0f, 5.0f));
    const Vec3d ahead = PredictMotion(PhysicsQueries(world), Vec3d(0.0f, 1.0f, 0.0f), Vec3d(8.0f, 0.0f, 0.0f), 1.0f, {});
    EXPECT_NEAR(ahead.X, 3.0f - MotionPredictionShape{}.BodyRadius, 1.0e-2f);
}

TEST(MotionPrediction, StopsAtTheLastStepWithGroundUnderIt)
{
    PhysicsWorld world;
    Box(world, Vec3d(1.0f, -0.5f, 0.0f), Vec3d(3.0f, 0.5f, 5.0f));
    const Vec3d ahead = PredictMotion(PhysicsQueries(world), Vec3d(0.0f, 1.0f, 0.0f), Vec3d(8.0f, 0.0f, 0.0f), 1.0f, {});
    EXPECT_LE(ahead.X, 4.0f);
    EXPECT_GE(ahead.X, 2.0f) << "one step of two metres is still over the floor";
}

TEST(MotionPrediction, SomethingStandingStillStaysPut)
{
    PhysicsWorld world;
    const Vec3d from(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(PredictMotion(PhysicsQueries(world), from, Vec3d(0.0f, -9.0f, 0.0f), 1.0f, {}), from);
}
