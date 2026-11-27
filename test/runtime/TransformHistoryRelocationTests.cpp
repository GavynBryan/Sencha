#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <math/geometry/3d/RigidMapping.h>
#include <world/WorldComponentRegistration.h>
#include <world/relocation/Relocation.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <gtest/gtest.h>

#include <numbers>

// An entity walks +X one metre a tick, then crosses at a quarter of the way
// through a tick into a room 100 m away and turned a quarter about up.
namespace
{
    const Transform3f kMapping{ Vec3d(100.0f, 0.0f, 0.0f),
                                Quatf::FromAxisAngle(Vec3d::Up(), std::numbers::pi_v<float> * 0.5f), Vec3d::One() };
    constexpr float kCrossedAt = 0.25f;

    bool Near(const Vec3d& a, const Vec3d& b) { return (a - b).Magnitude() < 1e-4f; }

    struct WalkWorld
    {
        World Entities;
        StoragePartitionSet Partitions;
        EntityId Walker;

        WalkWorld()
        {
            ComponentStorageTraits<LocalTransform>::Register(Entities);
            Entities.RegisterComponent<WorldTransformHistory>();
            Entities.RegisterComponent<RelocationSerial>();
            RegisterWorldRelocation(Entities);
            Partitions.Add(StoragePartitionId::Default());
            Walker = Spawn(Vec3d::Zero());
        }

        EntityId Spawn(const Vec3d& position)
        {
            const EntityId entity = Entities.CreateEntity();
            const Transform3f pose{ position, Quatf::Identity(), Vec3d::One() };
            Entities.AddComponent<LocalTransform>(entity, LocalTransform{ pose });
            Entities.AddComponent<WorldTransform>(entity, WorldTransform{ pose });
            Entities.AddComponent<WorldTransformHistory>(entity, WorldTransformHistory{});
            return entity;
        }

        // What a tick's motion does to both transforms of a root.
        void Step(const Vec3d& by)
        {
            Entities.TryGet<LocalTransform>(Walker)->Value.Position += by;
            Entities.TryGet<WorldTransform>(Walker)->Value.Position += by;
        }

        void Capture() { CaptureWorldTransformHistory(Entities, Partitions, false); }

        // Walks to x = 1, then walks a quarter metre, crosses, and walks on three
        // quarters of a metre the way the mapping turned +X.
        void WalkAndCross()
        {
            Capture();
            Step(Vec3d(1.0f, 0.0f, 0.0f));
            Capture();
            Step(Vec3d(kCrossedAt, 0.0f, 0.0f));
            ASSERT_EQ(RelocateEntity(Entities, Walker,
                                     RigidRelocation{ .Mapping = kMapping, .TickFraction = kCrossedAt })
                          .Outcome,
                      RelocationOutcome::Relocated);
            Step(kMapping.Rotation.RotateVector(Vec3d(1.0f - kCrossedAt, 0.0f, 0.0f)));
        }

        Vec3d PresentedAt(double alpha) const { return PresentationPoseOf(Entities, Walker, alpha).Position; }
    };
}

TEST(TransformHistoryRelocation, UntilTheNextCaptureTheBlendIsTheOneBeforeTheCrossing)
{
    WalkWorld world;
    world.WalkAndCross();

    EXPECT_TRUE(Near(world.PresentedAt(0.0), Vec3d(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(Near(world.PresentedAt(1.0), Vec3d(1.0f, 0.0f, 0.0f)));
}

TEST(TransformHistoryRelocation, BeforeTheCrossingFractionItIsDrawnOnTheSideItCameFrom)
{
    WalkWorld world;
    world.WalkAndCross();
    world.Capture();

    EXPECT_TRUE(Near(world.PresentedAt(0.0), Vec3d(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(Near(world.PresentedAt(0.125), Vec3d(1.125f, 0.0f, 0.0f)));
}

TEST(TransformHistoryRelocation, AtAndAfterTheCrossingFractionItIsDrawnOnTheFarSide)
{
    WalkWorld world;
    world.WalkAndCross();
    world.Capture();

    EXPECT_TRUE(Near(world.PresentedAt(kCrossedAt), kMapping.TransformPoint(Vec3d(1.25f, 0.0f, 0.0f))));
    EXPECT_TRUE(Near(world.PresentedAt(0.5), kMapping.TransformPoint(Vec3d(1.5f, 0.0f, 0.0f))));
    EXPECT_TRUE(Near(world.PresentedAt(1.0), kMapping.TransformPoint(Vec3d(2.0f, 0.0f, 0.0f))));
    EXPECT_TRUE(PresentationPoseOf(world.Entities, world.Walker, 0.5)
                    .Rotation.NearlyEquals(kMapping.Rotation, 1e-5f));
}

TEST(TransformHistoryRelocation, TheCaptureAfterThatBlendsInTheNewSpaceAlone)
{
    WalkWorld world;
    world.WalkAndCross();
    world.Capture();
    world.Step(kMapping.Rotation.RotateVector(Vec3d(1.0f, 0.0f, 0.0f)));
    world.Capture();

    EXPECT_EQ(world.Entities.TryGet<WorldTransformHistory>(world.Walker)->Crossing, HistoryCrossing::None);
    EXPECT_TRUE(Near(world.PresentedAt(0.0), kMapping.TransformPoint(Vec3d(2.0f, 0.0f, 0.0f))));
}

TEST(TransformHistoryRelocation, AChildIsDrawnThroughItsRelocatedParent)
{
    WalkWorld world;
    const EntityId child = world.Entities.CreateEntity();
    const Transform3f above{ Vec3d(0.0f, 2.0f, 0.0f), Quatf::Identity(), Vec3d::One() };
    world.Entities.AddComponent<LocalTransform>(child, LocalTransform{ above });
    world.Entities.AddComponent<WorldTransform>(child, WorldTransform{ above });
    world.Entities.AddComponent<Parent>(child, Parent{ world.Walker });
    world.WalkAndCross();
    world.Capture();

    EXPECT_TRUE(Near(PresentationPoseOf(world.Entities, child, 0.125).Position, Vec3d(1.125f, 2.0f, 0.0f)));
    EXPECT_TRUE(
        Near(PresentationPoseOf(world.Entities, child, 0.5).Position, kMapping.TransformPoint(Vec3d(1.5f, 2.0f, 0.0f))));
}

TEST(TransformHistoryRelocation, ASnapOutranksACarryWhicheverCameFirst)
{
    for (const bool snapFirst : { true, false })
    {
        WalkWorld world;
        world.Capture();
        if (snapFirst)
            RequestTransformHistorySnap(world.Entities, world.Walker);
        (void)RelocateEntity(world.Entities, world.Walker, RigidRelocation{ .Mapping = kMapping });
        if (!snapFirst)
            RequestTransformHistorySnap(world.Entities, world.Walker);
        world.Capture();

        const WorldTransformHistory& history = *world.Entities.TryGet<WorldTransformHistory>(world.Walker);
        EXPECT_EQ(history.Crossing, HistoryCrossing::None);
        EXPECT_TRUE(Near(world.PresentedAt(0.0), kMapping.TransformPoint(Vec3d::Zero()))) << snapFirst;
    }
}

TEST(TransformHistoryRelocation, TwoCrossingsInOneTickComposeAndCrossAtTheLater)
{
    WalkWorld world;
    world.Capture();
    world.Step(Vec3d(1.0f, 0.0f, 0.0f));
    world.Capture();
    const Transform3f back = InverseRigidMapping(kMapping);
    (void)RelocateEntity(world.Entities, world.Walker, RigidRelocation{ .Mapping = kMapping, .TickFraction = 0.2f });
    (void)RelocateEntity(world.Entities, world.Walker, RigidRelocation{ .Mapping = back, .TickFraction = 0.6f });
    world.Step(Vec3d(1.0f, 0.0f, 0.0f));
    world.Capture();

    EXPECT_TRUE(Near(world.PresentedAt(0.5), Vec3d(1.5f, 0.0f, 0.0f)))
        << "there and back again composes to no move at all";
    EXPECT_FLOAT_EQ(world.Entities.TryGet<WorldTransformHistory>(world.Walker)->CrossedAt, 0.6f);
}
