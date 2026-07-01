#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <core/config/EngineConfig.h>
#include <abilities/AbilityKit.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementModes.h>
#include <movement/MovementProfile.h>
#include <movement/MovementState.h>
#include <movement/MovementTags.h>
#include <movement/MovementRegistration.h>
#include <input/InputFrame.h>
#include <physics/components/CharacterController.h>
#include <runtime/RuntimeFrameLoop.h>
#include <zone/ZoneRuntime.h>

namespace
{
    struct GameplayScheduleHarness
    {
        EngineConfig Config;
        RuntimeFrameLoop Runtime;
        InputFrame Input;
        ZoneRuntime Zones;
        EngineSchedule Schedule;

        Registry& CreateLogicZone()
        {
            Registry& registry = Zones.CreateZone(ZoneId{ 1 });
            Zones.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Logic = true });
            return registry;
        }
    };

    struct TestInputSystem
    {
        explicit TestInputSystem(EntityId pawn)
            : Pawn(pawn)
        {
        }

        void FixedLogic(FixedLogicContext& ctx)
        {
            for (Registry* reg : ctx.ActiveRegistries)
            {
                World& world = reg->Components;
                if (MovementIntent* intent = world.TryGet<MovementIntent>(Pawn))
                    intent->WishDir = Vec3d(1.0f, 0.0f, 0.0f);

                const MovementDefs* defs = world.TryGetResource<MovementDefs>();
                AbilityActivationQueue* queue = world.TryGetResource<AbilityActivationQueue>();
                if (defs != nullptr && queue != nullptr)
                    queue->Pending.push_back({ Pawn, defs->Jump });
            }
        }

        EntityId Pawn;
    };

    EntityId SpawnControlledPawn(Registry& registry)
    {
        World& world = registry.Components;
        world.RegisterComponent<CharacterController>();
        RegisterMovement(world);

        const MovementDefs& defs = world.GetResource<MovementDefs>();
        const MovementTags& tags = world.GetResource<MovementTags>();

        const EntityId pawn = world.CreateEntity();
        CharacterController controller;
        controller.Grounded = true;
        world.AddComponent<CharacterController>(pawn, controller);
        world.AddComponent<MovementProfile>(pawn, MovementProfile{});
        world.AddComponent<MovementState>(pawn, MovementState{});
        world.AddComponent<MovementIntent>(pawn, MovementIntent{});
        world.AddComponent<LocomotionModeRequest>(pawn, LocomotionModeRequest{});
        world.AddComponent<OnGround>(pawn, OnGround{});

        GameplayTagContainer tagContainer;
        tagContainer.Grant(tags.Controlled);
        world.AddComponent<GameplayTagContainer>(pawn, tagContainer);

        AttributeSet attrs;
        attrs.Add(defs.MoveSpeed, 6.0f);
        world.AddComponent<AttributeSet>(pawn, attrs);

        AbilitySet abilities;
        abilities.Grant(defs.Jump);
        world.AddComponent<AbilitySet>(pawn, abilities);
        return pawn;
    }
}

TEST(GameplayPipeline, OrdersInputModeAbilityJumpResolveLocomotionAndLifetime)
{
    GameplayScheduleHarness harness;
    Registry& registry = harness.CreateLogicZone();
    const EntityId pawn = SpawnControlledPawn(registry);

    RegisterAbilityKitSystems(harness.Schedule);
    RegisterMovementSystems(harness.Schedule);
    harness.Schedule.Register<TestInputSystem>(pawn);
    OrderMovementAfterInput<TestInputSystem>(harness.Schedule);
    harness.Schedule.Init();

    FrameRegistryView view = harness.Schedule.BuildFrameView(harness.Zones);
    FixedLogicContext ctx{
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .Time = FixedSimTime{ .DeltaSeconds = 0.10, .TickIndex = 1 },
        .Registries = view,
        .ActiveRegistries = view.Logic,
    };
    harness.Schedule.RunFixedLogic(ctx);

    World& world = registry.Components;
    const MovementTags& tags = world.GetResource<MovementTags>();
    const auto* tagContainer = world.TryGet<GameplayTagContainer>(pawn);
    const auto* controller = world.TryGet<CharacterController>(pawn);
    const auto* state = world.TryGet<MovementState>(pawn);

    ASSERT_NE(tagContainer, nullptr);
    ASSERT_NE(controller, nullptr);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(tagContainer->HasExact(tags.Grounded));
    EXPECT_FALSE(tagContainer->HasExact(tags.JumpRequested));
    EXPECT_TRUE(tagContainer->HasExact(tags.JumpCooldown));
    EXPECT_FLOAT_EQ(controller->PendingJumpSpeed, 5.5f);
    EXPECT_GT(controller->DesiredVelocity.X, 0.0f);
    EXPECT_FLOAT_EQ(controller->DesiredVelocity.X, state->PlanarVelocity.X);
}
