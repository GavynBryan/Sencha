#include <gtest/gtest.h>

#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/transform/TransformComponents.h>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;

const StoragePartitionSet& ActivePartitions()
{
    static const StoragePartitionSet partitions = []
    {
        StoragePartitionSet value;
        value.Add(StoragePartitionId::Default());
        return value;
    }();
    return partitions;
}

void SetUpPhysics(World& world)
{
    world.RegisterComponent<LocalTransform>();
    RegisterPhysicsComponents(world);
}

EntityId SpawnAt(World& world, const Vec3d& position)
{
    Transform3f transform;
    transform.Position = position;
    const EntityId entity = world.CreateEntity();
    world.AddComponent<LocalTransform>(
        entity,
        LocalTransform{ transform });
    return entity;
}

void Tick(
    RigidBodyBinding& binding,
    World& ecs,
    PhysicsWorld& physics,
    int steps)
{
    for (int index = 0; index < steps; ++index)
    {
        binding.SyncToPhysics(ecs, ActivePartitions());
        physics.Step(kFixedDt);
        binding.SyncFromPhysics(ecs, ActivePartitions());
    }
}
} // namespace

TEST(RigidBodyBinding, DynamicEntityRestsOnStaticFloor)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId floor = SpawnAt(ecs, Vec3d::Zero());
    ecs.AddComponent<Collider>(
        floor,
        Collider{ CollisionShape::MakeBox(
            Vec3d(50.0f, 0.5f, 50.0f)) });

    const EntityId ball = SpawnAt(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    ecs.AddComponent<Collider>(
        ball,
        Collider{ CollisionShape::MakeSphere(0.5f) });
    ecs.AddComponent<RigidBody>(
        ball,
        RigidBody{
            BodyMotion::Dynamic,
            1.0f,
            Vec3d::Zero(),
            1.0f,
        });

    Tick(binding, ecs, physics, 240);

    EXPECT_EQ(binding.BodyCount(), 2u);
    const LocalTransform* rest = ecs.TryGet<LocalTransform>(ball);
    ASSERT_NE(rest, nullptr);
    EXPECT_NEAR(rest->Value.Position.Y, 1.0f, 0.05f);
}

TEST(RigidBodyBinding, RemovingColliderRemovesBody)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(
        box,
        Collider{ CollisionShape::MakeBox(
            Vec3d(0.5f, 0.5f, 0.5f)) });

    binding.SyncToPhysics(ecs, ActivePartitions());
    EXPECT_EQ(binding.BodyCount(), 1u);
    EXPECT_EQ(physics.BodyCount(), 1u);

    ecs.RemoveComponent<Collider>(box);
    binding.SyncToPhysics(ecs, ActivePartitions());
    EXPECT_EQ(binding.BodyCount(), 0u);
    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(RigidBodyBinding, BindingTeardownRemovesBodiesFromSharedWorld)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);

    {
        RigidBodyBinding binding(physics);
        const EntityId box =
            SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
        ecs.AddComponent<Collider>(
            box,
            Collider{ CollisionShape::MakeBox(
                Vec3d(0.5f, 0.5f, 0.5f)) });
        binding.SyncToPhysics(ecs, ActivePartitions());
        EXPECT_EQ(physics.BodyCount(), 1u);
    }

    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(RigidBodyBinding, KinematicBodyFollowsAuthoredTransform)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId platform = SpawnAt(ecs, Vec3d::Zero());
    ecs.AddComponent<Collider>(
        platform,
        Collider{ CollisionShape::MakeBox(
            Vec3d(1.0f, 0.25f, 1.0f)) });
    ecs.AddComponent<RigidBody>(
        platform,
        RigidBody{
            BodyMotion::Kinematic,
            1.0f,
            Vec3d::Zero(),
            1.0f,
        });

    binding.SyncToPhysics(ecs, ActivePartitions());
    physics.Step(kFixedDt);

    ecs.TryGet<LocalTransform>(platform)->Value.Position =
        Vec3d(0.0f, 2.0f, 0.0f);
    binding.SyncToPhysics(ecs, ActivePartitions());
    EXPECT_EQ(binding.BodyCount(), 1u);
}

TEST(RigidBodyBinding, ReconcileSkipsWhenNothingStructuralChanged)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(
        box,
        Collider{ CollisionShape::MakeBox(
            Vec3d(0.5f, 0.5f, 0.5f)) });

    binding.SyncToPhysics(ecs, ActivePartitions());
    EXPECT_EQ(binding.ReconcilePasses(), 1u);

    for (int index = 0; index < 10; ++index)
    {
        binding.SyncToPhysics(ecs, ActivePartitions());
        physics.Step(kFixedDt);
        binding.SyncFromPhysics(ecs, ActivePartitions());
    }
    EXPECT_EQ(binding.ReconcilePasses(), 1u);

    const EntityId second =
        SpawnAt(ecs, Vec3d(3.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(
        second,
        Collider{ CollisionShape::MakeBox(
            Vec3d(0.5f, 0.5f, 0.5f)) });
    binding.SyncToPhysics(ecs, ActivePartitions());
    EXPECT_EQ(binding.ReconcilePasses(), 2u);
    EXPECT_EQ(binding.BodyCount(), 2u);
}

TEST(RigidBodyBinding, BodyLinkTracksColliderLifetime)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(
        box,
        Collider{ CollisionShape::MakeBox(
            Vec3d(0.5f, 0.5f, 0.5f)) });
    EXPECT_FALSE(ecs.HasComponent<PhysicsBodyLink>(box));

    binding.SyncToPhysics(ecs, ActivePartitions());
    EXPECT_TRUE(ecs.HasComponent<PhysicsBodyLink>(box));

    ecs.RemoveComponent<Collider>(box);
    binding.SyncToPhysics(ecs, ActivePartitions());
    EXPECT_FALSE(ecs.HasComponent<PhysicsBodyLink>(box));
}

TEST(RigidBodyBinding, DestroyingEntityRemovesBody)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(
        box,
        Collider{ CollisionShape::MakeBox(
            Vec3d(0.5f, 0.5f, 0.5f)) });
    binding.SyncToPhysics(ecs, ActivePartitions());
    EXPECT_EQ(physics.BodyCount(), 1u);

    ecs.DestroyEntity(box);
    binding.SyncToPhysics(ecs, ActivePartitions());
    EXPECT_EQ(binding.BodyCount(), 0u);
    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(RigidBodyBinding, DeterministicAcrossIdenticalRuns)
{
    const auto run = [](Vec3d& output)
    {
        PhysicsWorld physics;
        World ecs;
        SetUpPhysics(ecs);
        RigidBodyBinding binding(physics);

        const EntityId floor = SpawnAt(ecs, Vec3d::Zero());
        ecs.AddComponent<Collider>(
            floor,
            Collider{ CollisionShape::MakeBox(
                Vec3d(50.0f, 0.5f, 50.0f)) });
        const EntityId ball =
            SpawnAt(ecs, Vec3d(0.13f, 5.0f, -0.21f));
        ecs.AddComponent<Collider>(
            ball,
            Collider{ CollisionShape::MakeSphere(0.5f) });
        ecs.AddComponent<RigidBody>(
            ball,
            RigidBody{
                BodyMotion::Dynamic,
                1.0f,
                Vec3d::Zero(),
                1.0f,
            });

        Tick(binding, ecs, physics, 120);
        output = ecs.TryGet<LocalTransform>(ball)->Value.Position;
    };

    Vec3d first;
    Vec3d second;
    run(first);
    run(second);
    EXPECT_EQ(first.X, second.X);
    EXPECT_EQ(first.Y, second.Y);
    EXPECT_EQ(first.Z, second.Z);
}

TEST(RigidBodyBinding, StepSystemOwnsSingleSimulationBinding)
{
    EngineConfig config;
    RuntimeFrameLoop runtime;
    InputFrame input;
    PhysicsStepSystem step;

    World world;
    SetUpPhysics(world);
    const EntityId box = SpawnAt(world, Vec3d(0.0f, 1.0f, 0.0f));
    world.AddComponent<Collider>(
        box,
        Collider{ CollisionShape::MakeBox(
            Vec3d(0.5f, 0.5f, 0.5f)) });

    PhysicsContext context{
        .Config = config,
        .Runtime = runtime,
        .Input = input,
        .Time = FixedSimTime{},
        .Entities = world,
        .Partitions = ActivePartitions(),
    };
    step.Physics(context);

    EXPECT_EQ(step.GetRigidBodies().BodyCount(), 1u);
    EXPECT_FALSE(world.HasResource<RigidBodyBinding>());
}

TEST(RigidBodyBinding, AngularStateRoundTripsThroughComponents)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId top = SpawnAt(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    ecs.AddComponent<Collider>(
        top,
        Collider{ CollisionShape::MakeSphere(0.5f) });
    RigidBody body;
    body.Motion = BodyMotion::Dynamic;
    body.GravityScale = 0.0f;
    body.AngularVelocity = Vec3d(0.0f, 4.0f, 0.0f);
    ecs.AddComponent<RigidBody>(top, body);

    Tick(binding, ecs, physics, 10);

    const RigidBody* pulled = ecs.TryGet<RigidBody>(top);
    ASSERT_NE(pulled, nullptr);
    EXPECT_NEAR(pulled->AngularVelocity.Y, 4.0f, 0.25f);
    const LocalTransform* transform =
        ecs.TryGet<LocalTransform>(top);
    ASSERT_NE(transform, nullptr);
    EXPECT_NEAR(transform->Value.Position.Y, 5.0f, 1e-3f);
}

TEST(RigidBodyBinding, RuntimeGravityScaleEditActsNextTick)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    const EntityId ball = SpawnAt(ecs, Vec3d(0.0f, 50.0f, 0.0f));
    ecs.AddComponent<Collider>(
        ball,
        Collider{ CollisionShape::MakeSphere(0.5f) });
    ecs.AddComponent<RigidBody>(
        ball,
        RigidBody{
            BodyMotion::Dynamic,
            1.0f,
            Vec3d::Zero(),
            1.0f,
        });

    Tick(binding, ecs, physics, 30);
    const float fallingSpeed =
        ecs.TryGet<RigidBody>(ball)->LinearVelocity.Y;
    EXPECT_LT(fallingSpeed, -1.0f);

    ecs.TryGet<RigidBody>(ball)->GravityScale = 0.0f;
    Tick(binding, ecs, physics, 1);

    EXPECT_GT(
        ecs.TryGet<RigidBody>(ball)->LinearVelocity.Y,
        fallingSpeed * 1.001f);
}
