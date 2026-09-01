#include <gtest/gtest.h>

#include <abilities/AbilityKit.h>
#include <app/EngineSchedule.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <input/InputFrame.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <assets/data/DataAssetCache.h>
#include <movement/MovementComponentTraits.h>
#include <movement/JumpState.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
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
    DataAssetCache DataAssets;
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
            intent->Jump = true;
        }
    }

    EntityId Pawn;
};

EntityId SpawnControlledPawn(World& world)
{
    const MovementDefs& defs = world.GetResource<MovementDefs>();
    const MovementTags& tags = world.GetResource<MovementTags>();

    const EntityId pawn = world.CreateEntity();
    world.AddComponent<CharacterController>(pawn, CharacterController{});

    // The per-tick scratch the whole pipeline reads and writes comes with this:
    // it is what CharacterMovement owes.
    CharacterMovement movement;
    movement.Mode = world.GetResource<LocomotionModeRegistry>().FreeMode();
    world.AddComponent<CharacterMovement>(pawn, movement);

    // Stable support is the physical fact the whole tick reads: it gates the
    // jump and keeps gravity out of the locomotion result.
    world.TryGet<SupportState>(pawn)->Kind = SupportKind::Stable;

    GameplayTagContainer tagContainer;
    tagContainer.Grant(tags.Controlled);
    world.AddComponent<GameplayTagContainer>(pawn, tagContainer);

    AttributeSet attributes;
    attributes.Add(defs.MoveSpeed, 6.0f);
    world.AddComponent<AttributeSet>(pawn, attributes);

    world.AddComponent<AbilitySet>(pawn, AbilitySet{});
    return pawn;
}
} // namespace

TEST(GameplayPipeline, OrdersInputModeResolveLocomotionJumpAndComposition)
{
    GameplayScheduleHarness harness;
    const EntityId pawn = SpawnControlledPawn(harness.WorldState);

    RegisterAbilityKitSystems(harness.Schedule);
    RegisterMovementSystems(harness.Schedule, harness.DataAssets);
    harness.Schedule.Register<TestInputSystem>(pawn);
    OrderMovementAfterInput<TestInputSystem>(harness.Schedule);
    harness.Schedule.Init();

    FixedLogicContext context{
        .Config = harness.Config,
        .Runtime = harness.Runtime,
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
    const LocomotionOutput* locomotion =
        harness.WorldState.TryGet<LocomotionOutput>(pawn);
    const MotionRequest* request =
        harness.WorldState.TryGet<MotionRequest>(pawn);
    const MotionAxisOverride* contributions =
        harness.WorldState.TryGet<MotionAxisOverride>(pawn);

    ASSERT_NE(tagContainer, nullptr);
    ASSERT_NE(locomotion, nullptr);
    ASSERT_NE(request, nullptr);
    ASSERT_NE(contributions, nullptr);

    // Support projected its tag for the systems that query locomotion state.
    EXPECT_TRUE(tagContainer->HasExact(tags.Grounded));

    // The jump fired off the same support the sweep produces, and spent its
    // cooldown, which is what stops a held key firing again next tick.
    const JumpState* jump = harness.WorldState.TryGet<JumpState>(pawn);
    ASSERT_NE(jump, nullptr);
    EXPECT_GT(jump->CooldownRemaining, 0.0f);

    // Input reached locomotion, and composition folded the jump's up-axis
    // contribution over it into the single motor request.
    EXPECT_GT(locomotion->Velocity.X, 0.0f);
    EXPECT_FLOAT_EQ(request->Velocity.X, locomotion->Velocity.X);
    EXPECT_FLOAT_EQ(request->Velocity.Y, 5.5f);

    // Composition consumed the contribution channels, so the jump cannot
    // replay on the next tick.
    EXPECT_FALSE(contributions->HasUp);
}
