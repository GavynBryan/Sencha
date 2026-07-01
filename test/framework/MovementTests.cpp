#include <gtest/gtest.h>

#include <ecs/World.h>
#include <abilities/AbilityKit.h>
#include <movement/LocomotionMode.h>
#include <movement/AirLocomotionSystem.h>
#include <movement/GroundLocomotionSystem.h>
#include <movement/GroundingTransitionSystem.h>
#include <movement/JumpExecutionSystem.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementModes.h>
#include <movement/MovementProfile.h>
#include <movement/MovementRegistration.h>
#include <movement/MovementState.h>
#include <movement/MovementTags.h>
#include <physics/components/CharacterController.h>

// A game-defined locomotion mode, declared OUTSIDE the engine (this test stands in
// for a game module). Its presence proves the OCP guarantee: adding it touches no
// file under engine/.
struct Climbing {};
SENCHA_DECLARE_COMPONENT_TYPE(Climbing, "game.climbing");

namespace
{
    constexpr float kTick = 1.0f / 60.0f;

    struct MovementWorld
    {
        World world;
        MovementDefs defs;
        MovementTags tags;

        MovementWorld()
        {
            world.RegisterComponent<CharacterController>();
            RegisterMovement(world);
            defs = world.GetResource<MovementDefs>();
            tags = world.GetResource<MovementTags>();
        }

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
            world.AddComponent<LocomotionModeRequest>(e, LocomotionModeRequest{});
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
            if (world.HasComponent<OnGround>(e)) world.RemoveComponent<OnGround>(e);
            if (!world.HasComponent<InAir>(e)) world.AddComponent<InAir>(e, InAir{});
        }
    };
}

TEST(GroundLocomotionTest, AcceleratesTowardWishAndClampsToMoveSpeed)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn();
    mw.SetWish(pawn, Vec3d(1.0f, 0.0f, 0.0f));

    GroundLocomotionSystem sys;
    sys.Step(mw.world, kTick);

    EXPECT_GT(mw.State(pawn).PlanarVelocity.X, 0.0f);
    EXPECT_LE(mw.State(pawn).PlanarVelocity.X, 6.0f);
    EXPECT_FLOAT_EQ(mw.Controller(pawn).DesiredVelocity.X, mw.State(pawn).PlanarVelocity.X);
}

TEST(GroundLocomotionTest, ConvergesToMoveSpeedAttribute)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn();
    mw.SetWish(pawn, Vec3d(1.0f, 0.0f, 0.0f));

    GroundLocomotionSystem sys; // one runner, many ticks: exercises cached-query rebind
    for (int i = 0; i < 600; ++i)
        sys.Step(mw.world, kTick);

    EXPECT_NEAR(mw.State(pawn).PlanarVelocity.X, 6.0f, 1e-2f);
}

TEST(GroundLocomotionTest, FrictionStopsIdleCharacter)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn();
    mw.State(pawn).PlanarVelocity = Vec3d(6.0f, 0.0f, 0.0f);

    GroundLocomotionSystem sys;
    for (int i = 0; i < 600; ++i)
        sys.Step(mw.world, kTick);

    EXPECT_NEAR(mw.State(pawn).PlanarVelocity.X, 0.0f, 1e-2f);
}

TEST(AirLocomotionTest, ReducedControlAndNoFriction)
{
    MovementWorld ground;
    const EntityId g = ground.SpawnPawn();
    ground.SetWish(g, Vec3d(1.0f, 0.0f, 0.0f));
    GroundLocomotionSystem groundSys;
    groundSys.Step(ground.world, kTick);

    MovementWorld air;
    const EntityId a = air.SpawnPawn();
    air.MakeAirborne(a);
    air.SetWish(a, Vec3d(1.0f, 0.0f, 0.0f));
    AirLocomotionSystem airSys;
    airSys.Step(air.world, kTick);

    EXPECT_GT(air.State(a).PlanarVelocity.X, 0.0f);
    EXPECT_LT(air.State(a).PlanarVelocity.X, ground.State(g).PlanarVelocity.X);

    air.State(a).PlanarVelocity = Vec3d(3.0f, 0.0f, 0.0f);
    air.SetWish(a, Vec3d::Zero());
    airSys.Step(air.world, kTick);
    EXPECT_FLOAT_EQ(air.State(a).PlanarVelocity.X, 3.0f);
}

TEST(AirLocomotionTest, StrafeGainsSpeed)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn();
    mw.MakeAirborne(pawn);
    mw.State(pawn).PlanarVelocity = Vec3d(3.0f, 0.0f, 0.0f);
    const float before = mw.State(pawn).PlanarVelocity.Magnitude();

    mw.SetWish(pawn, Vec3d(0.0f, 0.0f, 1.0f));
    AirLocomotionSystem sys;
    sys.Step(mw.world, kTick);

    EXPECT_GT(mw.State(pawn).PlanarVelocity.Z, 0.0f);
    EXPECT_GT(mw.State(pawn).PlanarVelocity.Magnitude(), before);
}

TEST(LocomotionModeTest, GroundingRequestsAndArbiterProjectsTagsAndMarkers)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn(/*grounded*/ true);

    RequestGroundingLocomotionModes(mw.world);
    ApplyLocomotionModes(mw.world);
    EXPECT_TRUE(mw.world.HasComponent<OnGround>(pawn));
    EXPECT_TRUE(mw.Tags(pawn).HasExact(mw.tags.Grounded));

    GameplayTagQuery query;
    query.AddAll(mw.tags.Grounded, GameplayTagMatchMode::Hierarchical);
    EXPECT_TRUE(query.Matches(mw.Tags(pawn), mw.world.GetResource<GameplayTagRegistry>()));

    mw.Controller(pawn).Grounded = false;
    RequestGroundingLocomotionModes(mw.world);
    ApplyLocomotionModes(mw.world);
    EXPECT_TRUE(mw.world.HasComponent<InAir>(pawn));
    EXPECT_FALSE(mw.world.HasComponent<OnGround>(pawn));
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.Grounded));
    EXPECT_TRUE(mw.Tags(pawn).HasExact(mw.tags.Airborne));
}

TEST(LocomotionModeTest, ProjectionIsMutuallyExclusiveAndDoesNotStack)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn(/*grounded*/ true);

    for (int i = 0; i < 16; ++i)
    {
        RequestGroundingLocomotionModes(mw.world);
        ApplyLocomotionModes(mw.world);
    }

    EXPECT_EQ(mw.Tags(pawn).StackCount(mw.tags.Grounded), 1);
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.Airborne));

    mw.Controller(pawn).Grounded = false;
    RequestGroundingLocomotionModes(mw.world);
    ApplyLocomotionModes(mw.world);
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.Grounded));
    EXPECT_EQ(mw.Tags(pawn).StackCount(mw.tags.Airborne), 1);
}

TEST(JumpExecutionTest, GroundedActivationSetsPendingSpeedAndSingleFires)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn(/*grounded*/ true);
    RequestGroundingLocomotionModes(mw.world);
    ApplyLocomotionModes(mw.world); // grant movement.grounded so Jump can activate

    mw.world.GetResource<AbilityActivationQueue>().Pending.push_back({ pawn, mw.defs.Jump });
    ProcessAbilityActivations(mw.world);
    EXPECT_TRUE(mw.Tags(pawn).HasExact(mw.tags.JumpRequested));

    JumpExecutionSystem jump;
    jump.Step(mw.world);
    EXPECT_FLOAT_EQ(mw.Controller(pawn).PendingJumpSpeed, 5.5f);
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.JumpRequested));

    mw.Controller(pawn).PendingJumpSpeed = 0.0f;
    mw.world.GetResource<AbilityActivationQueue>().Pending.push_back({ pawn, mw.defs.Jump });
    ProcessAbilityActivations(mw.world); // cooldown blocks
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.JumpRequested));
    jump.Step(mw.world);
    EXPECT_FLOAT_EQ(mw.Controller(pawn).PendingJumpSpeed, 0.0f);
}

TEST(JumpExecutionTest, AirborneActivationIsGatedOut)
{
    MovementWorld mw;
    const EntityId pawn = mw.SpawnPawn(/*grounded*/ false);
    mw.MakeAirborne(pawn);
    mw.Controller(pawn).Grounded = false;
    RequestGroundingLocomotionModes(mw.world);
    ApplyLocomotionModes(mw.world);
    ASSERT_FALSE(mw.Tags(pawn).HasExact(mw.tags.Grounded));

    mw.world.GetResource<AbilityActivationQueue>().Pending.push_back({ pawn, mw.defs.Jump });
    ProcessAbilityActivations(mw.world);

    JumpExecutionSystem jump;
    jump.Step(mw.world);
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.JumpRequested));
    EXPECT_FLOAT_EQ(mw.Controller(pawn).PendingJumpSpeed, 0.0f);
}

// The OCP guarantee, in CI: a game adds a locomotion mode with a marker + a mode
// registration + its own request, touching zero engine files. The generic arbiter
// gives the entity the mode by priority, and the engine's ground system does not
// touch it (archetype dispatch).
TEST(LocomotionModeTest, GameModeExtendsWithoutEngineEdits)
{
    MovementWorld mw;
    mw.world.RegisterComponent<Climbing>();
    const GameplayTagId climbingTag =
        *mw.world.GetResource<GameplayTagRegistry>().RegisterTag("game.movement.climbing");
    RegisterLocomotionMode<Climbing>(mw.world.GetResource<LocomotionModeRegistry>(), climbingTag);

    const EntityId pawn = mw.SpawnPawn(/*grounded*/ true);
    RequestGroundingLocomotionModes(mw.world);
    ApplyLocomotionModes(mw.world);
    ASSERT_TRUE(mw.world.HasComponent<OnGround>(pawn));

    // A game "transition" requests Climbing above the ground priority.
    RequestLocomotionMode(mw.world, pawn, ResolveComponentTypeId<Climbing>(), /*priority*/ 10);
    RequestGroundingLocomotionModes(mw.world); // ground still requests OnGround at priority 1, and loses
    ApplyLocomotionModes(mw.world);

    EXPECT_TRUE(mw.world.HasComponent<Climbing>(pawn));
    EXPECT_FALSE(mw.world.HasComponent<OnGround>(pawn));
    EXPECT_TRUE(mw.Tags(pawn).HasExact(climbingTag));
    EXPECT_FALSE(mw.Tags(pawn).HasExact(mw.tags.Grounded));

    // The engine's ground system must skip the climbing entity (it is not OnGround).
    mw.State(pawn).PlanarVelocity = Vec3d::Zero();
    mw.SetWish(pawn, Vec3d(1.0f, 0.0f, 0.0f));
    GroundLocomotionSystem engineGround;
    engineGround.Step(mw.world, kTick);
    EXPECT_FLOAT_EQ(mw.State(pawn).PlanarVelocity.X, 0.0f);
}
