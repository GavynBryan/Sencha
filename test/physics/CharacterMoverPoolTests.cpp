// CharacterMoverPool bridges CharacterController entities to CharacterMovers in a
// dense pool (the character analogue of PhysicsScene). Tests drive it directly
// with a bare ECS World; no engine frame harness, no Jolt headers.

#include <gtest/gtest.h>

#include <ecs/World.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <world/transform/TransformComponents.h>

namespace
{
constexpr float kFixedDt = 1.0f / 60.0f;
const Vec3d kGravity(0.0f, -9.81f, 0.0f);

void SetUpPhysics(World& world)
{
    world.RegisterComponent<LocalTransform>();
    RegisterPhysicsComponents(world);
}

EntityId SpawnCharacter(World& world, const Vec3d& position)
{
    Transform3f t;
    t.Position = position;
    const EntityId e = world.CreateEntity();
    world.AddComponent<LocalTransform>(e, LocalTransform{ t });
    world.AddComponent<CharacterController>(e, CharacterController{});
    return e;
}

void AddStaticFloor(PhysicsWorld& world)
{
    BodyDesc floor;
    floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f)); // top at y = 0.5
    floor.Motion = BodyMotion::Static;
    floor.Layer = CollisionLayer::Static;
    (void)world.AddBody(floor);
}
} // namespace

TEST(CharacterMoverPool, CreatesLinkAndDrivesMoverOntoFloor)
{
    PhysicsWorld physics;
    AddStaticFloor(physics);

    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId player = SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs);
    EXPECT_EQ(pool.MoverCount(), 1u);
    EXPECT_TRUE(ecs.HasComponent<CharacterMoverLink>(player));

    for (int i = 0; i < 240; ++i)
    {
        pool.Reconcile(ecs);
        pool.Drive(ecs, kFixedDt, kGravity);
    }

    const LocalTransform* rest = ecs.TryGet<LocalTransform>(player);
    ASSERT_NE(rest, nullptr);
    // Floor top 0.5 + capsule half height 0.9 (Height 1.8) = 1.4.
    EXPECT_NEAR(rest->Value.Position.Y, 1.4f, 0.05f);
}

TEST(CharacterMoverPool, ReconcileGateHoldsWhenNothingStructuralChanged)
{
    PhysicsWorld physics;
    AddStaticFloor(physics);

    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs);
    EXPECT_EQ(pool.ReconcilePasses(), 1u);

    for (int i = 0; i < 10; ++i)
    {
        pool.Reconcile(ecs);
        pool.Drive(ecs, kFixedDt, kGravity);
    }
    EXPECT_EQ(pool.ReconcilePasses(), 1u); // driving is data only: gate held
}

TEST(CharacterMoverPool, ReleasesMoverWhenControllerRemoved)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId player = SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs);
    EXPECT_EQ(pool.MoverCount(), 1u);

    ecs.RemoveComponent<CharacterController>(player);
    pool.Reconcile(ecs);
    EXPECT_EQ(pool.MoverCount(), 0u);
    EXPECT_FALSE(ecs.HasComponent<CharacterMoverLink>(player));
}

TEST(CharacterMoverPool, ReleasesMoverWhenEntityDestroyed)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId player = SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs);
    EXPECT_EQ(pool.MoverCount(), 1u);

    ecs.DestroyEntity(player); // no hook fires; the link vanished with the entity
    pool.Reconcile(ecs);
    EXPECT_EQ(pool.MoverCount(), 0u);
}

TEST(CharacterMoverPool, SlotReusedAfterRelease)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId first = SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs);
    const uint32_t firstSlot = ecs.TryGet<CharacterMoverLink>(first)->MoverSlot;

    ecs.DestroyEntity(first);
    pool.Reconcile(ecs);
    EXPECT_EQ(pool.MoverCount(), 0u);

    const EntityId second = SpawnCharacter(ecs, Vec3d(1.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs);
    EXPECT_EQ(pool.MoverCount(), 1u);
    // The freed slot is handed out again rather than growing the pool.
    EXPECT_EQ(ecs.TryGet<CharacterMoverLink>(second)->MoverSlot, firstSlot);
}
