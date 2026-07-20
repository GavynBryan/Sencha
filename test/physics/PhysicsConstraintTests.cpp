// Driven pose constraints at the PhysicsWorld facade: one-way locked driving
// toward a per-step target frame with collision preserved, generational
// handles that never resolve stale, and body-removal safety in either cleanup
// order. Headless, no Jolt headers, explicit fixed stepping.

#include <gtest/gtest.h>

#include <physics/PhysicsWorld.h>

#include <cmath>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;

PhysicsBodyId AddFloor(PhysicsWorld& world)
{
    BodyDesc floor;
    floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
    floor.Position = Vec3d(0.0f, 0.0f, 0.0f); // top surface at y = 0.5
    floor.Motion = BodyMotion::Static;
    floor.Layer = CollisionLayer::Static;
    return world.AddBody(floor);
}

PhysicsBodyId AddDynamicBox(PhysicsWorld& world, const Vec3d& position, float gravityScale = 1.0f)
{
    BodyDesc box;
    box.Shape = CollisionShape::MakeBox(Vec3d(0.25f, 0.25f, 0.25f));
    box.Position = position;
    box.Motion = BodyMotion::Dynamic;
    box.Layer = CollisionLayer::Moving;
    box.Mass = 1.0f;
    box.GravityScale = gravityScale;
    return world.AddBody(box);
}

DrivenPoseTarget FrameAt(const Vec3d& position, const Quatf& rotation = Quatf::Identity(),
                         const Vec3d& linearVelocity = Vec3d::Zero(),
                         const Vec3d& angularVelocity = Vec3d::Zero())
{
    DrivenPoseTarget target;
    target.WorldFrame.Position = position;
    target.WorldFrame.Rotation = rotation;
    target.LinearVelocity = linearVelocity;
    target.AngularVelocity = angularVelocity;
    return target;
}

float Distance(const Vec3d& a, const Vec3d& b)
{
    return (a - b).Magnitude();
}
} // namespace

// ─── Following ───────────────────────────────────────────────────────────────

TEST(PhysicsConstraint, LockedPositionFollowsTranslatingFrame)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);
    ASSERT_TRUE(constraint.IsValid());

    // The frame travels +X at 2 m/s; the follower must track it.
    Vec3d framePos(0.0f, 5.0f, 0.0f);
    const Vec3d frameVel(2.0f, 0.0f, 0.0f);
    for (int i = 0; i < 120; ++i)
    {
        framePos = framePos + frameVel * kFixedDt;
        world.SetDrivenPoseTarget(constraint, FrameAt(framePos, Quatf::Identity(), frameVel));
        world.Step(kFixedDt);
    }

    EXPECT_LT(Distance(world.GetBodyTransform(follower).Position, framePos), 0.05f);
    EXPECT_LT(world.GetConstraintTelemetry(constraint).PositionError, 0.05f);
}

TEST(PhysicsConstraint, LockedOrientationFollowsRotatingFrame)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    // The frame spins about Y at 1 rad/s.
    const Vec3d spin(0.0f, 1.0f, 0.0f);
    float angle = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        angle += 1.0f * kFixedDt;
        const Quatf frameRot = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), angle);
        world.SetDrivenPoseTarget(
            constraint, FrameAt(Vec3d(0.0f, 5.0f, 0.0f), frameRot, Vec3d::Zero(), spin));
        world.Step(kFixedDt);
    }

    EXPECT_LT(world.GetConstraintTelemetry(constraint).AngularError, 0.05f);
    EXPECT_NEAR(world.GetAngularVelocity(follower).Y, 1.0f, 0.1f);
}

TEST(PhysicsConstraint, OffCenterFollowerTracesTheFrameRotation)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(-1.0f, 5.0f, 0.0f), 0.0f);

    // The attachment frame sits 1 m from the body center along +X, so as the
    // target frame spins about Y, the body center must orbit at radius 1.
    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    desc.FollowerLocalFrame.Position = Vec3d(1.0f, 0.0f, 0.0f);
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    const Vec3d pivot(0.0f, 5.0f, 0.0f);
    float angle = 0.0f;
    for (int i = 0; i < 240; ++i)
    {
        angle += 0.8f * kFixedDt;
        const Quatf frameRot = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), angle);
        world.SetDrivenPoseTarget(
            constraint, FrameAt(pivot, frameRot, Vec3d::Zero(), Vec3d(0.0f, 0.8f, 0.0f)));
        world.Step(kFixedDt);
    }

    // Body center stays one attachment-arm from the pivot while orbiting.
    EXPECT_NEAR(Distance(world.GetBodyTransform(follower).Position, pivot), 1.0f, 0.05f);
}

TEST(PhysicsConstraint, SuspendedFollowerHoldsPoseWithoutGravity)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    for (int i = 0; i < 120; ++i)
    {
        world.SetDrivenPoseTarget(constraint, FrameAt(Vec3d(0.0f, 5.0f, 0.0f)));
        world.Step(kFixedDt);
    }

    EXPECT_LT(Distance(world.GetBodyTransform(follower).Position, Vec3d(0.0f, 5.0f, 0.0f)), 0.01f);
}

TEST(PhysicsConstraint, FollowerStillCollidesWithWorldGeometry)
{
    PhysicsWorld world;
    AddFloor(world);
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 3.0f, 0.0f));

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    // Drive toward a frame below the floor; the solver must keep the box on
    // top (floor top 0.5 + half extent 0.25) and telemetry must report the
    // unclosed error instead of tunneling.
    for (int i = 0; i < 180; ++i)
    {
        world.SetDrivenPoseTarget(constraint, FrameAt(Vec3d(0.0f, -2.0f, 0.0f)));
        world.Step(kFixedDt);
    }

    EXPECT_GT(world.GetBodyTransform(follower).Position.Y, 0.6f);
    EXPECT_GT(world.GetConstraintTelemetry(constraint).PositionError, 2.0f);
}

TEST(PhysicsConstraint, TeleportSnapsWithoutSyntheticVelocity)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    world.SetDrivenPoseTarget(constraint, FrameAt(Vec3d(0.0f, 5.0f, 0.0f)));
    world.Step(kFixedDt);

    DrivenPoseTarget far = FrameAt(Vec3d(100.0f, 5.0f, 0.0f));
    far.Teleported = true;
    world.SetDrivenPoseTarget(constraint, far);
    world.Step(kFixedDt);

    // Snapped to the frame, with the frame's (zero) velocity — not a
    // 6000 m/s chase.
    EXPECT_LT(Distance(world.GetBodyTransform(follower).Position, Vec3d(100.0f, 5.0f, 0.0f)), 0.01f);
    EXPECT_LT(world.GetLinearVelocity(follower).Magnitude(), 0.01f);
}

// ─── Handles and lifecycle ───────────────────────────────────────────────────

TEST(PhysicsConstraint, RemoveInvalidatesAndDeadHandleRemovalIsNoOp)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f));

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);
    EXPECT_TRUE(world.IsConstraintValid(constraint));
    EXPECT_EQ(world.ConstraintCount(), 1u);

    world.RemoveConstraint(constraint);
    EXPECT_FALSE(world.IsConstraintValid(constraint));
    EXPECT_EQ(world.ConstraintCount(), 0u);

    world.RemoveConstraint(constraint); // dead handle: no-op, no double free
    EXPECT_EQ(world.ConstraintCount(), 0u);
}

TEST(PhysicsConstraint, StaleGenerationNeverResolvesAfterSlotReuse)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f));

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId first = world.AddDrivenPoseConstraint(desc);
    world.RemoveConstraint(first);

    const PhysicsConstraintId second = world.AddDrivenPoseConstraint(desc);
    ASSERT_EQ(second.Index, first.Index); // the slot was reused...
    EXPECT_NE(second.Generation, first.Generation);

    EXPECT_FALSE(world.IsConstraintValid(first)); // ...and the old handle is dead
    EXPECT_TRUE(world.IsConstraintValid(second));

    // A stale-handle target write must not leak onto the new constraint.
    world.SetDrivenPoseTarget(first, FrameAt(Vec3d(9.0f, 9.0f, 9.0f)));
    world.SetDrivenPoseTarget(second, FrameAt(Vec3d(0.0f, 5.0f, 0.0f)));
    world.Step(kFixedDt);
    EXPECT_LT(Distance(world.GetBodyTransform(follower).Position, Vec3d(0.0f, 5.0f, 0.0f)), 0.5f);
}

TEST(PhysicsConstraint, RemoveBodyInvalidatesDependentConstraints)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f));

    DrivenPoseConstraintDesc desc;
    desc.Follower = follower;
    const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

    world.RemoveBody(follower);
    EXPECT_FALSE(world.IsConstraintValid(constraint));
    EXPECT_EQ(world.ConstraintCount(), 0u);

    world.RemoveConstraint(constraint); // the other cleanup order: no-op
    world.Step(kFixedDt);               // and stepping is unaffected
}

TEST(PhysicsConstraint, CreateDestroyChurnLeaksNothing)
{
    PhysicsWorld world;
    const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f), 0.0f);

    for (int i = 0; i < 100; ++i)
    {
        DrivenPoseConstraintDesc desc;
        desc.Follower = follower;
        const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);
        ASSERT_TRUE(constraint.IsValid());
        world.SetDrivenPoseTarget(constraint, FrameAt(Vec3d(0.0f, 5.0f, 0.0f)));
        world.Step(kFixedDt);
        world.RemoveConstraint(constraint);
    }

    EXPECT_EQ(world.ConstraintCount(), 0u);
    EXPECT_EQ(world.StaleRefreshCount(), 0u);
}

#ifndef NDEBUG
TEST(PhysicsConstraint, UnrefreshedConstraintAssertsInDebug)
{
    // The refresh contract: targets are per-step input. With residency
    // transitions explicit, an unrefreshed live constraint can only be an
    // orchestration bug.
    EXPECT_DEATH(
        {
            PhysicsWorld world;
            const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.0f, 5.0f, 0.0f));
            DrivenPoseConstraintDesc desc;
            desc.Follower = follower;
            (void)world.AddDrivenPoseConstraint(desc);
            world.Step(kFixedDt); // no SetDrivenPoseTarget since creation
        },
        "not refreshed");
}
#endif

// ─── Determinism ─────────────────────────────────────────────────────────────

TEST(PhysicsConstraint, DeterministicAcrossIdenticalRuns)
{
    auto run = [](Vec3d& out)
    {
        PhysicsWorld world;
        AddFloor(world);
        const PhysicsBodyId follower = AddDynamicBox(world, Vec3d(0.1f, 3.0f, -0.2f));

        DrivenPoseConstraintDesc desc;
        desc.Follower = follower;
        const PhysicsConstraintId constraint = world.AddDrivenPoseConstraint(desc);

        Vec3d framePos(0.1f, 2.0f, -0.2f);
        for (int i = 0; i < 120; ++i)
        {
            framePos = framePos + Vec3d(1.0f, 0.0f, 0.0f) * kFixedDt;
            world.SetDrivenPoseTarget(
                constraint, FrameAt(framePos, Quatf::Identity(), Vec3d(1.0f, 0.0f, 0.0f)));
            world.Step(kFixedDt);
        }
        out = world.GetBodyTransform(follower).Position;
    };

    Vec3d a;
    Vec3d b;
    run(a);
    run(b);
    EXPECT_EQ(a.X, b.X);
    EXPECT_EQ(a.Y, b.Y);
    EXPECT_EQ(a.Z, b.Z);
}
