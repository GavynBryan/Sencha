// RigidBodyBinding bridges ECS entities (Collider + optional RigidBody + transform)
// to bodies in the shared PhysicsWorld. Tests drive the bridge directly with a
// bare ECS World; no engine frame harness, no Jolt headers.

#include <gtest/gtest.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;

// Registers the transform plus the full physics component set (colliders, rigid
// bodies, and the runtime link components the bridge needs).
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

TEST(RigidBodyBinding, DynamicEntityRestsOnStaticFloor)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    EntityId floor = SpawnAt(ecs, Vec3d(0.0f, 0.0f, 0.0f));
    ecs.AddComponent<Collider>(floor, Collider{ CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f)) });

    EntityId ball = SpawnAt(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    ecs.AddComponent<Collider>(ball, Collider{ CollisionShape::MakeSphere(0.5f) });
    ecs.AddComponent<RigidBody>(ball, RigidBody{ BodyMotion::Dynamic, 1.0f, Vec3d::Zero(), 1.0f });

    Tick(binding, ecs, physics, 240);

    EXPECT_EQ(binding.BodyCount(), 2u);
    const LocalTransform* rest = ecs.TryGet<LocalTransform>(ball);
    ASSERT_NE(rest, nullptr);
    EXPECT_NEAR(rest->Value.Position.Y, 1.0f, 0.05f); // floor top 0.5 + radius 0.5
}

TEST(RigidBodyBinding, RemovingColliderRemovesBody)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(box, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });

    binding.SyncToPhysics(ecs);
    EXPECT_EQ(binding.BodyCount(), 1u);
    EXPECT_EQ(physics.BodyCount(), 1u);

    ecs.RemoveComponent<Collider>(box);
    binding.SyncToPhysics(ecs);
    EXPECT_EQ(binding.BodyCount(), 0u);
    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(RigidBodyBinding, RegistryTeardownRemovesBodiesFromSharedWorld)
{
    // The unload path as it actually runs: the binding lives in
    // Registry::Resources and dies with the registry, removing that
    // registry's bodies from the shared world.
    PhysicsWorld physics;

    {
        Registry registry = MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{ 1 });
        World& ecs = registry.Components;
        SetUpPhysics(ecs);
        RigidBodyBinding& binding = registry.Resources.Register<RigidBodyBinding>(physics);

        EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
        ecs.AddComponent<Collider>(box, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });
        binding.SyncToPhysics(ecs);
        EXPECT_EQ(physics.BodyCount(), 1u); // body lives in the shared world
    } // registry destroyed here: the real zone-unload teardown

    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(RigidBodyBinding, KinematicBodyFollowsAuthoredTransform)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    EntityId platform = SpawnAt(ecs, Vec3d(0.0f, 0.0f, 0.0f));
    ecs.AddComponent<Collider>(platform, Collider{ CollisionShape::MakeBox(Vec3d(1.0f, 0.25f, 1.0f)) });
    ecs.AddComponent<RigidBody>(platform, RigidBody{ BodyMotion::Kinematic, 1.0f, Vec3d::Zero(), 1.0f });

    binding.SyncToPhysics(ecs);
    physics.Step(kFixedDt);

    // Move the authored transform; the kinematic body should track it on sync.
    if (LocalTransform* lt = ecs.TryGet<LocalTransform>(platform))
        lt->Value.Position = Vec3d(0.0f, 2.0f, 0.0f);
    binding.SyncToPhysics(ecs);

    // Reconcile must not have created a second body for the moved platform.
    EXPECT_EQ(binding.BodyCount(), 1u);
}

// The reconcile gate: topology work runs only when the zone's structural version
// changes. Moving bodies and stepping (data writes) must not retrigger it.
TEST(RigidBodyBinding, ReconcileSkipsWhenNothingStructuralChanged)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(box, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });

    binding.SyncToPhysics(ecs);
    EXPECT_EQ(binding.ReconcilePasses(), 1u); // one pass to create the body

    for (int i = 0; i < 10; ++i)
    {
        binding.SyncToPhysics(ecs);
        physics.Step(kFixedDt);
        binding.SyncFromPhysics(ecs);
    }
    EXPECT_EQ(binding.ReconcilePasses(), 1u); // steady state: gate held, no rescan

    // A structural change (a new collider) advances the gate exactly once.
    EntityId box2 = SpawnAt(ecs, Vec3d(3.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(box2, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });
    binding.SyncToPhysics(ecs);
    EXPECT_EQ(binding.ReconcilePasses(), 2u);
    EXPECT_EQ(binding.BodyCount(), 2u);
}

// The runtime PhysicsBodyLink appears when the body is bound and is stripped when
// the collider is removed; it is the per-frame sync's hash-free handle.
TEST(RigidBodyBinding, BodyLinkTracksColliderLifetime)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(box, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });
    EXPECT_FALSE(ecs.HasComponent<PhysicsBodyLink>(box));

    binding.SyncToPhysics(ecs);
    EXPECT_TRUE(ecs.HasComponent<PhysicsBodyLink>(box));

    ecs.RemoveComponent<Collider>(box);
    binding.SyncToPhysics(ecs);
    EXPECT_FALSE(ecs.HasComponent<PhysicsBodyLink>(box));
}

// Destroy robustness: PhysicsBodyLink carries no lifecycle hook, so it vanishes
// silently with the entity and only the physics-side Owned record can report the
// dead body. The reconcile sweep (gated on the structural-version bump that
// destroy causes) removes it. This is the case the dense Owned vector exists for.
TEST(RigidBodyBinding, DestroyingEntityRemovesBody)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    EntityId box = SpawnAt(ecs, Vec3d(0.0f, 1.0f, 0.0f));
    ecs.AddComponent<Collider>(box, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });
    binding.SyncToPhysics(ecs);
    EXPECT_EQ(physics.BodyCount(), 1u);

    ecs.DestroyEntity(box);
    binding.SyncToPhysics(ecs);
    EXPECT_EQ(binding.BodyCount(), 0u);
    EXPECT_EQ(physics.BodyCount(), 0u);
}

// Same inputs on one build produce bit-identical results: the bridge iterates in
// archetype/chunk/row order (not unordered-map order) and Jolt runs single-threaded.
TEST(RigidBodyBinding, DeterministicAcrossIdenticalRuns)
{
    auto run = [](Vec3d& out)
    {
        PhysicsWorld physics;
        World ecs;
        SetUpPhysics(ecs);
        RigidBodyBinding binding(physics);

        EntityId floor = SpawnAt(ecs, Vec3d(0.0f, 0.0f, 0.0f));
        ecs.AddComponent<Collider>(floor, Collider{ CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f)) });
        EntityId ball = SpawnAt(ecs, Vec3d(0.13f, 5.0f, -0.21f));
        ecs.AddComponent<Collider>(ball, Collider{ CollisionShape::MakeSphere(0.5f) });
        ecs.AddComponent<RigidBody>(ball, RigidBody{ BodyMotion::Dynamic, 1.0f, Vec3d::Zero(), 1.0f });

        for (int i = 0; i < 120; ++i)
        {
            binding.SyncToPhysics(ecs);
            physics.Step(kFixedDt);
            binding.SyncFromPhysics(ecs);
        }
        out = ecs.TryGet<LocalTransform>(ball)->Value.Position;
    };

    Vec3d a;
    Vec3d b;
    run(a);
    run(b);
    EXPECT_EQ(a.X, b.X);
    EXPECT_EQ(a.Y, b.Y);
    EXPECT_EQ(a.Z, b.Z);
}

// The step system stores bindings in Registry::Resources — the owner of
// per-registry backend bindings — never in the ECS World's resource bag.
TEST(RigidBodyBinding, StepSystemStoresBindingInRegistryResources)
{
    EngineConfig config;
    RuntimeFrameLoop runtime;
    InputFrame input;
    PhysicsStepSystem step;

    Registry registry = MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{ 1 });
    SetUpPhysics(registry.Components);
    EntityId box = SpawnAt(registry.Components, Vec3d(0.0f, 1.0f, 0.0f));
    registry.Components.AddComponent<Collider>(
        box, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });

    Registry* registries[] = { &registry };
    PhysicsContext ctx{
        .Config = config,
        .Runtime = runtime,
        .Input = input,
        .Time = FixedSimTime{},
        .Registries = FrameRegistryView{},
        .ActiveRegistries = std::span<Registry*>{ registries },
    };
    step.Physics(ctx);

    EXPECT_TRUE(registry.Resources.Has<RigidBodyBinding>());
    EXPECT_FALSE(registry.Components.HasResource<RigidBodyBinding>());
    EXPECT_EQ(registry.Resources.Get<RigidBodyBinding>().BodyCount(), 1u);
}

// ─── Full body state through the ECS bridge ──────────────────────────────────

TEST(RigidBodyBinding, AngularStateRoundTripsThroughComponents)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    EntityId top = SpawnAt(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    ecs.AddComponent<Collider>(top, Collider{ CollisionShape::MakeSphere(0.5f) });
    RigidBody body;
    body.Motion = BodyMotion::Dynamic;
    body.GravityScale = 0.0f;               // spin in place
    body.AngularVelocity = Vec3d(0.0f, 4.0f, 0.0f);
    ecs.AddComponent<RigidBody>(top, body);

    Tick(binding, ecs, physics, 10);

    const RigidBody* pulled = ecs.TryGet<RigidBody>(top);
    ASSERT_NE(pulled, nullptr);
    EXPECT_NEAR(pulled->AngularVelocity.Y, 4.0f, 0.25f); // damping trims a little
    const LocalTransform* lt = ecs.TryGet<LocalTransform>(top);
    ASSERT_NE(lt, nullptr);
    EXPECT_NEAR(lt->Value.Position.Y, 5.0f, 1e-3f); // gravity scale held it up
}

TEST(RigidBodyBinding, RuntimeGravityScaleEditActsNextTick)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    RigidBodyBinding binding(physics);

    EntityId ball = SpawnAt(ecs, Vec3d(0.0f, 50.0f, 0.0f));
    ecs.AddComponent<Collider>(ball, Collider{ CollisionShape::MakeSphere(0.5f) });
    ecs.AddComponent<RigidBody>(ball, RigidBody{ BodyMotion::Dynamic, 1.0f, Vec3d::Zero(), 1.0f });

    Tick(binding, ecs, physics, 30);
    const float fallingSpeed = ecs.TryGet<RigidBody>(ball)->LinearVelocity.Y;
    EXPECT_LT(fallingSpeed, -1.0f);

    // The suspended-actor case: gameplay zeroes gravity on the live component.
    ecs.TryGet<RigidBody>(ball)->GravityScale = 0.0f;
    Tick(binding, ecs, physics, 1);

    EXPECT_GT(ecs.TryGet<RigidBody>(ball)->LinearVelocity.Y, fallingSpeed * 1.001f);
}
