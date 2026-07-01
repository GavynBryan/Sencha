#include <gtest/gtest.h>

#include <ecs/World.h>
#include <framework/AbilityKit.h>
#include <framework/movement/MovementDefs.h>
#include <framework/movement/MovementIntent.h>
#include <framework/movement/MovementModes.h>
#include <framework/movement/MovementProfile.h>
#include <framework/movement/MovementState.h>
#include <framework/movement/MovementSystems.h>
#include <framework/movement/MovementTags.h>
#include <physics/components/CharacterController.h>

namespace
{
    constexpr float kTick = 1.0f / 60.0f;

    // A world wired the way InitializeMovementRegistry leaves a zone World, plus
    // the physics CharacterController the locomotor reads and writes.
    struct MovementWorld
    {
        World world;
        MovementDefs defs;
        MovementTags tags;

        MovementWorld()
        {
            world.RegisterComponent<CharacterController>();
            InitializeMovementRegistry(world);
            defs = world.GetResource<MovementDefs>();
            tags = world.GetResource<MovementTags>();
        }

        // Grounded pawn: OnGround marker, MoveSpeed attribute, Jump granted.
        EntityId SpawnPawn(bool grounded = true)
        {
            const EntityId e = world.CreateEntity();
            CharacterController controller;
            controller.Grounded = grounded;
            world.AddComponent<CharacterController>(e, controller);
            world.AddComponent<MovementProfile>(e, MovementProfile{});
            world.AddComponent<MovementState>(e, MovementState{});
            world.AddComponent<MovementIntent>(e, MovementIntent{});
            world.AddComponent<GameplayTagContainer>(e, GameplayTagContainer{});

            AttributeSet attrs{};
            attrs.Add(defs.MoveSpeed, 6.0f);
            world.AddComponent<AttributeSet>(e, attrs);

            AbilitySet abilities{};
            abilities.Grant(defs.Jump);
            world.AddComponent<AbilitySet>(e, abilities);

            world.AddComponent<OnGround>(e, OnGround{});
            return e;
        }

        void SetWish(EntityId e, const Vec3d& dir) { world.TryGet<MovementIntent>(e)->WishDir = dir; }
        MovementState& State(EntityId e) { return *world.TryGet<MovementState>(e); }
        CharacterController& Controller(EntityId e) { return *world.TryGet<CharacterController>(e); }
        GameplayTagContainer& Tags(EntityId e) { return *world.TryGet<GameplayTagContainer>(e); }

        void MakeAirborne(EntityId e)
        {
            if (world.HasComponent<OnGround>(e))
                world.RemoveComponent<OnGround>(e);
            if (!world.HasComponent<InAir>(e))
                world.AddComponent<InAir>(e, InAir{});
        }
    };
}

TEST(GroundLocomotion, AcceleratesTowardWishAndClampsToMoveSpeed)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn();
    mw.SetWish(pawn, Vec3d(1.0f, 0.0f, 0.0f));

    TickGroundLocomotion(mw.world, kTick);

    EXPECT_GT(mw.State(pawn).PlanarVelocity.X, 0.0f);
    EXPECT_FLOAT_EQ(mw.State(pawn).PlanarVelocity.Y, 0.0f);
    EXPECT_LE(mw.State(pawn).PlanarVelocity.X, 6.0f);
    // The resolved velocity is written out to the physics controller.
    EXPECT_FLOAT_EQ(mw.Controller(pawn).DesiredVelocity.X, mw.State(pawn).PlanarVelocity.X);
}

TEST(GroundLocomotion, ConvergesToMoveSpeedAttribute)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn();
    mw.SetWish(pawn, Vec3d(1.0f, 0.0f, 0.0f));

    for (int i = 0; i < 600; ++i)
        TickGroundLocomotion(mw.world, kTick);

    EXPECT_NEAR(mw.State(pawn).PlanarVelocity.X, 6.0f, 1e-2f);
}

TEST(GroundLocomotion, FrictionStopsIdleCharacter)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn();
    mw.State(pawn).PlanarVelocity = Vec3d(6.0f, 0.0f, 0.0f);

    for (int i = 0; i < 600; ++i)
        TickGroundLocomotion(mw.world, kTick);

    EXPECT_NEAR(mw.State(pawn).PlanarVelocity.X, 0.0f, 1e-2f);
}

TEST(AirLocomotion, ReducedControlAndNoFriction)
{
    MovementWorld ground;
    const EntityId g = ground.SpawnPawn();
    ground.SetWish(g, Vec3d(1.0f, 0.0f, 0.0f));
    TickGroundLocomotion(ground.world, kTick);

    MovementWorld air;
    const EntityId a = air.SpawnPawn();
    air.MakeAirborne(a);
    air.SetWish(a, Vec3d(1.0f, 0.0f, 0.0f));
    TickAirLocomotion(air.world, kTick);

    // Air steering has less authority than ground for the same wish and dt.
    EXPECT_GT(air.State(a).PlanarVelocity.X, 0.0f);
    EXPECT_LT(air.State(a).PlanarVelocity.X, ground.State(g).PlanarVelocity.X);

    // No air friction: an idle airborne character keeps its horizontal velocity.
    air.State(a).PlanarVelocity = Vec3d(3.0f, 0.0f, 0.0f);
    air.SetWish(a, Vec3d::Zero());
    TickAirLocomotion(air.world, kTick);
    EXPECT_FLOAT_EQ(air.State(a).PlanarVelocity.X, 3.0f);
}

TEST(LocomotionTransitions, GroundedGrantsMarkerAndTags)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn(/*grounded*/ true);
    mw.SetWish(pawn, Vec3d(1.0f, 0.0f, 0.0f));

    TickLocomotionTransitions(mw.world, kTick);

    EXPECT_TRUE(mw.world.HasComponent<OnGround>(pawn));
    EXPECT_FALSE(mw.world.HasComponent<InAir>(pawn));
    EXPECT_TRUE(mw.Tags(pawn).HasExact(mw.tags.Grounded));
    EXPECT_TRUE(mw.Tags(pawn).HasExact(mw.tags.GroundedWalking));
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.Airborne));

    GameplayTagQuery query;
    query.AddAll(mw.tags.Grounded, GameplayTagMatchMode::Hierarchical);
    EXPECT_TRUE(query.Matches(mw.Tags(pawn), mw.world.GetResource<GameplayTagRegistry>()));
}

TEST(LocomotionTransitions, LeavingGroundFlipsToAirAfterCoyote)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn(/*grounded*/ true);
    TickLocomotionTransitions(mw.world, kTick);
    ASSERT_TRUE(mw.Tags(pawn).HasExact(mw.tags.Grounded));

    mw.Controller(pawn).Grounded = false;
    // Coyote grace (0.1s) keeps the grounded tag for a few ticks past contact loss.
    TickLocomotionTransitions(mw.world, kTick);
    EXPECT_TRUE(mw.world.HasComponent<InAir>(pawn)); // marker follows real contact
    EXPECT_TRUE(mw.Tags(pawn).HasExact(mw.tags.Grounded));

    for (int i = 0; i < 20; ++i)
        TickLocomotionTransitions(mw.world, kTick);

    EXPECT_TRUE(mw.world.HasComponent<InAir>(pawn));
    EXPECT_FALSE(mw.world.HasComponent<OnGround>(pawn));
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.Grounded));
    EXPECT_TRUE(mw.Tags(pawn).HasExact(mw.tags.Airborne));
}

TEST(LocomotionTransitions, SettledCharacterDoesNotStackTags)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn(/*grounded*/ true);

    for (int i = 0; i < 16; ++i)
        TickLocomotionTransitions(mw.world, kTick);

    EXPECT_EQ(mw.Tags(pawn).StackCount(mw.tags.Grounded), 1);
    EXPECT_EQ(mw.Tags(pawn).StackCount(mw.tags.GroundedIdle), 1);
}

TEST(JumpAbility, GroundedActivationSetsPendingSpeedAndSingleFires)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn(/*grounded*/ true);
    TickLocomotionTransitions(mw.world, kTick); // grant movement.grounded so Jump can activate

    mw.world.GetResource<AbilityActivationQueue>().Pending.push_back({ pawn, mw.defs.Jump });
    ProcessAbilityActivations(mw.world);
    EXPECT_TRUE(mw.Tags(pawn).HasExact(mw.tags.JumpRequested));
    EXPECT_TRUE(mw.Tags(pawn).HasExact(mw.tags.JumpCooldown));

    TickJumpExecution(mw.world);
    EXPECT_FLOAT_EQ(mw.Controller(pawn).PendingJumpSpeed, 5.5f);
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.JumpRequested)); // consumed

    // Cooldown blocks a second jump this window.
    mw.Controller(pawn).PendingJumpSpeed = 0.0f;
    mw.world.GetResource<AbilityActivationQueue>().Pending.push_back({ pawn, mw.defs.Jump });
    ProcessAbilityActivations(mw.world);
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.JumpRequested));
    TickJumpExecution(mw.world);
    EXPECT_FLOAT_EQ(mw.Controller(pawn).PendingJumpSpeed, 0.0f);
}

TEST(JumpAbility, AirborneActivationIsGatedOut)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn(/*grounded*/ false);
    mw.MakeAirborne(pawn);
    TickLocomotionTransitions(mw.world, kTick); // no contact, no coyote armed -> not grounded
    for (int i = 0; i < 20; ++i)
        TickLocomotionTransitions(mw.world, kTick);
    ASSERT_FALSE(mw.Tags(pawn).HasExact(mw.tags.Grounded));

    mw.world.GetResource<AbilityActivationQueue>().Pending.push_back({ pawn, mw.defs.Jump });
    ProcessAbilityActivations(mw.world);
    TickJumpExecution(mw.world);

    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.JumpRequested));
    EXPECT_FLOAT_EQ(mw.Controller(pawn).PendingJumpSpeed, 0.0f);
}
