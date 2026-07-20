// The physics residency contract: dormant means zero backend presence. A
// registry leaving the physics domain evicts its bodies and movers from the
// shared simulation (no contacts, no solver cost), state is captured into
// components, and returning restores through the ordinary reconcile with no
// dedicated path. Headless; no Jolt headers.

#include <gtest/gtest.h>

#include <app/GameContexts.h>
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
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;

void SetUpPhysics(World& world)
{
    world.RegisterComponent<LocalTransform>();
    RegisterPhysicsComponents(world);
}

EntityId SpawnAt(World& world, const Vec3d& position)
{
    Transform3f t;
    t.Position = position;
    const EntityId e = world.CreateEntity();
    world.AddComponent<LocalTransform>(e, LocalTransform{ t });
    return e;
}

EntityId SpawnDynamicBall(World& world, const Vec3d& position)
{
    const EntityId e = SpawnAt(world, position);
    world.AddComponent<Collider>(e, Collider{ CollisionShape::MakeSphere(0.5f) });
    world.AddComponent<RigidBody>(e, RigidBody{ BodyMotion::Dynamic, 1.0f, Vec3d::Zero(), 1.0f });
    return e;
}

void Tick(RigidBodyBinding& binding, World& ecs, PhysicsWorld& physics, int steps)
{
    for (int i = 0; i < steps; ++i)
    {
        binding.SyncToPhysics(ecs);
        physics.Step(kFixedDt);
        binding.SyncFromPhysics(ecs);
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

    binding.Evict(ecs);

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
            binding.Evict(ecs);
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
    // Two registries sharing one simulation: a ball rests on a dormant-able
    // zone's floor. Evicting the floor's registry must drop the ball —
    // "cannot affect simulation" includes being stood on.
    PhysicsWorld physics;

    World floorZone;
    SetUpPhysics(floorZone);
    RigidBodyBinding floorBinding(physics);
    const EntityId floor = SpawnAt(floorZone, Vec3d(0.0f, 0.0f, 0.0f));
    floorZone.AddComponent<Collider>(
        floor, Collider{ CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f)) });

    World ballZone;
    SetUpPhysics(ballZone);
    RigidBodyBinding ballBinding(physics);
    const EntityId ball = SpawnDynamicBall(ballZone, Vec3d(0.0f, 2.0f, 0.0f));

    for (int i = 0; i < 240; ++i)
    {
        floorBinding.SyncToPhysics(floorZone);
        ballBinding.SyncToPhysics(ballZone);
        physics.Step(kFixedDt);
        ballBinding.SyncFromPhysics(ballZone);
    }
    const float restY = ballZone.TryGet<LocalTransform>(ball)->Value.Position.Y;
    EXPECT_NEAR(restY, 1.0f, 0.05f); // resting on the floor

    floorBinding.Evict(floorZone); // the floor's zone goes dormant

    for (int i = 0; i < 60; ++i)
    {
        ballBinding.SyncToPhysics(ballZone);
        physics.Step(kFixedDt);
        ballBinding.SyncFromPhysics(ballZone);
    }

    EXPECT_LT(ballZone.TryGet<LocalTransform>(ball)->Value.Position.Y, restY - 1.0f);
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

    pool.Reconcile(ecs);
    EXPECT_EQ(pool.MoverCount(), 1u);

    pool.Evict(ecs);
    EXPECT_EQ(pool.MoverCount(), 0u);
    EXPECT_FALSE(ecs.HasComponent<CharacterMoverLink>(player));

    pool.Reconcile(ecs); // return: the ordinary reconcile is the restore
    EXPECT_EQ(pool.MoverCount(), 1u);
    EXPECT_TRUE(ecs.HasComponent<CharacterMoverLink>(player));
}

TEST(PhysicsResidency, StepSystemEvictsOnPhysicsDomainExit)
{
    PhysicsStepSystem step;
    EngineConfig config;

    Registry registry = MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{ 1 });
    SetUpPhysics(registry.Components);
    SpawnDynamicBall(registry.Components, Vec3d(0.0f, 5.0f, 0.0f));

    RigidBodyBinding& binding =
        registry.Resources.Ensure<RigidBodyBinding>(step.GetSimulation());
    binding.SyncToPhysics(registry.Components);
    ASSERT_EQ(step.GetSimulation().BodyCount(), 1u);

    // The zone leaves the physics domain (dormancy — and Detaching arrives
    // with the same Previous/Current shape).
    ZoneParticipation previous;
    previous.Physics = true;
    RegistryResidencyChange change{
        RegistryResidencyChangeKind::ParticipationChanged,
        registry.Id,
        &registry,
        previous,
        ZoneParticipation{},
    };
    RegistryResidencyContext ctx{ config, std::span<const RegistryResidencyChange>{ &change, 1 } };
    step.RegistryResidency(ctx);

    EXPECT_EQ(step.GetSimulation().BodyCount(), 0u);
    EXPECT_EQ(binding.BodyCount(), 0u);
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
        binding.Evict(ecs);
        EXPECT_EQ(physics.BodyCount(), 0u);
    }

    Tick(binding, ecs, physics, 1);
    EXPECT_EQ(physics.BodyCount(), 1u); // exactly one body, no duplicates
    EXPECT_EQ(binding.BodyCount(), 1u);
}
