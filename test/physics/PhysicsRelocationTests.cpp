#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/MotionChannels.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <time/ActorTime.h>
#include <world/WorldComponentRegistration.h>
#include <world/relocation/Relocation.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <numbers>

// The live backend copies of a relocated entity: a rigid body's pose and
// velocities are pulled from the backend each step, and a character's position
// is its mover's, so both must be moved there or the next step undoes the move.
namespace
{
    constexpr StoragePartitionId kHere{ 1 };
    constexpr StoragePartitionId kThere{ 2 };
    const Quatf kQuarterTurn = Quatf::FromAxisAngle(Vec3d::Up(), std::numbers::pi_v<float> * 0.5f);
    const Transform3f kMapping{ Vec3d(50.0f, 0.0f, 0.0f), kQuarterTurn, Vec3d::One() };

    bool Near(const Vec3d& a, const Vec3d& b, float tolerance = 1e-3f) { return (a - b).Magnitude() < tolerance; }

    struct PhysicsRelocationWorld
    {
        PhysicsWorld Physics;
        World Entities;
        RigidBodyBinding Bodies{ Physics };
        CharacterMoverPool Movers{ Physics };
        StoragePartitionSet Active;

        PhysicsRelocationWorld()
        {
            ComponentStorageTraits<LocalTransform>::Register(Entities);
            RegisterPhysicsComponents(Entities);
            Entities.RegisterComponent<RelocationSerial>();
            Entities.RegisterComponent<MotionRequest>();
            Entities.RegisterComponent<KinematicState>();
            Entities.RegisterComponent<SupportState>();
            Entities.RegisterComponent<ActorTimeDilated>();
            RegisterWorldRelocation(Entities);
            RegisterPhysicsRelocation(Entities);
            InstallPhysicsBackendRelocation(Entities, Physics, Movers);
            Active.Add(StoragePartitionId::Default());
            Active.Add(kHere);
            Active.Add(kThere);
        }

        EntityId Spawn(const Vec3d& position)
        {
            const Transform3f pose{ position, Quatf::Identity(), Vec3d::One() };
            const EntityId entity = Entities.CreateEntity(kHere);
            Entities.AddComponent<LocalTransform>(entity, LocalTransform{ pose });
            Entities.AddComponent<WorldTransform>(entity, WorldTransform{ pose });
            return entity;
        }

        RelocationResult Relocate(EntityId entity)
        {
            return RelocateEntity(Entities, entity, RigidRelocation{ .Mapping = kMapping, .Partition = kThere });
        }
    };
}

TEST(PhysicsRelocation, ALiveBodyIsTeleportedWithItsVelocitiesTurnedAndKeepsItsBody)
{
    PhysicsRelocationWorld fx;
    const EntityId crate = fx.Spawn(Vec3d(0.0f, 5.0f, 0.0f));
    fx.Entities.AddComponent<Collider>(crate, Collider{ CollisionShape::MakeSphere(0.25f) });
    RigidBody body;
    body.LinearVelocity = Vec3d(4.0f, 0.0f, 0.0f);
    body.GravityScale = 0.0f;
    body.LinearDamping = 0.0f;
    fx.Entities.AddComponent<RigidBody>(crate, body);
    fx.Bodies.SyncToPhysics(fx.Entities, fx.Active);
    const PhysicsBodyId id = fx.Entities.TryGet<PhysicsBodyLink>(crate)->Body;

    ASSERT_EQ(fx.Relocate(crate).Outcome, RelocationOutcome::Relocated);

    const Vec3d arrived = kMapping.TransformPoint(Vec3d(0.0f, 5.0f, 0.0f));
    const Vec3d turned = kQuarterTurn.RotateVector(Vec3d(4.0f, 0.0f, 0.0f));
    EXPECT_TRUE(Near(fx.Physics.GetBodyTransform(id).Position, arrived));
    EXPECT_TRUE(Near(fx.Physics.GetLinearVelocity(id), turned));

    fx.Bodies.SyncToPhysics(fx.Entities, fx.Active);
    fx.Physics.Step(0.25f);
    fx.Bodies.SyncFromPhysics(fx.Entities, fx.Active);
    EXPECT_EQ(fx.Entities.TryGet<PhysicsBodyLink>(crate)->Body, id) << "a move between active zones keeps the body";
    EXPECT_TRUE(Near(fx.Entities.TryGet<LocalTransform>(crate)->Value.Position, arrived + turned * 0.25f, 1e-2f))
        << "the next step carries on from the far side";
}

TEST(PhysicsRelocation, ACharacterMoverFollowsItsTransformAndKeepsItsMover)
{
    PhysicsRelocationWorld fx;
    const EntityId walker = fx.Spawn(Vec3d(0.0f, 2.0f, 0.0f));
    fx.Entities.AddComponent<CharacterController>(walker, CharacterController{});
    fx.Entities.AddComponent<MotionRequest>(walker, MotionRequest{});
    fx.Entities.AddComponent<KinematicState>(walker, KinematicState{});
    fx.Entities.AddComponent<SupportState>(walker, SupportState{});
    fx.Movers.Reconcile(fx.Entities, fx.Active);
    const std::uint32_t slot = fx.Entities.TryGet<CharacterMoverLink>(walker)->MoverSlot;

    ASSERT_EQ(fx.Relocate(walker).Outcome, RelocationOutcome::Relocated);
    fx.Movers.Reconcile(fx.Entities, fx.Active);
    ASSERT_TRUE(fx.Movers.Step(fx.Entities, walker, MotionRequest{ .GravityScale = 0.0f }, 1.0f / 60.0f, Vec3d::Zero()));

    EXPECT_EQ(fx.Entities.TryGet<CharacterMoverLink>(walker)->MoverSlot, slot);
    EXPECT_TRUE(Near(fx.Entities.TryGet<LocalTransform>(walker)->Value.Position,
                     kMapping.TransformPoint(Vec3d(0.0f, 2.0f, 0.0f))))
        << "a sweep starts from where the mover was moved to, not from the side it left";
}
