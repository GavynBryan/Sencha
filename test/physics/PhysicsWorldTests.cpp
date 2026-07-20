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

// ─── Full body state (angular velocity, gravity scale, damping, wake) ────────

TEST(PhysicsWorld, AngularVelocityRoundTrips)
{
    PhysicsWorld world;
    BodyDesc desc;
    desc.Shape = CollisionShape::MakeSphere(0.5f);
    desc.Position = Vec3d(0.0f, 5.0f, 0.0f);
    desc.Motion = BodyMotion::Dynamic;
    desc.Layer = CollisionLayer::Moving;
    desc.GravityScale = 0.0f; // free space: nothing but the spin
    const PhysicsBodyId body = world.AddBody(desc);
    ASSERT_TRUE(body.IsValid());

    world.SetAngularVelocity(body, Vec3d(0.0f, 3.0f, 0.0f));
    const Quatf before = world.GetBodyTransform(body).Rotation;
    world.Step(kFixedDt);

    // One step of default damping trims a fraction of a percent.
    EXPECT_NEAR(world.GetAngularVelocity(body).Y, 3.0f, 0.05f);
    const Quatf after = world.GetBodyTransform(body).Rotation;
    EXPECT_FALSE(before.X == after.X && before.Y == after.Y
                 && before.Z == after.Z && before.W == after.W);
}

TEST(PhysicsWorld, GravityScaleZeroSuspendsBody)
{
    PhysicsWorld world;

    BodyDesc desc;
    desc.Shape = CollisionShape::MakeSphere(0.5f);
    desc.Position = Vec3d(0.0f, 5.0f, 0.0f);
    desc.Motion = BodyMotion::Dynamic;
    desc.Layer = CollisionLayer::Moving;
    desc.GravityScale = 0.0f;
    const PhysicsBodyId suspended = world.AddBody(desc);

    desc.Position = Vec3d(3.0f, 5.0f, 0.0f);
    desc.GravityScale = 1.0f;
    const PhysicsBodyId falling = world.AddBody(desc);

    for (int i = 0; i < 60; ++i)
        world.Step(kFixedDt);

    EXPECT_NEAR(world.GetBodyTransform(suspended).Position.Y, 5.0f, 1e-3f);
    EXPECT_LT(world.GetBodyTransform(falling).Position.Y, 4.0f);
}

TEST(PhysicsWorld, GravityScaleChangesActOnTheNextStep)
{
    PhysicsWorld world;
    const PhysicsBodyId sphere = AddFallingSphere(world, 50.0f);

    for (int i = 0; i < 30; ++i)
        world.Step(kFixedDt);
    const float fallingSpeed = world.GetLinearVelocity(sphere).Y;
    EXPECT_LT(fallingSpeed, -1.0f); // it was genuinely falling

    world.SetGravityScale(sphere, 0.0f);
    world.Step(kFixedDt);

    // No gravity this step: vertical speed only loses its damping fraction.
    EXPECT_GT(world.GetLinearVelocity(sphere).Y, fallingSpeed * 1.001f);
}

TEST(PhysicsWorld, HighAngularDampingBrakesSpinFaster)
{
    PhysicsWorld world;

    BodyDesc desc;
    desc.Shape = CollisionShape::MakeSphere(0.5f);
    desc.Position = Vec3d(0.0f, 5.0f, 0.0f);
    desc.Motion = BodyMotion::Dynamic;
    desc.Layer = CollisionLayer::Moving;
    desc.GravityScale = 0.0f;
    desc.AngularDamping = 0.05f;
    const PhysicsBodyId lazy = world.AddBody(desc);

    desc.Position = Vec3d(3.0f, 5.0f, 0.0f);
    desc.AngularDamping = 5.0f;
    const PhysicsBodyId braked = world.AddBody(desc);

    world.SetAngularVelocity(lazy, Vec3d(0.0f, 10.0f, 0.0f));
    world.SetAngularVelocity(braked, Vec3d(0.0f, 10.0f, 0.0f));
    for (int i = 0; i < 60; ++i)
        world.Step(kFixedDt);

    EXPECT_GT(world.GetAngularVelocity(lazy).Y, 2.0f * world.GetAngularVelocity(braked).Y);
}

TEST(PhysicsWorld, WakeBodyMakesGravityChangesActOnSleptBodies)
{
    PhysicsWorld world;
    AddFloor(world);
    const PhysicsBodyId sphere = AddFallingSphere(world, 2.0f);

    for (int i = 0; i < 300; ++i) // settle and fall asleep
        world.Step(kFixedDt);
    const float restY = world.GetBodyTransform(sphere).Position.Y;

    // A gravity-scale write alone does not wake a sleeping body: even inverted
    // gravity leaves it resting. (This stillness is also the proof it slept.)
    world.SetGravityScale(sphere, -1.0f);
    for (int i = 0; i < 30; ++i)
        world.Step(kFixedDt);
    EXPECT_NEAR(world.GetBodyTransform(sphere).Position.Y, restY, 1e-3f);

    // An explicit wake makes the pending gravity change act.
    world.WakeBody(sphere);
    for (int i = 0; i < 30; ++i)
        world.Step(kFixedDt);
    EXPECT_GT(world.GetBodyTransform(sphere).Position.Y, restY + 0.1f);
}
