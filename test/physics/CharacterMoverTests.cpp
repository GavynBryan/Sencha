// CharacterMover (Jolt CharacterVirtual behind a PIMPL): move-and-slide against
// primitive static geometry. No Jolt headers here; the firewall holds.

#include <gtest/gtest.h>

#include <physics/CharacterMover.h>
#include <physics/PhysicsWorld.h>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;
const Vec3d kGravity(0.0f, -9.81f, 0.0f);

void AddStaticBox(PhysicsWorld& world, const Vec3d& center, const Vec3d& halfExtents)
{
    BodyDesc box;
    box.Shape = CollisionShape::MakeBox(halfExtents);
    box.Position = center;
    box.Motion = BodyMotion::Static;
    box.Layer = CollisionLayer::Static;
    (void)world.AddBody(box);
}

CharacterMoverConfig MakeConfig()
{
    CharacterMoverConfig config;
    config.Radius = 0.3f;
    config.Height = 1.8f; // half height 0.9
    return config;
}
} // namespace

TEST(CharacterMover, FallsAndLandsOnFloor)
{
    PhysicsWorld world;
    AddStaticBox(world, Vec3d(0.0f, 0.0f, 0.0f), Vec3d(50.0f, 0.5f, 50.0f)); // top at y = 0.5

    CharacterMover mover(world, MakeConfig(), Vec3d(0.0f, 5.0f, 0.0f));
    for (int i = 0; i < 240; ++i)
        mover.Move(Vec3d::Zero(), kFixedDt, kGravity);

    EXPECT_TRUE(mover.IsGrounded());
    // Floor top 0.5 + capsule half height 0.9 = 1.4.
    EXPECT_NEAR(mover.GetPosition().Y, 1.4f, 0.05f);
}

TEST(CharacterMover, WalksIntoWallAndStops)
{
    PhysicsWorld world;
    AddStaticBox(world, Vec3d(0.0f, 0.0f, 0.0f), Vec3d(50.0f, 0.5f, 50.0f));
    AddStaticBox(world, Vec3d(2.0f, 1.5f, 0.0f), Vec3d(0.5f, 2.0f, 5.0f)); // wall face at x = 1.5

    CharacterMover mover(world, MakeConfig(), Vec3d(0.0f, 1.4f, 0.0f));
    for (int i = 0; i < 180; ++i)
        mover.Move(Vec3d(5.0f, 0.0f, 0.0f), kFixedDt, kGravity);

    const float x = mover.GetPosition().X;
    EXPECT_GT(x, 0.5f);  // it advanced
    EXPECT_LT(x, 1.5f);  // but did not pass through the wall (face at 1.5, minus radius)
}

TEST(CharacterMover, JumpLeavesGround)
{
    PhysicsWorld world;
    AddStaticBox(world, Vec3d(0.0f, 0.0f, 0.0f), Vec3d(50.0f, 0.5f, 50.0f));

    CharacterMover mover(world, MakeConfig(), Vec3d(0.0f, 1.4f, 0.0f));
    for (int i = 0; i < 30; ++i) // settle on the floor
        mover.Move(Vec3d::Zero(), kFixedDt, kGravity);
    ASSERT_TRUE(mover.IsGrounded());

    const float restY = mover.GetPosition().Y;
    mover.Jump(5.0f);
    mover.Move(Vec3d::Zero(), kFixedDt, kGravity);
    EXPECT_GT(mover.GetPosition().Y, restY); // rose off the floor

    for (int i = 0; i < 240; ++i) // come back down and settle
        mover.Move(Vec3d::Zero(), kFixedDt, kGravity);
    EXPECT_TRUE(mover.IsGrounded());
    EXPECT_NEAR(mover.GetPosition().Y, restY, 0.05f);
}
