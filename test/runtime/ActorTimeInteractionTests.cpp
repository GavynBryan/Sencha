// A dilated character against the rigid-body world, which runs at simulation rate:
// it pushes a body with the velocity it crosses the simulation at, and its substeps
// keep each sweep short enough that thin geometry stops it.

#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <movement/CharacterTickStep.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
#include <movement/components/CharacterMovement.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <time/ActorTime.h>
#include <time/ActorTimeLeases.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

namespace
{
constexpr float kDt = 1.0f / 60.0f;
const Vec3d kGravity(0.0f, -9.81f, 0.0f);
const Vec3d kUp(0.0f, 1.0f, 0.0f);

const StoragePartitionSet& Partitions()
{
    static const StoragePartitionSet partitions = [] {
        StoragePartitionSet value;
        value.Add(StoragePartitionId::Default());
        return value;
    }();
    return partitions;
}

// One character on a floor, walking +X at its scale; the bodies a test adds run at
// simulation rate.
struct Arena
{
    PhysicsWorld Physics;
    CharacterMoverPool Movers{ Physics };
    World Entities;
    EntityId Character;
    std::uint64_t Now = 0;

    Arena()
    {
        ComponentRegistrar registrar(Entities);
        RegisterEngineComponents(registrar);
        RegisterMovement(Entities);
        BodyDesc floor;
        floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
        floor.Motion = BodyMotion::Static;
        floor.Layer = CollisionLayer::Static;
        (void)Physics.AddBody(floor);

        Character = Entities.CreateEntity();
        Entities.AddComponent<LocalTransform>(
            Character, LocalTransform{ Transform3f{ Vec3d(0.0f, 1.5f, 0.0f), Quatf::Identity(), Vec3d::One() } });
        Entities.AddComponent<CharacterController>(Character, CharacterController{});
        Entities.AddComponent<CharacterMovement>(
            Character, CharacterMovement{ .Mode = Entities.GetResource<LocomotionModeRegistry>().FreeMode() });
        Movers.Reconcile(Entities, Partitions());
    }

    PhysicsBodyId Add(BodyDesc desc) { return Physics.AddBody(desc); }

    void Run(int ticks, std::int64_t scaleQ16, bool walking = true)
    {
        MovementIntent intent;
        if (walking)
            intent.WishDir = Vec3d(1.0f, 0.0f, 0.0f);
        for (int i = 0; i < ticks; ++i)
        {
            StepScaledCharacterTick(Entities, &Movers, Character, intent, Now, kDt, scaleQ16, kGravity, kUp);
            Physics.Step(kDt);
            ++Now;
        }
    }

    [[nodiscard]] float X() const
    {
        const World& reader = Entities;
        return reader.TryGet<LocalTransform>(Character)->Value.Position.X;
    }
};

BodyDesc Crate(float x)
{
    BodyDesc crate;
    crate.Shape = CollisionShape::MakeBox(Vec3d(0.25f, 0.25f, 0.25f));
    crate.Motion = BodyMotion::Dynamic;
    crate.Position = Vec3d(x, 0.75f, 0.0f);
    crate.Mass = 2.0f;
    return crate;
}
}

TEST(ActorTimeInteraction, ADilatedCharacterPushesABodyAtTheSpeedItCrossesTheSimulation)
{
    Arena normal;
    Arena slowed;
    const PhysicsBodyId pushedByNormal = normal.Add(Crate(1.5f));
    const PhysicsBodyId pushedBySlowed = slowed.Add(Crate(1.5f));
    normal.Run(90, kActorTimeOne);
    slowed.Run(180, kActorTimeOne / 2);

    const float normalCrate = normal.Physics.GetBodyTransform(pushedByNormal).Position.X;
    const float slowedCrate = slowed.Physics.GetBodyTransform(pushedBySlowed).Position.X;
    EXPECT_NEAR(slowed.X(), normal.X(), 0.15f) << "as far over the same actor time";
    EXPECT_GT(slowedCrate, 2.5f) << "its sweeps push the crate as an undilated character's do";
    EXPECT_LT(slowedCrate, normalCrate) << "kicked at half the speed, the crate runs on less far";
}

TEST(ActorTimeInteraction, FastActorDoesNotTunnelThroughThinWall)
{
    Arena hastened;
    BodyDesc wall;
    wall.Shape = CollisionShape::MakeBox(Vec3d(0.01f, 2.0f, 5.0f));
    wall.Motion = BodyMotion::Static;
    wall.Layer = CollisionLayer::Static;
    wall.Position = Vec3d(2.0f, 2.0f, 0.0f);
    (void)hastened.Add(wall);
    hastened.Run(120, kActorScaleHardMaximumQ16);
    EXPECT_LT(hastened.X(), 2.0f - 0.25f) << "stopped on the near side of a two-centimetre wall";
    EXPECT_GT(hastened.X(), 1.0f) << "having walked up to it";
}
