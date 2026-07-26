// The physics residency contract: dormant means zero backend presence. A
// partition leaving the physics domain evicts its bodies and movers from the
// shared simulation (no contacts, no solver cost), state is captured into
// components, and returning restores through the ordinary reconcile with no
// dedicated path. Headless; no Jolt headers.

#include <gtest/gtest.h>

#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

#include <span>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;
constexpr StoragePartitionId kFloorPartition{ 1 };
constexpr StoragePartitionId kBallPartition{ 2 };

void SetUpPhysics(World& world)
{
    world.RegisterComponent<LocalTransform>();
    RegisterPhysicsComponents(world);
}

const StoragePartitionSet& DefaultPartitions()
{
    static const StoragePartitionSet partitions = []
    {
        StoragePartitionSet value;
        value.Add(StoragePartitionId::Default());
        return value;
    }();
    return partitions;
}

EntityId SpawnAt(World& world, const Vec3d& position,
                 StoragePartitionId partition = StoragePartitionId::Default())
{
    Transform3f t;
    t.Position = position;
    const EntityId e = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(e, LocalTransform{ t });
    return e;
}

EntityId SpawnDynamicBall(World& world, const Vec3d& position,
                          StoragePartitionId partition = StoragePartitionId::Default())
{
    const EntityId e = SpawnAt(world, position, partition);
    world.AddComponent<Collider>(e, Collider{ CollisionShape::MakeSphere(0.5f) });
    world.AddComponent<RigidBody>(e, RigidBody{ BodyMotion::Dynamic, 1.0f, Vec3d::Zero(), 1.0f });
    return e;
}

void Tick(RigidBodyBinding& binding, World& ecs, PhysicsWorld& physics, int steps,
          const StoragePartitionSet& partitions = DefaultPartitions())
{
    for (int i = 0; i < steps; ++i)
    {
        binding.SyncToPhysics(ecs, partitions);
        physics.Step(kFixedDt);
        binding.SyncFromPhysics(ecs, partitions);
    }
}
} // namespace

TEST(PhysicsResidency, EvictRemovesBodiesAndCapturesState)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId ball = SpawnDynamicBall(ecs, Vec3d(0.0f, 50.0f, 0.0f));
    Tick(binding, ecs, physics, 30); // fall a while

    binding.EvictAll(ecs);

    EXPECT_EQ(physics.BodyCount(), 0u);
    EXPECT_EQ(binding.BodyCount(), 0u);
    EXPECT_FALSE(ecs.HasComponent<PhysicsBodyLink>(ball));

    // State was captured: the component carries the fall, frozen.
    const RigidBody* body = ecs.TryGet<RigidBody>(ball);
    ASSERT_NE(body, nullptr);
    EXPECT_LT(body->LinearVelocity.Y, -1.0f);
    EXPECT_LT(ecs.TryGet<LocalTransform>(ball)->Value.Position.Y, 50.0f);
}

TEST(PhysicsResidency, ReconcileRestoresEvictedBodiesFaithfully)
{
    // Free fall has no solver-internal state, so an evict/restore mid-fall
    // must continue on the uninterrupted trajectory.
    auto run = [](bool interrupt, Vec3d& out)
    {
        PhysicsWorld physics;
        World ecs;
        SetUpPhysics(ecs);
        RigidBodyBinding binding(physics);

        const EntityId ball = SpawnDynamicBall(ecs, Vec3d(0.0f, 100.0f, 0.0f));
        Tick(binding, ecs, physics, 60);

        if (interrupt)
        {
            binding.EvictAll(ecs);
            EXPECT_EQ(physics.BodyCount(), 0u);
            // The next sync's reconcile is the restore path.
        }

        Tick(binding, ecs, physics, 60);
        out = ecs.TryGet<LocalTransform>(ball)->Value.Position;
    };

    Vec3d uninterrupted;
    Vec3d resumed;
    run(false, uninterrupted);
    run(true, resumed);

    EXPECT_NEAR(resumed.Y, uninterrupted.Y, 0.01f);
}

TEST(PhysicsResidency, DormantGeometryStopsColliding)
{
    // Two zone partitions in one World, one simulation: a ball rests on a
    // dormant-able zone's floor. Evicting the floor's partition must drop the
    // ball — "cannot affect simulation" includes being stood on.
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId floor = SpawnAt(ecs, Vec3d(0.0f, 0.0f, 0.0f), kFloorPartition);
    ecs.AddComponent<Collider>(
        floor, Collider{ CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f)) });

    const EntityId ball =
        SpawnDynamicBall(ecs, Vec3d(0.0f, 2.0f, 0.0f), kBallPartition);

    StoragePartitionSet bothZones;
    bothZones.Add(kFloorPartition);
    bothZones.Add(kBallPartition);

    Tick(binding, ecs, physics, 240, bothZones);
    const float restY = ecs.TryGet<LocalTransform>(ball)->Value.Position.Y;
    EXPECT_NEAR(restY, 1.0f, 0.05f); // resting on the floor

    binding.EvictPartition(ecs, kFloorPartition); // the floor's zone goes dormant

    StoragePartitionSet ballOnly;
    ballOnly.Add(kBallPartition);
    Tick(binding, ecs, physics, 60, ballOnly);

    EXPECT_LT(ecs.TryGet<LocalTransform>(ball)->Value.Position.Y, restY - 1.0f);
}

TEST(PhysicsResidency, MoverPoolEvictReleasesMoversAndReconcileRestores)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    Transform3f t;
    t.Position = Vec3d(0.0f, 5.0f, 0.0f);
    const EntityId player = ecs.CreateEntity();
    ecs.AddComponent<LocalTransform>(player, LocalTransform{ t });
    ecs.AddComponent<CharacterController>(player, CharacterController{});

    pool.Reconcile(ecs, DefaultPartitions());
    EXPECT_EQ(pool.MoverCount(), 1u);

    pool.EvictAll(ecs);
    EXPECT_EQ(pool.MoverCount(), 0u);
    EXPECT_FALSE(ecs.HasComponent<CharacterMoverLink>(player));

    pool.Reconcile(ecs, DefaultPartitions()); // return: the ordinary reconcile is the restore
    EXPECT_EQ(pool.MoverCount(), 1u);
    EXPECT_TRUE(ecs.HasComponent<CharacterMoverLink>(player));
}

TEST(PhysicsResidency, StepSystemEvictsOnPhysicsDomainExit)
{
    PhysicsStepSystem step;
    EngineConfig config;

    World ecs;
    SetUpPhysics(ecs);
    SpawnDynamicBall(ecs, Vec3d(0.0f, 5.0f, 0.0f), kBallPartition);

    StoragePartitionSet zone;
    zone.Add(kBallPartition);
    step.GetRigidBodies().SyncToPhysics(ecs, zone);
    ASSERT_EQ(step.GetSimulation().BodyCount(), 1u);

    // The zone leaves the physics domain (dormancy — and Detaching arrives
    // with the same Previous/Current shape).
    ZoneParticipation previous;
    previous.Physics = true;
    const ZoneResidencyChange change{
        ZoneResidencyChangeKind::ParticipationChanged,
        ZoneId{ 1 },
        kBallPartition,
        previous,
        ZoneParticipation{},
    };
    ZoneResidencyContext ctx{
        config, ecs, std::span<const ZoneResidencyChange>{ &change, 1 } };
    step.ZoneResidency(ctx);

    EXPECT_EQ(step.GetSimulation().BodyCount(), 0u);
    EXPECT_EQ(step.GetRigidBodies().BodyCount(), 0u);
}

TEST(PhysicsResidency, EvictRestoreCyclesAreStable)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    SpawnDynamicBall(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    ecs.TryGet<RigidBody>(ecs.GetAliveEntities()[0])->GravityScale = 0.0f;

    for (int cycle = 0; cycle < 10; ++cycle)
    {
        Tick(binding, ecs, physics, 5);
        EXPECT_EQ(physics.BodyCount(), 1u);
        binding.EvictAll(ecs);
        EXPECT_EQ(physics.BodyCount(), 0u);
    }

    Tick(binding, ecs, physics, 1);
    EXPECT_EQ(physics.BodyCount(), 1u); // exactly one body, no duplicates
    EXPECT_EQ(binding.BodyCount(), 1u);
}
