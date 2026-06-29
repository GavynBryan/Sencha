// Headless physics tests. They include only the engine's physics facade, never
// a Jolt header, which is the firewall working: gameplay-side code (and tests)
// see engine types only. Single-threaded backend -> deterministic.

#include <gtest/gtest.h>

#include <physics/PhysicsWorld.h>

namespace
{
PhysicsBodyId AddFloor(PhysicsWorld& world)
{
    BodyDesc floor;
    floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
    floor.Position = Vec3d(0.0f, 0.0f, 0.0f); // top surface at y = 0.5
    floor.Motion = BodyMotion::Static;
    floor.Layer = CollisionLayer::Static;
    return world.AddBody(floor);
}

PhysicsBodyId AddFallingSphere(PhysicsWorld& world, float startHeight)
{
    BodyDesc sphere;
    sphere.Shape = CollisionShape::MakeSphere(0.5f);
    sphere.Position = Vec3d(0.0f, startHeight, 0.0f);
    sphere.Motion = BodyMotion::Dynamic;
    sphere.Layer = CollisionLayer::Moving;
    sphere.Mass = 1.0f;
    return world.AddBody(sphere);
}

constexpr float kFixedDt = 1.0f / 60.0f;
} // namespace

TEST(PhysicsWorld, StaticBodyStaysPut)
{
    PhysicsWorld world;
    PhysicsBodyId floor = AddFloor(world);
    ASSERT_TRUE(floor.IsValid());

    const BodyTransform before = world.GetBodyTransform(floor);
    for (int i = 0; i < 60; ++i)
        world.Step(kFixedDt);
    const BodyTransform after = world.GetBodyTransform(floor);

    EXPECT_FLOAT_EQ(before.Position.Y, after.Position.Y);
}

TEST(PhysicsWorld, DynamicBodyFallsUnderGravity)
{
    PhysicsWorld world;
    AddFloor(world);
    PhysicsBodyId sphere = AddFallingSphere(world, 5.0f);
    ASSERT_TRUE(sphere.IsValid());

    const float startY = world.GetBodyTransform(sphere).Position.Y;
    for (int i = 0; i < 10; ++i)
        world.Step(kFixedDt);

    EXPECT_LT(world.GetBodyTransform(sphere).Position.Y, startY);
}

TEST(PhysicsWorld, DynamicBodyRestsOnFloor)
{
    PhysicsWorld world;
    AddFloor(world);
    PhysicsBodyId sphere = AddFallingSphere(world, 5.0f);

    for (int i = 0; i < 240; ++i) // 4 seconds: drop and settle
        world.Step(kFixedDt);

    const BodyTransform rest = world.GetBodyTransform(sphere);
    // Floor top (0.5) + sphere radius (0.5) = 1.0.
    EXPECT_NEAR(rest.Position.Y, 1.0f, 0.05f);
    EXPECT_LT(std::abs(world.GetLinearVelocity(sphere).Y), 0.05f);
}

TEST(PhysicsWorld, DeterministicAcrossRuns)
{
    auto run = []
    {
        PhysicsWorld world;
        AddFloor(world);
        PhysicsBodyId sphere = AddFallingSphere(world, 5.0f);
        // A small lateral nudge so the trajectory exercises more than one axis.
        world.SetLinearVelocity(sphere, Vec3d(0.7f, 0.0f, -0.3f));
        for (int i = 0; i < 150; ++i)
            world.Step(kFixedDt);
        return world.GetBodyTransform(sphere).Position;
    };

    const Vec3d a = run();
    const Vec3d b = run();

    // Same build, same inputs, single-threaded: bit-identical, not just close.
    EXPECT_EQ(a.X, b.X);
    EXPECT_EQ(a.Y, b.Y);
    EXPECT_EQ(a.Z, b.Z);
}

TEST(PhysicsWorld, BodyCountTracksAddRemove)
{
    PhysicsWorld world;
    EXPECT_EQ(world.BodyCount(), 0u);

    PhysicsBodyId floor = AddFloor(world);
    PhysicsBodyId sphere = AddFallingSphere(world, 3.0f);
    EXPECT_EQ(world.BodyCount(), 2u);

    world.RemoveBody(sphere);
    EXPECT_EQ(world.BodyCount(), 1u);

    world.RemoveBody(floor);
    EXPECT_EQ(world.BodyCount(), 0u);
}

TEST(PhysicsWorld, CarriesUserDataForQueryMapping)
{
    PhysicsWorld world;
    BodyDesc desc;
    desc.Shape = CollisionShape::MakeBox(Vec3d(1.0f, 1.0f, 1.0f));
    desc.Motion = BodyMotion::Static;
    desc.Layer = CollisionLayer::Static;
    desc.UserData = 0xABCDEF01u;
    PhysicsBodyId id = world.AddBody(desc);

    EXPECT_EQ(world.GetUserData(id), 0xABCDEF01u);
}
