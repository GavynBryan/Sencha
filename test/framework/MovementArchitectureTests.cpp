#include <gtest/gtest.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/MotionComposition.h>
#include <movement/MovementComponents.h>
#include <movement/MovementIntent.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementModeSystems.h>

#include <utility>

struct TestClingCandidate
{
    EntityId Surface;
    Vec3d Normal{0.0f, 0.0f, 1.0f};
    Vec3d Anchor{};
};
SENCHA_DECLARE_COMPONENT_TYPE(TestClingCandidate, "test.cling_candidate");

namespace
{
    struct ModeWorld
    {
        World WorldState;
        GameplayTagRegistry* Tags = nullptr;
        LocomotionModeRegistry* Modes = nullptr;
        LocomotionModeId Free;
        LocomotionModeId Flight;
        LocomotionModeId Cling;

        ModeWorld()
        {
            WorldState.RegisterComponent<GameplayTagContainer>();
            WorldState.RegisterComponent<CharacterMovement>();
            WorldState.RegisterComponent<ModeTransitionRequest>();
            WorldState.RegisterComponent<FlightSession>();
            WorldState.RegisterComponent<ClingSession>();
            WorldState.RegisterComponent<TestClingCandidate>();

            Tags = &WorldState.AddResource<GameplayTagRegistry>();
            Modes = &WorldState.AddResource<LocomotionModeRegistry>(*Tags);
            Free = Modes->RegisterFree();
            Flight = Modes->Register<FlightSession>("movement.mode.flight");
            Cling = Modes->RegisterWithCandidate<ClingSession, TestClingCandidate>(
                "movement.mode.cling",
                [](const TestClingCandidate& candidate)
                {
                    return ClingSession{
                        .Surface = candidate.Surface,
                        .SurfaceNormal = candidate.Normal,
                        .Anchor = candidate.Anchor,
                    };
                });
        }

        EntityId Spawn()
        {
            const EntityId entity = WorldState.CreateEntity();
            WorldState.AddComponent<GameplayTagContainer>(entity, {});
            // The mailbox comes with the component that owes it.
            WorldState.AddComponent<CharacterMovement>(entity, CharacterMovement{ .Mode = Free });
            WorldState.TryGet<GameplayTagContainer>(entity)->Grant(Modes->Find(Free)->ActiveTag);
            return entity;
        }
    };
}

TEST(MovementModeMailboxTest, HigherClassReplacesAndSameClassIsFirstWriteWins)
{
    ModeWorld fixture;
    const EntityId entity = fixture.Spawn();

    EXPECT_TRUE(RequestLocomotionMode(
        fixture.WorldState, entity, fixture.Flight, ModeRequestClass::Automatic));
    EXPECT_FALSE(RequestLocomotionMode(
        fixture.WorldState, entity, fixture.Free, ModeRequestClass::Automatic));
    EXPECT_TRUE(RequestLocomotionMode(
        fixture.WorldState, entity, fixture.Cling, ModeRequestClass::Explicit));
    EXPECT_FALSE(RequestLocomotionMode(
        fixture.WorldState, entity, fixture.Flight, ModeRequestClass::Explicit));
    EXPECT_TRUE(RequestLocomotionMode(
        fixture.WorldState, entity, fixture.Free, ModeRequestClass::Forced));

    const ModeTransitionRequest* request =
        std::as_const(fixture.WorldState).TryGet<ModeTransitionRequest>(entity);
    ASSERT_NE(request, nullptr);
    EXPECT_EQ(request->Target, fixture.Free);
    EXPECT_EQ(request->Class, ModeRequestClass::Forced);
}

TEST(MovementModeTransitionTest, CandidateBecomesSessionAtomically)
{
    ModeWorld fixture;
    const EntityId entity = fixture.Spawn();
    const EntityId surface{42, 7};
    fixture.WorldState.AddComponent<TestClingCandidate>(entity, TestClingCandidate{
        .Surface = surface,
        .Normal = Vec3d(0.0f, 0.0f, -1.0f),
        .Anchor = Vec3d(1.0f, 2.0f, 3.0f),
    });

    ASSERT_TRUE(RequestLocomotionMode(
        fixture.WorldState, entity, fixture.Cling, ModeRequestClass::Explicit));
    LocomotionModeTransitionSystem{}.Step(fixture.WorldState);

    const CharacterMovement* movement =
        std::as_const(fixture.WorldState).TryGet<CharacterMovement>(entity);
    const ClingSession* session =
        std::as_const(fixture.WorldState).TryGet<ClingSession>(entity);
    const GameplayTagContainer* tags =
        std::as_const(fixture.WorldState).TryGet<GameplayTagContainer>(entity);
    ASSERT_NE(movement, nullptr);
    ASSERT_NE(session, nullptr);
    ASSERT_NE(tags, nullptr);
    EXPECT_EQ(movement->Mode, fixture.Cling);
    EXPECT_EQ(session->Surface, surface);
    EXPECT_EQ(session->SurfaceNormal, Vec3d(0.0f, 0.0f, -1.0f));
    EXPECT_EQ(session->Anchor, Vec3d(1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(fixture.WorldState.HasComponent<TestClingCandidate>(entity));
    EXPECT_TRUE(tags->HasExact(fixture.Modes->Find(fixture.Cling)->ActiveTag));
    EXPECT_FALSE(tags->HasExact(fixture.Modes->Find(fixture.Free)->ActiveTag));
}

TEST(MovementModeTransitionTest, MissingCandidateLeavesCurrentModeIntact)
{
    ModeWorld fixture;
    const EntityId entity = fixture.Spawn();

    ASSERT_TRUE(RequestLocomotionMode(
        fixture.WorldState, entity, fixture.Cling, ModeRequestClass::Explicit));
    LocomotionModeTransitionSystem{}.Step(fixture.WorldState);

    const CharacterMovement* movement =
        std::as_const(fixture.WorldState).TryGet<CharacterMovement>(entity);
    const GameplayTagContainer* tags =
        std::as_const(fixture.WorldState).TryGet<GameplayTagContainer>(entity);
    ASSERT_NE(movement, nullptr);
    ASSERT_NE(tags, nullptr);
    EXPECT_EQ(movement->Mode, fixture.Free);
    EXPECT_FALSE(fixture.WorldState.HasComponent<ClingSession>(entity));
    EXPECT_TRUE(tags->HasExact(fixture.Modes->Find(fixture.Free)->ActiveTag));
}

TEST(MotionCompositionTest, ComposesRelativeChannelsSupportAndImpulse)
{
    LocomotionOutput locomotion{
        .Velocity = Vec3d(3.0f, 2.0f, 0.0f),
        .UpAxis = Vec3d(0.0f, 1.0f, 0.0f),
        .GravityScale = 0.5f,
    };
    SupportState support;
    support.Kind = SupportKind::Stable;
    support.SurfaceVelocity = Vec3d(1.0f, 4.0f, 0.0f);
    MotionAxisOverride overrides;
    overrides.HasPlanar = true;
    overrides.PlanarVelocity = Vec3d(10.0f, 99.0f, 0.0f);
    overrides.HasUp = true;
    overrides.UpVelocity = 5.0f;
    MotionImpulse impulses{ .DeltaVelocity = Vec3d(0.0f, 1.0f, 2.0f) };

    const MotionRequest request = ComposeMotion(locomotion, &support, overrides, impulses);
    EXPECT_EQ(request.UpAxis, Vec3d(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(request.Velocity, Vec3d(11.0f, 10.0f, 2.0f));
    EXPECT_FLOAT_EQ(request.GravityScale, 0.5f);
}

TEST(MotionCompositionTest, SystemConsumesTransientContributionsOnce)
{
    World world;
    world.RegisterComponent<LocomotionOutput>();
    world.RegisterComponent<MotionAxisOverride>();
    world.RegisterComponent<MotionImpulse>();
    world.RegisterComponent<MotionRequest>();
    world.RegisterComponent<SupportState>();
    const EntityId entity = world.CreateEntity();
    world.AddComponent<LocomotionOutput>(entity, LocomotionOutput{ .Velocity = Vec3d(2.0f, 0.0f, 0.0f) });
    world.AddComponent<MotionAxisOverride>(entity, {});
    world.AddComponent<MotionImpulse>(entity, MotionImpulse{ .DeltaVelocity = Vec3d(1.0f, 0.0f, 0.0f) });
    world.AddComponent<MotionRequest>(entity, {});
    world.AddComponent<SupportState>(entity, {});

    MotionCompositionSystem system;
    system.Step(world);
    EXPECT_EQ(std::as_const(world).TryGet<MotionRequest>(entity)->Velocity,
              Vec3d(3.0f, 0.0f, 0.0f));
    EXPECT_EQ(std::as_const(world).TryGet<MotionImpulse>(entity)->DeltaVelocity, Vec3d::Zero());
    system.Step(world);
    EXPECT_EQ(std::as_const(world).TryGet<MotionRequest>(entity)->Velocity,
              Vec3d(2.0f, 0.0f, 0.0f));
}

TEST(FreeLocomotionTest, UsesSupportRelativeAchievedVelocity)
{
    KinematicState kinematic{ .Velocity = Vec3d(5.0f, 0.0f, 0.0f) };
    SupportState support;
    support.Kind = SupportKind::Stable;
    support.SurfaceVelocity = Vec3d(2.0f, 0.0f, 0.0f);
    MovementIntent intent{};
    ResolvedMovementTuning tuning;
    tuning.Friction = 1.0f;
    tuning.StopSpeed = 0.0f;
    tuning.Drag = 0.0f;

    const LocomotionOutput output = StepFreeLocomotion(
        kinematic, support, intent, tuning,
        Vec3d(0.0f, -10.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), 0.5f);
    EXPECT_NEAR(output.Velocity.X, 1.5f, 1.0e-5f);
    EXPECT_FLOAT_EQ(output.Velocity.Y, 0.0f);
}

TEST(FreeLocomotionTest, WishSpeedCapLimitsAirAccelerationTerm)
{
    KinematicState kinematic{};
    SupportState support;
    support.Kind = SupportKind::None;
    MovementIntent intent{ .WishDir = Vec3d(1.0f, 0.0f, 0.0f) };
    ResolvedMovementTuning tuning;
    tuning.MaxSpeed = 6.0f;
    tuning.Acceleration = 10.0f;
    tuning.Friction = 0.0f;
    tuning.WishSpeedCap = 1.0f;
    tuning.GravityScale = 0.0f;

    const LocomotionOutput output = StepFreeLocomotion(
        kinematic, support, intent, tuning,
        Vec3d(0.0f, -10.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), 1.0f);
    EXPECT_NEAR(output.Velocity.X, 1.0f, 1.0e-5f);
}

TEST(FreeLocomotionTest, GravityIntegratesOnlyWhenUnsupported)
{
    KinematicState kinematic{};
    MovementIntent intent{};
    ResolvedMovementTuning tuning;
    tuning.Friction = 0.0f;
    tuning.GravityScale = 0.5f;

    SupportState unsupported;
    unsupported.Kind = SupportKind::None;
    const LocomotionOutput falling = StepFreeLocomotion(
        kinematic, unsupported, intent, tuning,
        Vec3d(0.0f, -10.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), 1.0f);
    EXPECT_NEAR(falling.Velocity.Y, -5.0f, 1.0e-5f);

    SupportState stable;
    stable.Kind = SupportKind::Stable;
    const LocomotionOutput planted = StepFreeLocomotion(
        kinematic, stable, intent, tuning,
        Vec3d(0.0f, -10.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(planted.Velocity.Y, 0.0f);
}
