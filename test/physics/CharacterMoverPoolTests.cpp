#include <gtest/gtest.h>

#include <algorithm>
#include <tuple>

#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsWorld.h>
#include <movement/MovementComponentSchemas.h>
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
    world.RegisterComponent<MotionRequest>();
    world.RegisterComponent<KinematicState>();
    world.RegisterComponent<SupportState>();
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
    world.AddComponent<MotionRequest>(entity, MotionRequest{});
    world.AddComponent<KinematicState>(entity, KinematicState{});
    world.AddComponent<SupportState>(entity, SupportState{});
    return entity;
}

EntityId SpawnCharacter(World& world, const Vec3d& position)
{
    return SpawnCharacterIn(
        world,
        StoragePartitionId::Default(),
        position);
}

// Stands in for the locomotion and composition systems: carry the achieved
// velocity forward, integrate gravity, and cancel descent while standing. The
// motor owns no velocity of its own, so something has to.
void StepLocomotion(World& world, EntityId entity)
{
    const KinematicState* kinematic = world.TryGet<KinematicState>(entity);
    const SupportState* support = world.TryGet<SupportState>(entity);
    MotionRequest* request = world.TryGet<MotionRequest>(entity);
    if (kinematic == nullptr || support == nullptr || request == nullptr)
        return;

    Vec3d velocity = kinematic->Velocity;
    if (support->Kind == SupportKind::Stable && velocity.Y < 0.0f)
        velocity.Y = 0.0f;
    velocity.Y += static_cast<float>(kGravity.Y) * kFixedDt;
    request->Velocity = velocity;
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
        StepLocomotion(ecs, player);
        pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
    }

    const LocalTransform* rest = ecs.TryGet<LocalTransform>(player);
    ASSERT_NE(rest, nullptr);
    EXPECT_NEAR(rest->Value.Position.Y, 1.4f, 0.05f);
}

TEST(CharacterMoverPool, UpwardRequestLaunchesMoverUpward)
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
        StepLocomotion(ecs, player);
        pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
    }
    const float restY =
        ecs.TryGet<LocalTransform>(player)->Value.Position.Y;
    ASSERT_EQ(
        ecs.TryGet<SupportState>(player)->Kind,
        SupportKind::Stable);

    // The composed request is the only way in: an upward velocity is what a
    // jump reduces to by the time the motor sees it.
    ecs.TryGet<MotionRequest>(player)->Velocity = Vec3d(0.0f, 5.0f, 0.0f);

    float peakY = restY;
    for (int index = 0; index < 30; ++index)
    {
        pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
        peakY = std::max(
            peakY,
            ecs.TryGet<LocalTransform>(player)->Value.Position.Y);
        StepLocomotion(ecs, player);
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

//=============================================================================
// The seam prediction replay stands on
//
// Replay re-runs the ticks a client has not had answered yet. It has to move
// the pawn through the code the live tick uses, and it has to be able to put
// the pawn back where the authority says it was first -- so both operations
// are pinned here against the chunk-driven path they must agree with.
//=============================================================================

TEST(CharacterMoverPoolStep, MovesOneCharacterExactlyAsDriveDoes)
{
    const auto run = [](bool useStep) {
        PhysicsWorld physics;
        AddStaticFloor(physics);

        World ecs;
        SetUpPhysics(ecs);
        CharacterMoverPool pool(physics);

        const EntityId player = SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
        pool.Reconcile(ecs, ActivePartitions());

        for (int index = 0; index < 120; ++index)
        {
            pool.Reconcile(ecs, ActivePartitions());
            StepLocomotion(ecs, player);
            if (useStep)
            {
                const MotionRequest request = *ecs.TryGet<MotionRequest>(player);
                EXPECT_TRUE(pool.Step(ecs, player, request, kFixedDt, kGravity));
            }
            else
            {
                pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
            }
        }

        return std::tuple{ ecs.TryGet<LocalTransform>(player)->Value.Position,
                           ecs.TryGet<KinematicState>(player)->Velocity,
                           ecs.TryGet<SupportState>(player)->Kind };
    };

    const auto [drivePos, driveVel, driveSupport] = run(false);
    const auto [stepPos, stepVel, stepSupport] = run(true);

    // Exact: it is the same sweep over the same geometry on the same machine.
    // Anything else means replay and the live tick are two rulebooks.
    EXPECT_EQ(stepPos.X, drivePos.X);
    EXPECT_EQ(stepPos.Y, drivePos.Y);
    EXPECT_EQ(stepPos.Z, drivePos.Z);
    EXPECT_EQ(stepVel.Y, driveVel.Y);
    EXPECT_EQ(stepSupport, driveSupport);
}

TEST(CharacterMoverPoolStep, RefusesAnEntityWithNoMover)
{
    PhysicsWorld physics;
    AddStaticFloor(physics);

    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId bare = ecs.CreateEntity();
    EXPECT_FALSE(pool.Step(ecs, bare, MotionRequest{}, kFixedDt, kGravity));
    EXPECT_FALSE(pool.RestorePosition(ecs, bare, Vec3d(1.0f, 1.0f, 1.0f)));
}

// The defect that made every correction a no-op: a character's position lives
// inside its mover, and the transform is where the last sweep left a copy.
// Writing the copy is undone by the next sweep, which starts from where the
// mover still believes it is.
TEST(CharacterMoverPoolStep, RestoreIsWhereTheNextSweepStartsFrom)
{
    PhysicsWorld physics;
    AddStaticFloor(physics);

    World ecs;
    SetUpPhysics(ecs);
    CharacterMoverPool pool(physics);

    const EntityId player = SpawnCharacter(ecs, Vec3d(0.0f, 5.0f, 0.0f));
    pool.Reconcile(ecs, ActivePartitions());
    for (int index = 0; index < 120; ++index)
    {
        pool.Reconcile(ecs, ActivePartitions());
        StepLocomotion(ecs, player);
        pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
    }

    const Vec3d moved(4.0f, ecs.TryGet<LocalTransform>(player)->Value.Position.Y,
                      0.0f);
    ASSERT_TRUE(pool.RestorePosition(ecs, player, moved));

    // A standing sweep must leave it where the restore put it.
    ecs.TryGet<MotionRequest>(player)->Velocity = Vec3d(0.0f, 0.0f, 0.0f);
    pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
    EXPECT_NEAR(ecs.TryGet<LocalTransform>(player)->Value.Position.X, 4.0f, 1e-3f);

    // The negative control for the same claim: writing only the transform is
    // reverted by the sweep, because the mover was never told.
    ecs.TryGet<LocalTransform>(player)->Value.Position.X = -7.0f;
    pool.Drive(ecs, ActivePartitions(), kFixedDt, kGravity);
    EXPECT_NEAR(ecs.TryGet<LocalTransform>(player)->Value.Position.X, 4.0f, 1e-3f)
        << "the transform-only write survived a sweep, so this test can no "
           "longer tell a real correction from a lost one";
}
