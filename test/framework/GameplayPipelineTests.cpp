#include <gtest/gtest.h>

#include <abilities/AbilityKit.h>
#include <app/EngineSchedule.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <input/InputFrame.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementModes.h>
#include <movement/MovementProfile.h>
#include <movement/MovementRegistration.h>
#include <movement/MovementState.h>
#include <movement/MovementTags.h>
#include <physics/components/CharacterController.h>
#include <runtime/RuntimeFrameLoop.h>

namespace
{
struct GameplayScheduleHarness
{
    GameplayScheduleHarness()
    {
        WorldState.RegisterComponent<CharacterController>();
        RegisterMovement(WorldState);
        ActivePartitions.Add(StoragePartitionId::Default());
    }

    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    InputFrame Input;
    World WorldState;
    StoragePartitionSet ActivePartitions;
    EngineSchedule Schedule;
};

struct TestInputSystem
{
    explicit TestInputSystem(EntityId pawn)
        : Pawn(pawn)
    {
    }

    void FixedLogic(FixedLogicContext& ctx)
    {
        if (!ctx.Entities.IsAlive(Pawn)
            || !ctx.Partitions.Contains(
                ctx.Entities.GetEntityPartition(Pawn)))
        {
            return;
        }

        if (MovementIntent* intent =
                ctx.Entities.TryGet<MovementIntent>(Pawn))
        {
            intent->WishDir = Vec3d(1.0f, 0.0f, 0.0f);
        }

        const MovementDefs* defs =
            ctx.Entities.TryGetResource<MovementDefs>();
        AbilityActivationQueue* queue =
            ctx.Entities.TryGetResource<AbilityActivationQueue>();
        if (defs != nullptr && queue != nullptr)
            queue->Pending.push_back({ Pawn, defs->Jump });
    }

    EntityId Pawn;
};

EntityId SpawnControlledPawn(World& world)
{
    const MovementDefs& defs = world.GetResource<MovementDefs>();
    const MovementTags& tags = world.GetResource<MovementTags>();

    const EntityId pawn = world.CreateEntity();
    CharacterController controller;
    controller.Grounded = true;
    world.AddComponent<CharacterController>(pawn, controller);
    world.AddComponent<MovementProfile>(pawn, MovementProfile{});
    world.AddComponent<MovementState>(pawn, MovementState{});
    world.AddComponent<MovementIntent>(pawn, MovementIntent{});
    world.AddComponent<LocomotionModeRequest>(
        pawn,
        LocomotionModeRequest{});
    world.AddComponent<OnGround>(pawn, OnGround{});

    GameplayTagContainer tagContainer;
    tagContainer.Grant(tags.Controlled);
    world.AddComponent<GameplayTagContainer>(pawn, tagContainer);

    AttributeSet attributes;
    attributes.Add(defs.MoveSpeed, 6.0f);
    world.AddComponent<AttributeSet>(pawn, attributes);

    AbilitySet abilities;
    abilities.Grant(defs.Jump);
    world.AddComponent<AbilitySet>(pawn, abilities);
    return pawn;
}
} // namespace

TEST(GameplayPipeline, OrdersInputModeAbilityJumpResolveLocomotionAndLifetime)
{
    GameplayScheduleHarness harness;
    const EntityId pawn = SpawnControlledPawn(harness.WorldState);

    RegisterAbilityKitSystems(harness.Schedule);
    RegisterMovementSystems(harness.Schedule);
    harness.Schedule.Register<TestInputSystem>(pawn);
    OrderMovementAfterInput<TestInputSystem>(harness.Schedule);
    harness.Schedule.Init();

    FixedLogicContext context{
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .Time = FixedSimTime{
            .DeltaSeconds = 0.10,
            .TickIndex = 1,
        },
        .Entities = harness.WorldState,
        .Partitions = harness.ActivePartitions,
    };
    harness.Schedule.RunFixedLogic(context);

    const MovementTags& tags =
        harness.WorldState.GetResource<MovementTags>();
    const GameplayTagContainer* tagContainer =
        harness.WorldState.TryGet<GameplayTagContainer>(pawn);
    const CharacterController* controller =
        harness.WorldState.TryGet<CharacterController>(pawn);
    const MovementState* state =
        harness.WorldState.TryGet<MovementState>(pawn);

    ASSERT_NE(tagContainer, nullptr);
    ASSERT_NE(controller, nullptr);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(tagContainer->HasExact(tags.Grounded));
    EXPECT_FALSE(tagContainer->HasExact(tags.JumpRequested));
    EXPECT_TRUE(tagContainer->HasExact(tags.JumpCooldown));
    EXPECT_FLOAT_EQ(controller->PendingJumpSpeed, 5.5f);
    EXPECT_GT(controller->DesiredVelocity.X, 0.0f);
    EXPECT_FLOAT_EQ(
        controller->DesiredVelocity.X,
        state->PlanarVelocity.X);
}
