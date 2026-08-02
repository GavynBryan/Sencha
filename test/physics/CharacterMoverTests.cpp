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

// Stands in for the locomotion system: integrate gravity, hold the achieved
// vertical velocity between ticks, and cancel it while standing. The motor
// itself carries no velocity and integrates nothing.
CharacterMoveResult Advance(CharacterMover& mover,
                            Vec3d& velocity,
                            const Vec3d& planar,
                            CharacterSupportKind support)
{
    float up = static_cast<float>(velocity.Y);
    if (support == CharacterSupportKind::Stable && up < 0.0f)
        up = 0.0f;
    up += static_cast<float>(kGravity.Y) * kFixedDt;

    CharacterMoveRequest request;
    request.Velocity = Vec3d(planar.X, up, planar.Z);
    request.Gravity = kGravity;
    request.DeltaSeconds = kFixedDt;

    const CharacterMoveResult result = mover.Move(request);
    velocity = result.Velocity;
    return result;
}
} // namespace

TEST(CharacterMover, FallsAndLandsOnFloor)
{
    PhysicsWorld world;
    AddStaticBox(world, Vec3d(0.0f, 0.0f, 0.0f), Vec3d(50.0f, 0.5f, 50.0f)); // top at y = 0.5

    CharacterMover mover(world, MakeConfig(), Vec3d(0.0f, 5.0f, 0.0f));
    Vec3d velocity = Vec3d::Zero();
    CharacterMoveResult result;
    for (int i = 0; i < 240; ++i)
        result = Advance(mover, velocity, Vec3d::Zero(), result.Support.Kind);

    EXPECT_EQ(result.Support.Kind, CharacterSupportKind::Stable);
    // Floor top 0.5 + capsule half height 0.9 = 1.4.
    EXPECT_NEAR(result.Position.Y, 1.4f, 0.05f);
    EXPECT_GT(result.Support.Normal.Y, 0.9f);
}

TEST(CharacterMover, WalksIntoWallAndStops)
{
    PhysicsWorld world;
    AddStaticBox(world, Vec3d(0.0f, 0.0f, 0.0f), Vec3d(50.0f, 0.5f, 50.0f));
    AddStaticBox(world, Vec3d(2.0f, 1.5f, 0.0f), Vec3d(0.5f, 2.0f, 5.0f)); // wall face at x = 1.5

    CharacterMover mover(world, MakeConfig(), Vec3d(0.0f, 1.4f, 0.0f));
    Vec3d velocity = Vec3d::Zero();
    CharacterSupportKind support = CharacterSupportKind::None;
    for (int i = 0; i < 180; ++i)
        support = Advance(mover, velocity, Vec3d(5.0f, 0.0f, 0.0f), support).Support.Kind;

    const float x = mover.GetPosition().X;
    EXPECT_GT(x, 0.5f);  // it advanced
    EXPECT_LT(x, 1.5f);  // but did not pass through the wall (face at 1.5, minus radius)
}

TEST(CharacterMover, UpwardVelocityLeavesGroundAndFallsBack)
{
    PhysicsWorld world;
    AddStaticBox(world, Vec3d(0.0f, 0.0f, 0.0f), Vec3d(50.0f, 0.5f, 50.0f));

    CharacterMover mover(world, MakeConfig(), Vec3d(0.0f, 1.4f, 0.0f));
    Vec3d velocity = Vec3d::Zero();
    CharacterMoveResult result;
    for (int i = 0; i < 30; ++i) // settle on the floor
        result = Advance(mover, velocity, Vec3d::Zero(), result.Support.Kind);
    ASSERT_EQ(result.Support.Kind, CharacterSupportKind::Stable);

    const float restY = result.Position.Y;

    // A jump is an ordinary up-axis velocity in the request, with snapping
    // disabled for the tick that leaves the ground.
    CharacterMoveRequest launch;
    launch.Velocity = Vec3d(0.0f, 5.0f, 0.0f);
    launch.Gravity = kGravity;
    launch.DeltaSeconds = kFixedDt;
    launch.AllowGroundSnap = false;
    result = mover.Move(launch);
    velocity = result.Velocity;
    EXPECT_GT(result.Position.Y, restY);

    for (int i = 0; i < 240; ++i) // come back down and settle
        result = Advance(mover, velocity, Vec3d::Zero(), result.Support.Kind);
    EXPECT_EQ(result.Support.Kind, CharacterSupportKind::Stable);
    EXPECT_NEAR(result.Position.Y, restY, 0.05f);
}
