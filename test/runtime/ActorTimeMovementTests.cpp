// A dilated character moves in its own time: one step of scaled time when slowed,
// as many complete steps as its scale rounds up to when hastened, carried by the
// ground at the ground's speed, and stepped exactly once a tick in the schedule.

#include <app/EngineSchedule.h>
#include <core/config/EngineConfig.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <movement/CharacterTickStep.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <abilities/AbilityKit.h>
#include <assets/data/DataAssetCache.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/ActorTime.h>
#include <time/ActorTimeLeases.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

namespace
{
constexpr float kDt = 1.0f / 60.0f;
const Vec3d kGravity(0.0f, -9.81f, 0.0f);
const Vec3d kUp(0.0f, 1.0f, 0.0f);

struct Walkers
{
    World Entities;

    Walkers()
    {
        Entities.RegisterComponent<LocalTransform>();
        RegisterMovement(Entities);
    }

    EntityId Walker()
    {
        const EntityId walker = Entities.CreateEntity();
        Entities.AddComponent<LocalTransform>(walker, LocalTransform{});
        Entities.AddComponent<CharacterMovement>(
            walker, CharacterMovement{ .Mode = Entities.GetResource<LocomotionModeRegistry>().FreeMode() });
        Entities.TryGet<SupportState>(walker)->Kind = SupportKind::Stable;
        return walker;
    }

    void Step(EntityId walker, std::int64_t scaleQ16, float dt = kDt)
    {
        MovementIntent intent;
        intent.WishDir = Vec3d(1.0f, 0.0f, 0.0f);
        StepScaledCharacterTick(Entities, nullptr, walker, intent, 0, dt, scaleQ16, kGravity, kUp);
    }

    Vec3d Position(EntityId walker) { return Entities.TryGet<LocalTransform>(walker)->Value.Position; }
};
}

TEST(ActorTimeMovement, AFastActorMatchesASlowerOneOverEqualActorTime)
{
    Walkers world;
    const EntityId fast = world.Walker();
    const EntityId normal = world.Walker();
    for (int tick = 0; tick < 30; ++tick)
        world.Step(fast, 2 * kActorTimeOne);
    for (int tick = 0; tick < 60; ++tick)
        world.Step(normal, kActorTimeOne);
    EXPECT_EQ(world.Position(fast).X, world.Position(normal).X) << "two substeps of one tick each, exactly";
}

TEST(ActorTimeMovement, AFastActorRunsAsManySubstepsAsItsScaleRoundsUpTo)
{
    Walkers world;
    const EntityId hastened = world.Walker();
    const EntityId reference = world.Walker();
    for (int tick = 0; tick < 30; ++tick)
        world.Step(hastened, kActorTimeOne * 3 / 2);
    for (int step = 0; step < 60; ++step)
        world.Step(reference, kActorTimeOne, kDt * 0.75f);
    EXPECT_EQ(world.Position(hastened).X, world.Position(reference).X) << "1.5 runs two steps of three quarters";
}

TEST(ActorTimeMovement, ASlowedActorCoversItsScaledDistance)
{
    Walkers world;
    const EntityId slowed = world.Walker();
    const EntityId normal = world.Walker();
    for (int tick = 0; tick < 240; ++tick)
    {
        world.Step(slowed, kActorTimeOne / 2);
        world.Step(normal, kActorTimeOne);
    }
    const float before = world.Position(slowed).X;
    const float normalBefore = world.Position(normal).X;
    world.Step(slowed, kActorTimeOne / 2);
    world.Step(normal, kActorTimeOne);
    EXPECT_NEAR(world.Position(slowed).X - before, (world.Position(normal).X - normalBefore) * 0.5f, 1.0e-4f)
        << "at full speed, half the distance a tick";
}

TEST(ActorTimeMovement, ADilatedActorRidesAPlatformAtThePlatformsSpeed)
{
    Walkers world;
    const EntityId rider = world.Walker();
    world.Entities.TryGet<SupportState>(rider)->SurfaceVelocity = Vec3d(0.0f, 0.0f, 2.0f);
    MovementIntent still;
    const auto ride = [&](int ticks) {
        for (int tick = 0; tick < ticks; ++tick)
        {
            world.Entities.TryGet<SupportState>(rider)->Kind = SupportKind::Stable;
            StepScaledCharacterTick(world.Entities, nullptr, rider, still, 0, kDt, kActorTimeOne / 2, kGravity, kUp);
        }
    };
    ride(600);
    const float settled = world.Position(rider).Z;
    ride(10);
    EXPECT_NEAR(world.Position(rider).Z - settled, 2.0f * 10.0f * kDt, 1.0e-4f)
        << "carried at the platform's speed once friction has matched it";
}

TEST(ActorTimeMovement, TheScheduleStepsADilatedCharacterExactlyOnceATick)
{
    EngineConfig config;
    RuntimeFrameLoop runtime;
    DataAssetCache dataAssets;
    EngineSchedule schedule;
    World world;
    world.RegisterComponent<LocalTransform>();
    RegisterMovement(world);
    RegisterAbilityKitSystems(schedule);
    RegisterMovementSystems(schedule, dataAssets);
    schedule.Init();
    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());

    Walkers reference;
    const EntityId expected = reference.Walker();
    const auto spawn = [&] {
        const EntityId walker = world.CreateEntity();
        world.AddComponent<LocalTransform>(walker, LocalTransform{});
        world.AddComponent<CharacterMovement>(
            walker, CharacterMovement{ .Mode = world.GetResource<LocomotionModeRegistry>().FreeMode() });
        return walker;
    };
    const EntityId slowed = spawn();
    world.AddComponent<ActorTimeLeases>(slowed, ActorTimeLeases{});
    ASSERT_TRUE(AcquireActorTimeLease(*world.TryGet<ActorTimeLeases>(slowed), LeaseOwner{ .Entity = slowed },
                                      kActorTimeOne / 2, 0));
    for (std::uint64_t tick = 0; tick < 20; ++tick)
    {
        world.TryGet<MovementIntent>(slowed)->WishDir = Vec3d(1.0f, 0.0f, 0.0f);
        FixedLogicContext context{ .Config = config, .Runtime = runtime,
                                   .Time = FixedSimTime{ .DeltaSeconds = kDt, .TickIndex = tick },
                                   .Entities = world, .Partitions = partitions };
        schedule.RunFixedLogic(context);
        if (tick >= 1)
        {
            EXPECT_TRUE(world.HasComponent<ActorTimeDilated>(slowed));
        }
        // The reference steps the same kernel from the first dilated tick; before it,
        // the bulk path has no physics to move anything with.
        if (tick == 0)
            continue;
        MovementIntent intent;
        intent.WishDir = Vec3d(1.0f, 0.0f, 0.0f);
        StepScaledCharacterTick(reference.Entities, nullptr, expected, intent, tick, kDt, kActorTimeOne / 2, kGravity,
                                kUp);
    }
    EXPECT_NEAR(world.TryGet<LocalTransform>(slowed)->Value.Position.X, reference.Position(expected).X, 1.0e-5f);

    ReleaseActorTimeLeases(*world.TryGet<ActorTimeLeases>(slowed), LeaseOwner{ .Entity = slowed }, 19);
    FixedLogicContext context{ .Config = config, .Runtime = runtime,
                               .Time = FixedSimTime{ .DeltaSeconds = kDt, .TickIndex = 20 },
                               .Entities = world, .Partitions = partitions };
    schedule.RunFixedLogic(context);
    EXPECT_FALSE(world.HasComponent<ActorTimeDilated>(slowed)) << "back in the bulk path";
}
