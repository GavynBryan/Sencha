#include <gtest/gtest.h>

#include <algorithm>

#include <ecs/StoragePartitionSet.h>
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

EntityId SpawnCharacterIn(
    World& world,
    StoragePartitionId partition,
    const Vec3d& position)
{
    Transform3f transform;
    transform.Position = position;
    const EntityId entity = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(
        entity,
        LocalTransform{ transform });
    world.AddComponent<CharacterController>(
        entity,
        CharacterController{});
    return entity;
}

EntityId SpawnCharacter(World& world, const Vec3d& position)
{
    return SpawnCharacterIn(
        world,
        StoragePartitionId::Default(),
        position);
}

void AddStaticFloor(PhysicsWorld& world)
{
    BodyDesc floor;
    floor.Shape = CollisionShape::MakeBox(
        Vec3d(50.0f, 0.5f, 50.0f));
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

    const EntityId player =
        SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    EXPECT_EQ(pool.MoverCount(), 1u);
    EXPECT_TRUE(ecs.HasComponent<CharacterMoverLink>(player));

    for (int index = 0; index < 240; ++index)
    {
        pool.Reconcile(ecs, ActivePartitions());
        pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
    }

    const LocalTransform* rest = ecs.TryGet<LocalTransform>(player);
    ASSERT_NE(rest, nullptr);
    EXPECT_NEAR(rest->Value.Position.Y, 1.4f, 0.05f);
}

TEST(CharacterMoverPool, PendingJumpSpeedLaunchesMoverUpward)
{
    PhysicsWorld physics;
    AddStaticFloor(physics);

    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId player =
        SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));

    for (int index = 0; index < 240; ++index)
    {
        pool.Reconcile(ecs, ActivePartitions());
        pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
    }
    const float restY =
        ecs.TryGet<LocalTransform>(player)->Value.Position.Y;
    ASSERT_TRUE(
        ecs.TryGet<CharacterController>(player)->Grounded);

    ecs.TryGet<CharacterController>(player)->PendingJumpSpeed = 5.0f;
    pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
    EXPECT_FLOAT_EQ(
        ecs.TryGet<CharacterController>(player)->PendingJumpSpeed,
        0.0f);

    float peakY = restY;
    for (int index = 0; index < 30; ++index)
    {
        pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
        peakY = std::max(
            peakY,
            ecs.TryGet<LocalTransform>(player)->Value.Position.Y);
    }
    EXPECT_GT(peakY, restY + 0.1f);
}

TEST(CharacterMoverPool, ReconcileGateHoldsWhenNothingStructuralChanged)
{
    PhysicsWorld physics;
    AddStaticFloor(physics);

    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    EXPECT_EQ(pool.ReconcilePasses(), 1u);

    for (int index = 0; index < 10; ++index)
    {
        pool.Reconcile(ecs, ActivePartitions());
        pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
    }
    EXPECT_EQ(pool.ReconcilePasses(), 1u);
}

// Movers exist only for controllers in the active set, so reconciliation is
// gated on that set's structural version rather than the world's.
TEST(CharacterMoverPool, ChurnOutsideTheActiveSetDoesNotReconcile)
{
    constexpr StoragePartitionId kDormant{ 1 };

    PhysicsWorld physics;
    AddStaticFloor(physics);

    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    ASSERT_EQ(pool.ReconcilePasses(), 1u);
    ASSERT_EQ(pool.MoverCount(), 1u);

    const EntityId sleeper =
        SpawnCharacterIn(ecs, kDormant, Vec3d(20.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    EXPECT_EQ(pool.ReconcilePasses(), 1u)
        << "a spawn outside the active set must not rescan the world";
    EXPECT_EQ(pool.MoverCount(), 1u);
    EXPECT_FALSE(ecs.HasComponent<CharacterMoverLink>(sleeper));

    // The inverse: the controller was skipped, not lost.
    StoragePartitionSet widened = ActivePartitions();
    widened.Add(kDormant);
    pool.Reconcile(ecs, widened);
    EXPECT_EQ(pool.ReconcilePasses(), 2u);
    EXPECT_EQ(pool.MoverCount(), 2u);
    EXPECT_TRUE(ecs.HasComponent<CharacterMoverLink>(sleeper));
}

TEST(CharacterMoverPool, SetPositionUpdatesMoverAndTransformTogether)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId player = SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    const Vec3d thresholdClamp{ -0.35f, 2.0f, 1.0f };
    ASSERT_TRUE(pool.SetPosition(ecs, player, thresholdClamp));
    EXPECT_EQ(ecs.TryGet<LocalTransform>(player)->Value.Position, thresholdClamp);

    pool.Drive(ecs, ActivePartitions(), 0.0f, Vec3d::Zero());
    EXPECT_EQ(ecs.TryGet<LocalTransform>(player)->Value.Position, thresholdClamp);
}

TEST(CharacterMoverPool, ReleasesMoverWhenControllerRemoved)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId player =
        SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    EXPECT_EQ(pool.MoverCount(), 1u);

    ecs.RemoveComponent<CharacterController>(player);
    pool.Reconcile(ecs, ActivePartitions());
    EXPECT_EQ(pool.MoverCount(), 0u);
    EXPECT_FALSE(ecs.HasComponent<CharacterMoverLink>(player));
}

TEST(CharacterMoverPool, ReleasesMoverWhenEntityDestroyed)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId player =
        SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    EXPECT_EQ(pool.MoverCount(), 1u);

    ecs.DestroyEntity(player);
    pool.Reconcile(ecs, ActivePartitions());
    EXPECT_EQ(pool.MoverCount(), 0u);
}

TEST(CharacterMoverPool, SlotReusedAfterRelease)
{
    PhysicsWorld physics;
    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId first =
        SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    const uint32_t firstSlot =
        ecs.TryGet<CharacterMoverLink>(first)->MoverSlot;

    ecs.DestroyEntity(first);
    pool.Reconcile(ecs, ActivePartitions());
    EXPECT_EQ(pool.MoverCount(), 0u);

    const EntityId second =
        SpawnCharacter(ecs, Vec3d(1.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    EXPECT_EQ(pool.MoverCount(), 1u);
    EXPECT_EQ(
        ecs.TryGet<CharacterMoverLink>(second)->MoverSlot,
        firstSlot);
}
