#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <gtest/gtest.h>

namespace
{
    struct HistoryWorld
    {
        World Entities;
        StoragePartitionSet Partitions;

        HistoryWorld()
        {
            Entities.RegisterComponent<LocalTransform>();
            Entities.RegisterComponent<WorldTransform>();
            Entities.RegisterComponent<WorldTransformHistory>();
            Partitions.Add(StoragePartitionId::Default());
        }

        EntityId SpawnAt(float x)
        {
            const EntityId entity = Entities.CreateEntity();
            Transform3f pose;
            pose.Position = Vec3d(x, 0.0f, 0.0f);
            Entities.AddComponent<LocalTransform>(entity, LocalTransform{ pose });
            Entities.AddComponent<WorldTransform>(entity, WorldTransform{ pose });
            Entities.AddComponent<WorldTransformHistory>(entity, WorldTransformHistory{});
            return entity;
        }

        void MoveTo(EntityId entity, float x)
        {
            Transform3f pose;
            pose.Position = Vec3d(x, 0.0f, 0.0f);
            Entities.TryGet<WorldTransform>(entity)->Value = pose;
        }

        const WorldTransformHistory& HistoryOf(EntityId entity)
        {
            return *Entities.TryGet<WorldTransformHistory>(entity);
        }

        void Capture(bool snapAll = false)
        {
            CaptureWorldTransformHistory(Entities, Partitions, snapAll);
        }
    };
}

TEST(TransformHistory, FirstCaptureHasNothingToBlendFrom)
{
    HistoryWorld world;
    const EntityId entity = world.SpawnAt(5.0f);

    world.Capture();

    // A spawn must render where it was placed, not blend in from the origin.
    const WorldTransformHistory& history = world.HistoryOf(entity);
    EXPECT_FLOAT_EQ(history.Previous.Position.X, 5.0f);
    EXPECT_FLOAT_EQ(history.Current.Position.X, 5.0f);
    EXPECT_FALSE(history.Snap);
    EXPECT_FLOAT_EQ(ResolvePresentationPose(history, 0.0).Position.X, 5.0f);
}

TEST(TransformHistory, CaptureShiftsCurrentIntoPrevious)
{
    HistoryWorld world;
    const EntityId entity = world.SpawnAt(0.0f);
    world.Capture();

    world.MoveTo(entity, 1.0f);
    world.Capture();

    const WorldTransformHistory& history = world.HistoryOf(entity);
    EXPECT_FLOAT_EQ(history.Previous.Position.X, 0.0f);
    EXPECT_FLOAT_EQ(history.Current.Position.X, 1.0f);
}

TEST(TransformHistory, ResolveBlendsBetweenTheLastTwoTicks)
{
    HistoryWorld world;
    const EntityId entity = world.SpawnAt(0.0f);
    world.Capture();
    world.MoveTo(entity, 10.0f);
    world.Capture();

    const WorldTransformHistory& history = world.HistoryOf(entity);
    EXPECT_FLOAT_EQ(ResolvePresentationPose(history, 0.0).Position.X, 0.0f);
    EXPECT_FLOAT_EQ(ResolvePresentationPose(history, 0.5).Position.X, 5.0f);
    EXPECT_FLOAT_EQ(ResolvePresentationPose(history, 1.0).Position.X, 10.0f);
}

TEST(TransformHistory, ResolveClampsAlphaOutsideTheTick)
{
    HistoryWorld world;
    const EntityId entity = world.SpawnAt(0.0f);
    world.Capture();
    world.MoveTo(entity, 10.0f);
    world.Capture();

    const WorldTransformHistory& history = world.HistoryOf(entity);
    EXPECT_FLOAT_EQ(ResolvePresentationPose(history, -1.0).Position.X, 0.0f);
    EXPECT_FLOAT_EQ(ResolvePresentationPose(history, 2.0).Position.X, 10.0f);
}

TEST(TransformHistory, SnapAllCollapsesEveryHistory)
{
    HistoryWorld world;
    const EntityId first = world.SpawnAt(0.0f);
    const EntityId second = world.SpawnAt(100.0f);
    world.Capture();

    world.MoveTo(first, 1.0f);
    world.MoveTo(second, 200.0f);
    world.Capture(/*snapAll*/ true);

    // A frame-wide discontinuity has no previous pose worth blending from, so
    // nothing streaks across the gap.
    EXPECT_FLOAT_EQ(world.HistoryOf(first).Previous.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(world.HistoryOf(second).Previous.Position.X, 200.0f);
    EXPECT_FLOAT_EQ(ResolvePresentationPose(world.HistoryOf(second), 0.0).Position.X, 200.0f);
}

TEST(TransformHistory, RequestSnapAffectsOnlyThatEntity)
{
    HistoryWorld world;
    const EntityId teleported = world.SpawnAt(0.0f);
    const EntityId walking = world.SpawnAt(0.0f);
    world.Capture();

    RequestTransformHistorySnap(world.Entities, teleported);
    world.MoveTo(teleported, 500.0f);
    world.MoveTo(walking, 1.0f);
    world.Capture();

    // The teleported entity appears at its destination; its neighbour still
    // blends normally.
    EXPECT_FLOAT_EQ(ResolvePresentationPose(world.HistoryOf(teleported), 0.0).Position.X, 500.0f);
    EXPECT_FLOAT_EQ(ResolvePresentationPose(world.HistoryOf(walking), 0.0).Position.X, 0.0f);
    EXPECT_FLOAT_EQ(ResolvePresentationPose(world.HistoryOf(walking), 1.0).Position.X, 1.0f);
}

TEST(TransformHistory, SnapRequestOnEntityWithoutHistoryIsHarmless)
{
    HistoryWorld world;
    const EntityId bare = world.Entities.CreateEntity();

    RequestTransformHistorySnap(world.Entities, bare);

    EXPECT_FALSE(world.Entities.HasComponent<WorldTransformHistory>(bare));
}
