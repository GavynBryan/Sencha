#include <gtest/gtest.h>

#include <physics/PhysicsWorld.h>

namespace
{
BodyDesc DynamicSphereAt(float x)
{
    BodyDesc sphere;
    sphere.Shape = CollisionShape::MakeSphere(0.5f);
    sphere.Position = Vec3d(x, 0.0f, 0.0f);
    sphere.Motion = BodyMotion::Dynamic;
    sphere.Layer = CollisionLayer::Moving;
    return sphere;
}
}

TEST(PhysicsCapacity, ABodyPastTheConfiguredLimitIsCounted)
{
    PhysicsWorld world(PhysicsWorldConfig{ .MaxBodies = 2 });

    EXPECT_TRUE(world.AddBody(DynamicSphereAt(0.0f)).IsValid());
    EXPECT_TRUE(world.AddBody(DynamicSphereAt(5.0f)).IsValid());
    EXPECT_FALSE(world.AddBody(DynamicSphereAt(10.0f)).IsValid());

    EXPECT_EQ(world.GetCapacityFailures().BodiesRefused, 1u);
}

TEST(PhysicsCapacity, ContactsPastTheConfiguredLimitAreCounted)
{
    PhysicsWorld world(PhysicsWorldConfig{ .MaxBodyPairs = 4, .MaxContactConstraints = 4 });
    for (int i = 0; i < 16; ++i)
        ASSERT_TRUE(world.AddBody(DynamicSphereAt(static_cast<float>(i) * 0.1f)).IsValid());

    world.Step(1.0f / 60.0f, 1);

    EXPECT_GT(world.GetCapacityFailures().OverflowedSteps, 0u);
}

TEST(PhysicsCapacity, AWorldWithinItsLimitsReportsNothing)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.AddBody(DynamicSphereAt(0.0f)).IsValid());

    world.Step(1.0f / 60.0f, 1);

    EXPECT_EQ(world.GetCapacityFailures().BodiesRefused, 0u);
    EXPECT_EQ(world.GetCapacityFailures().OverflowedSteps, 0u);
}
