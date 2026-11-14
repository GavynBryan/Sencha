#include <gtest/gtest.h>

#include <camera/CameraBob.h>
#include <camera/CameraBobState.h>
#include <camera/CameraSubject.h>
#include <camera/CameraTilt.h>
#include <camera/CameraTiltState.h>
#include <camera/CameraViewOffset.h>
#include <components/CameraComponent.h>
#include <components/StrideCycle.h>
#include <ecs/World.h>
#include <math/Angle.h>
#include <movement/StrideCycleSystem.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kTick = 1.0 / 60.0;
constexpr float kTickF = static_cast<float>(kTick);

CameraEffectClock At(std::uint64_t tick, double alpha = 0.0)
{
    return { .Tick = tick, .Alpha = alpha, .TickSeconds = kTick };
}

// Distance from `position` to the footfall at 0, the short way round.
float FromStart(float position)
{
    return std::min(position, 1.0f - position);
}

void Walk(StrideCycle& stride, Vec3d velocity, int ticks, float length = 2.0f, bool supported = true)
{
    for (int i = 0; i < ticks; ++i)
        AdvanceStrideCycle(stride, velocity, Vec3d::Zero(), Vec3d::Up(), supported, length, kTickF);
}

// A pawn and the camera presenting its view, as a first-person rig has them.
struct Views
{
    Views()
    {
        Entities.RegisterComponent<LocalTransform>();
        Entities.RegisterComponent<WorldTransform>();
        Entities.RegisterComponent<Parent>();
        Entities.RegisterComponent<CameraComponent>();
        Entities.RegisterComponent<CameraSubject>();
        Entities.RegisterComponent<CameraBob>();
        Entities.RegisterComponent<CameraTilt>();
        Entities.RegisterComponent<StrideCycle>();

        Pawn = Entities.CreateEntity();
        Entities.AddComponent(Pawn, LocalTransform{});
        Entities.AddComponent(Pawn, WorldTransform{});
        Entities.AddComponent(Pawn, StrideCycle{});

        Camera = Entities.CreateEntity();
        Entities.AddComponent(Camera, LocalTransform{});
        Entities.AddComponent(Camera, WorldTransform{});
        Entities.AddComponent(Camera, Parent{ Pawn });
        Entities.AddComponent(Camera, CameraComponent{});
        Entities.AddComponent(Camera, CameraBob{});
        Entities.AddComponent(Camera, CameraTilt{});
        SetCameraSubject(Entities, Camera, Pawn);
    }

    StrideCycle& Stride() { return *Entities.TryGet<StrideCycle>(Pawn); }

    void Striding(float position, float speed)
    {
        Stride() = { .Position = position, .PreviousPosition = position, .GroundSpeed = speed,
                     .PreviousGroundSpeed = speed };
    }

    CameraViewOffset BobAt(const CameraEffectClock& now)
    {
        Bob.Update(Entities, Camera, now);
        return Bob.OffsetFor(Camera);
    }

    CameraViewOffset TiltAt(const CameraEffectClock& now)
    {
        Tilt.Update(Entities, Camera, now);
        return Tilt.OffsetFor(Camera);
    }

    void MovePawn(Vec3d by) { Entities.TryGet<LocalTransform>(Pawn)->Value.Position += by; }

    World Entities;
    EntityId Pawn;
    EntityId Camera;
    CameraBobState Bob;
    CameraTiltState Tilt;
};

// Where the view's up points once `offset` moves an unturned camera.
Vec3d ViewUp(const CameraViewOffset& offset)
{
    return ApplyCameraViewOffset(Transform3f::Identity(), offset).Rotation.RotateVector(Vec3d::Up());
}
} // namespace

TEST(StrideCycle, CountsTheGroundCoveredInStrides)
{
    StrideCycle stride;
    Walk(stride, Vec3d(4.0f, 0.0f, 0.0f), 15);
    EXPECT_NEAR(stride.Position, 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(stride.GroundSpeed, 4.0f);
    Walk(stride, Vec3d(0.0f, 0.0f, -4.0f), 15);
    EXPECT_NEAR(FromStart(stride.Position), 0.0f, 1e-4f);

    // Half the speed, half the cadence: the cycle is distance, not time.
    StrideCycle slow;
    Walk(slow, Vec3d(2.0f, 0.0f, 0.0f), 15);
    EXPECT_NEAR(slow.Position, 0.25f, 1e-4f);
}

TEST(StrideCycle, OffTheGroundOrWithNoLengthItHoldsItsPlace)
{
    StrideCycle stride;
    Walk(stride, Vec3d(4.0f, 0.0f, 0.0f), 6);
    const float landed = stride.Position;

    Walk(stride, Vec3d(4.0f, 3.0f, 0.0f), 30, 2.0f, false);
    EXPECT_FLOAT_EQ(stride.Position, landed);
    EXPECT_FLOAT_EQ(stride.GroundSpeed, 0.0f);

    Walk(stride, Vec3d(4.0f, 0.0f, 0.0f), 30, 0.0f);
    EXPECT_FLOAT_EQ(stride.Position, landed);
    EXPECT_FLOAT_EQ(stride.GroundSpeed, 0.0f);
}

TEST(StrideCycle, RidingTheGroundOrMovingAlongUpIsNotWalking)
{
    StrideCycle riding;
    for (int i = 0; i < 30; ++i)
        AdvanceStrideCycle(riding, Vec3d(3.0f, 0.0f, 1.0f), Vec3d(3.0f, 0.0f, 1.0f), Vec3d::Up(), true, 2.0f, kTickF);
    EXPECT_FLOAT_EQ(riding.Position, 0.0f);

    StrideCycle wall;
    const Vec3d up(0.0f, 0.0f, 1.0f);
    for (int i = 0; i < 15; ++i)
        AdvanceStrideCycle(wall, Vec3d(0.0f, 4.0f, 9.0f), Vec3d::Zero(), up, true, 2.0f, kTickF);
    EXPECT_NEAR(wall.Position, 0.5f, 1e-4f);
}

TEST(StrideCycle, NoTickAdvancesHalfAStrideSoItNeverReadsAsBackwards)
{
    StrideCycle stride;
    Walk(stride, Vec3d(1000.0f, 0.0f, 0.0f), 1);
    EXPECT_FLOAT_EQ(stride.Position, kStrideCycleMaxAdvance);
    Walk(stride, Vec3d(1000.0f, 0.0f, 0.0f), 2);
    EXPECT_NEAR(stride.Position, 3.0f * kStrideCycleMaxAdvance - 1.0f, 1e-5f);
    // Halfway through the wrapping tick is half a capped advance forward, past the wrap.
    EXPECT_NEAR(StrideCyclePositionAt(stride, 0.5f), 2.5f * kStrideCycleMaxAdvance - 1.0f, 1e-5f);
}

TEST(StrideCycle, BetweenTicksItRunsForwardTheShortWayRound)
{
    const StrideCycle stride{ .Position = 0.1f, .PreviousPosition = 0.9f, .GroundSpeed = 6.0f,
                              .PreviousGroundSpeed = 2.0f };
    EXPECT_NEAR(StrideCyclePositionAt(stride, 0.25f), 0.95f, 1e-5f);
    EXPECT_NEAR(FromStart(StrideCyclePositionAt(stride, 0.5f)), 0.0f, 1e-5f);
    EXPECT_NEAR(StrideCyclePositionAt(stride, 1.0f), 0.1f, 1e-5f);
    EXPECT_FLOAT_EQ(StrideGroundSpeedAt(stride, 0.5f), 4.0f);
}

TEST(StrideCycleSystem, CountsEveryCharacterFromWhatTheTickAchieved)
{
    World world;
    world.RegisterComponent<StrideCycle>();
    world.RegisterComponent<KinematicState>();
    world.RegisterComponent<SupportState>();
    world.RegisterComponent<MotionRequest>();
    world.RegisterComponent<ResolvedMovementTuning>();
    const auto character = [&](float speed, SupportKind support) {
        const EntityId entity = world.CreateEntity();
        world.AddComponent(entity, StrideCycle{});
        world.AddComponent(entity, KinematicState{ .Velocity = Vec3d(speed, 0.0f, 0.0f) });
        SupportState ground;
        ground.Kind = support;
        world.AddComponent(entity, ground);
        world.AddComponent(entity, MotionRequest{});
        ResolvedMovementTuning tuning;
        tuning.StrideLength = 2.0f;
        world.AddComponent(entity, tuning);
        return entity;
    };
    const EntityId runner = character(6.0f, SupportKind::Stable);
    const EntityId walker = character(1.5f, SupportKind::Stable);
    const EntityId sliding = character(6.0f, SupportKind::Steep);

    StrideCycleSystem system;
    for (int tick = 0; tick < 10; ++tick)
        system.Step(world, kTickF);
    EXPECT_NEAR(world.TryGet<StrideCycle>(runner)->Position, 0.5f, 1e-4f);
    EXPECT_NEAR(world.TryGet<StrideCycle>(walker)->Position, 0.125f, 1e-4f);
    EXPECT_FLOAT_EQ(world.TryGet<StrideCycle>(sliding)->Position, 0.0f);
}

TEST(CameraBob, DipsAtEachFootfallAndFloatsBetweenWithoutTurningTheView)
{
    const CameraBob bob;
    const CameraViewOffset footfall = EvaluateCameraBob(bob, 0.0f, 1.0f);
    const CameraViewOffset between = EvaluateCameraBob(bob, 0.25f, 1.0f);
    const CameraViewOffset otherFoot = EvaluateCameraBob(bob, 0.5f, 1.0f);
    const CameraViewOffset otherSide = EvaluateCameraBob(bob, 0.75f, 1.0f);

    EXPECT_NEAR(footfall.Translation.Y, -bob.Drop, 1e-6f);
    EXPECT_NEAR(otherFoot.Translation.Y, -bob.Drop, 1e-6f);
    EXPECT_NEAR(between.Translation.Y, 0.0f, 1e-6f);
    EXPECT_NEAR(between.Translation.X, bob.Sway, 1e-6f);
    EXPECT_NEAR(otherSide.Translation.X, -bob.Sway, 1e-6f);
    for (float cycle = 0.0f; cycle < 1.0f; cycle += 0.05f)
    {
        const CameraViewOffset at = EvaluateCameraBob(bob, cycle, 1.0f);
        EXPECT_EQ(at.Rotation, Vec3d::Zero());
        EXPECT_LE(at.Translation.Y, 0.0f);
        EXPECT_GE(at.Translation.Y, -bob.Drop - 1e-6f);
    }
}

TEST(CameraBob, StrengthScalesItAndNoneIsNoBob)
{
    const CameraBob bob;
    EXPECT_NEAR(EvaluateCameraBob(bob, 0.25f, 0.5f).Translation.X, bob.Sway * 0.5f, 1e-6f);
    EXPECT_TRUE(EvaluateCameraBob(bob, 0.1f, 0.0f).IsIdentity());
}

TEST(CameraBob, FollowsItsSubjectsStrideAtItsSpeed)
{
    Views views;
    views.Striding(0.25f, 6.0f);
    EXPECT_NEAR(views.BobAt(At(1)).Translation.X, CameraBob{}.Sway, 1e-6f);

    // Half of full speed, half the bob.
    Views slower;
    slower.Striding(0.25f, 3.0f);
    EXPECT_NEAR(slower.BobAt(At(1)).Translation.X, CameraBob{}.Sway * 0.5f, 1e-6f);

    // Between ticks the cycle is drawn where the blend puts it.
    views.Stride() = { .Position = 0.5f, .PreviousPosition = 0.0f, .GroundSpeed = 6.0f, .PreviousGroundSpeed = 6.0f };
    EXPECT_NEAR(views.BobAt(At(2, 0.5)).Translation.X, CameraBob{}.Sway, 1e-5f);
}

TEST(CameraBob, EasesInTheSameAtAnyFrameRate)
{
    Views slow;
    Views fast;
    slow.Striding(0.25f, 0.0f);
    fast.Striding(0.25f, 0.0f);
    (void)slow.BobAt(At(10));
    (void)fast.BobAt(At(10));
    slow.Striding(0.25f, 6.0f);
    fast.Striding(0.25f, 6.0f);
    for (std::uint64_t tick = 11; tick <= 16; ++tick)
    {
        (void)slow.BobAt(At(tick));
        for (int quarter = 0; quarter < 4; ++quarter)
            (void)fast.BobAt(At(tick - 1, quarter / 4.0 + 0.25));
    }
    const float expected = 1.0f - std::exp(-0.1f / CameraBob{}.Response);
    EXPECT_NEAR(slow.Bob.CurrentStrength(), expected, 1e-4f);
    EXPECT_NEAR(fast.Bob.CurrentStrength(), expected, 1e-4f);
}

TEST(CameraBob, SettlesOffTheGroundAndHoldsWhileTimeStands)
{
    Views views;
    views.Striding(0.25f, 6.0f);
    (void)views.BobAt(At(1));
    views.Striding(0.25f, 0.0f);
    const CameraViewOffset paused = views.BobAt(At(1));
    EXPECT_NEAR(paused.Translation.X, CameraBob{}.Sway, 1e-6f);
    EXPECT_TRUE(views.BobAt(At(61)).Translation.X < CameraBob{}.Sway * 1e-3f);
}

TEST(CameraBob, ZeroScaleNoBobOrAnotherCameraIsTheCamerasOwnView)
{
    Views views;
    views.Striding(0.25f, 6.0f);
    views.Bob.Settings.Scale = 0.0f;
    EXPECT_TRUE(views.BobAt(At(1)).IsIdentity());

    views.Bob.Settings.Scale = 1.0f;
    EXPECT_FALSE(views.BobAt(At(2)).IsIdentity());
    EXPECT_TRUE(views.Bob.OffsetFor(views.Pawn).IsIdentity());

    views.Entities.RemoveComponent<CameraBob>(views.Camera);
    EXPECT_TRUE(views.BobAt(At(3)).IsIdentity());
}

TEST(CameraTilt, LeansIntoSidewaysMotionUpToItsMost)
{
    Views views;
    (void)views.TiltAt(At(0));
    CameraViewOffset right;
    for (std::uint64_t tick = 1; tick <= 120; ++tick)
    {
        views.MovePawn(Vec3d(6.0f * kTickF, 0.0f, 0.0f));
        right = views.TiltAt(At(tick));
    }
    EXPECT_NEAR(std::abs(right.Rotation.Z), ToDegrees(CameraTilt{}.MaxRoll), 1e-3f);
    EXPECT_EQ(right.Translation, Vec3d::Zero());
    // The view's top leans the way it moves.
    EXPECT_GT(ViewUp(right).X, 0.0f);

    CameraViewOffset left;
    for (std::uint64_t tick = 121; tick <= 240; ++tick)
    {
        views.MovePawn(Vec3d(-20.0f * kTickF, 0.0f, 0.0f));
        left = views.TiltAt(At(tick));
    }
    EXPECT_NEAR(left.Rotation.Z, -right.Rotation.Z, 1e-3f);
    EXPECT_LT(ViewUp(left).X, 0.0f);
}

TEST(CameraTilt, ForwardMotionAndATeleportLeaveTheViewLevel)
{
    Views views;
    (void)views.TiltAt(At(0));
    for (std::uint64_t tick = 1; tick <= 30; ++tick)
    {
        views.MovePawn(Vec3d(0.0f, 0.0f, -7.0f * kTickF));
        EXPECT_NEAR(views.TiltAt(At(tick)).Rotation.Z, 0.0f, 1e-5f);
    }
    views.MovePawn(Vec3d(50.0f, 0.0f, 0.0f));
    EXPECT_NEAR(views.TiltAt(At(31)).Rotation.Z, 0.0f, 1e-5f);
}

TEST(CameraTilt, ZeroScaleKeepsTheViewLevel)
{
    Views views;
    views.Tilt.Settings.Scale = 0.0f;
    (void)views.TiltAt(At(0));
    for (std::uint64_t tick = 1; tick <= 30; ++tick)
    {
        views.MovePawn(Vec3d(6.0f * kTickF, 0.0f, 0.0f));
        EXPECT_TRUE(views.TiltAt(At(tick)).IsIdentity());
    }
}
