#include <gtest/gtest.h>

#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/ZoneLoadPackage.h>

#include <chrono>
#include <thread>

struct ZoneLoadMarker
{
    int Value = 0;
};
SENCHA_DECLARE_COMPONENT_TYPE(ZoneLoadMarker, "test.zone_load_marker");

namespace
{
WorldComponentSchema MakeSchema()
{
    WorldComponentSchema schema;
    schema.Add<ZoneLoadMarker>();
    schema.Seal();
    return schema;
}

void BuildTestZone(ZoneLoadPackage& package, int entityCount)
{
    for (int i = 0; i < entityCount; ++i)
    {
        const ZoneLocalEntityId entity = package.CreateEntity();
        EXPECT_TRUE(package.AddComponent(entity, ZoneLoadMarker{ i }));
    }
}

void ProcessResidency(RuntimeWorld& world)
{
    (void)world.BeginResidencyProcessing();
    world.FinalizeResidencyProcessing();
}

std::size_t CountMarkers(RuntimeWorld& world, ZoneId zone)
{
    const RuntimeZoneRecord* record = world.FindZone(zone);
    if (record == nullptr)
        return 0;

    std::size_t count = 0;
    for (EntityId entity : world.Entities().GetAliveEntities())
    {
        if (world.Entities().GetEntityPartition(entity) != record->Partition)
            continue;
        if (world.Entities().TryGet<ZoneLoadMarker>(entity) != nullptr)
            ++count;
    }
    return count;
}
} // namespace

TEST(AsyncZoneLoad, ZeroThreadEndToEnd)
{
    AsyncTaskQueue tasks(0);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    const ZoneId zone{ 3 };
    auto handle = loader.BeginLoad(
        zone,
        [](ZoneLoadPackage& package) { BuildTestZone(package, 64); },
        ZoneParticipation{ .Visible = true, .Logic = true });

    EXPECT_TRUE(loader.IsLoading(zone));
    EXPECT_FALSE(world.IsZoneResident(zone));

    // Work builds only detached package data. No RuntimeWorld partition exists.
    EXPECT_EQ(tasks.PumpWork(), 1u);
    EXPECT_TRUE(loader.IsLoading(zone));
    EXPECT_EQ(world.FindZone(zone), nullptr);

    // Commit imports hidden data, publishes once, then marks discontinuity.
    EXPECT_EQ(tasks.DrainCompletions(), 1u);
    EXPECT_FALSE(loader.IsLoading(zone));
    EXPECT_TRUE(tasks.IsComplete(handle));
    ASSERT_TRUE(world.IsZoneResident(zone));
    EXPECT_EQ(CountMarkers(world, zone), 64u);
    EXPECT_TRUE(world.FindZone(zone)->Participation.Visible);
    EXPECT_EQ(
        runtime.GetCurrentFrame().DiscontinuityReason,
        TemporalDiscontinuityReason::ZoneLoad);
}

TEST(AsyncZoneLoad, FinalizeRunsWhileImportIsStillHidden)
{
    AsyncTaskQueue tasks(0);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    const ZoneId zone{ 8 };
    bool finalized = false;

    loader.BeginLoad(
        zone,
        [](ZoneLoadPackage& package) { BuildTestZone(package, 5); },
        [&](RuntimeWorld& runtimeWorld, RuntimeZoneRecord& record) {
            finalized = true;
            EXPECT_EQ(record.State, RuntimeZoneLoadState::Importing);
            EXPECT_FALSE(runtimeWorld.IsZoneResident(zone));
            EXPECT_TRUE(runtimeWorld.PendingResidencyChanges().empty());
            EXPECT_NE(
                runtime.GetCurrentFrame().DiscontinuityReason,
                TemporalDiscontinuityReason::ZoneLoad);

            const EntityId extra =
                runtimeWorld.Entities().CreateEntity(record.Partition);
            runtimeWorld.Entities().AddComponent<ZoneLoadMarker>(
                extra,
                ZoneLoadMarker{ 99 });
            return true;
        },
        ZoneParticipation{ .Logic = true });

    tasks.PumpWork();
    EXPECT_FALSE(finalized);

    tasks.DrainCompletions();
    EXPECT_TRUE(finalized);
    EXPECT_TRUE(world.IsZoneResident(zone));
    EXPECT_EQ(CountMarkers(world, zone), 6u);
    EXPECT_EQ(
        runtime.GetCurrentFrame().DiscontinuityReason,
        TemporalDiscontinuityReason::ZoneLoad);
}

TEST(AsyncZoneLoad, FailedFinalizeCancelsWithoutPublication)
{
    AsyncTaskQueue tasks(0);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    const ZoneId zone{ 12 };
    loader.BeginLoad(
        zone,
        [](ZoneLoadPackage& package) { BuildTestZone(package, 4); },
        [](RuntimeWorld&, RuntimeZoneRecord&) { return false; },
        ZoneParticipation{ .Logic = true });

    tasks.PumpWork();
    tasks.DrainCompletions();

    EXPECT_EQ(world.FindZone(zone), nullptr);
    EXPECT_EQ(world.Entities().EntityCount(), 0u);
    EXPECT_TRUE(world.PendingResidencyChanges().empty());
    EXPECT_EQ(
        runtime.GetCurrentFrame().DiscontinuityReason,
        TemporalDiscontinuityReason::None);
}

TEST(AsyncZoneLoad, DormantPreloadAttachesSeamlessly)
{
    AsyncTaskQueue tasks(0);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    const ZoneId nextRoom{ 11 };
    loader.BeginLoad(
        nextRoom,
        [](ZoneLoadPackage& package) { BuildTestZone(package, 12); });

    tasks.PumpWork();
    tasks.DrainCompletions();
    ProcessResidency(world);

    ASSERT_TRUE(world.IsZoneResident(nextRoom));
    EXPECT_EQ(
        runtime.GetCurrentFrame().DiscontinuityReason,
        TemporalDiscontinuityReason::None);

    const StoragePartitionId partition = world.FindZone(nextRoom)->Partition;
    const FrameZoneView& dormantView = world.BuildFrameView();
    EXPECT_TRUE(dormantView.Resident.Contains(partition));
    EXPECT_FALSE(dormantView.Visible.Contains(partition));
    EXPECT_FALSE(dormantView.Logic.Contains(partition));

    // Gameplay requests activation mid-frame. The current view remains stable;
    // the next drain applies it before the next frame view is built.
    ASSERT_TRUE(world.RequestParticipation(
        nextRoom,
        ZoneParticipation{ .Visible = true, .Logic = true }));
    EXPECT_FALSE(dormantView.Logic.Contains(partition));
    world.EndFrameView();

    world.FlushLifecycleRequests();
    ProcessResidency(world);
    const FrameZoneView& liveView = world.BuildFrameView();
    EXPECT_TRUE(liveView.Visible.Contains(partition));
    EXPECT_TRUE(liveView.Logic.Contains(partition));
    world.EndFrameView();
}

TEST(AsyncZoneLoad, CancelBeforeWorkDropsTheLoad)
{
    AsyncTaskQueue tasks(0);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    const ZoneId zone{ 4 };
    bool buildRan = false;
    loader.BeginLoad(zone, [&](ZoneLoadPackage&) { buildRan = true; });

    EXPECT_TRUE(loader.CancelLoad(zone));
    EXPECT_FALSE(loader.IsLoading(zone));
    EXPECT_EQ(tasks.PumpWork(), 0u);
    EXPECT_EQ(tasks.DrainCompletions(), 0u);
    EXPECT_FALSE(buildRan);
    EXPECT_EQ(world.FindZone(zone), nullptr);
}

TEST(AsyncZoneLoad, CancelAfterWorkDropsTheImport)
{
    AsyncTaskQueue tasks(0);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    const ZoneId zone{ 5 };
    loader.BeginLoad(
        zone,
        [](ZoneLoadPackage& package) { BuildTestZone(package, 8); });

    EXPECT_EQ(tasks.PumpWork(), 1u);
    EXPECT_TRUE(loader.CancelLoad(zone));
    EXPECT_EQ(tasks.DrainCompletions(), 0u);
    EXPECT_EQ(world.FindZone(zone), nullptr);
    EXPECT_FALSE(loader.IsLoading(zone));
}

TEST(AsyncZoneLoad, ReloadAfterFinalDetachUsesFreshPartitionContent)
{
    AsyncTaskQueue tasks(0);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    const ZoneId zone{ 6 };
    loader.BeginLoad(
        zone,
        [](ZoneLoadPackage& package) { BuildTestZone(package, 4); });
    tasks.PumpWork();
    tasks.DrainCompletions();
    ProcessResidency(world);
    ASSERT_TRUE(world.IsZoneResident(zone));

    loader.RequestDestroy(zone);
    tasks.PumpWork();
    tasks.DrainCompletions();
    ProcessResidency(world);
    EXPECT_EQ(world.FindZone(zone), nullptr);

    loader.BeginLoad(
        zone,
        [](ZoneLoadPackage& package) { BuildTestZone(package, 9); });
    tasks.PumpWork();
    tasks.DrainCompletions();
    ASSERT_TRUE(world.IsZoneResident(zone));
    EXPECT_EQ(CountMarkers(world, zone), 9u);
}

TEST(AsyncZoneLoad, ThreadedLoadCompletesViaPolling)
{
    AsyncTaskQueue tasks(1);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    const ZoneId zone{ 9 };
    std::thread::id buildThread;
    loader.BeginLoad(
        zone,
        [&](ZoneLoadPackage& package) {
            buildThread = std::this_thread::get_id();
            BuildTestZone(package, 32);
        },
        ZoneParticipation{ .Visible = true });

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!world.IsZoneResident(zone))
    {
        tasks.DrainCompletions();
        ASSERT_LT(std::chrono::steady_clock::now(), deadline)
            << "zone load timed out";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_NE(buildThread, std::this_thread::get_id());
    EXPECT_FALSE(loader.IsLoading(zone));
    EXPECT_EQ(CountMarkers(world, zone), 32u);
    EXPECT_EQ(
        runtime.GetCurrentFrame().DiscontinuityReason,
        TemporalDiscontinuityReason::ZoneLoad);
}

TEST(AsyncZoneLoadContracts, BeginLoadOnLoadedZoneDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    AsyncTaskQueue tasks(0);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    world.AttachZone(ZoneId{ 2 });
    EXPECT_DEATH(
        loader.BeginLoad(ZoneId{ 2 }, [](ZoneLoadPackage&) {}),
        "already loaded or importing");
}

TEST(AsyncZoneLoadContracts, BeginLoadWhileInFlightDies)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    AsyncTaskQueue tasks(0);
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld world(schema);
    ComponentSerializerRegistry serializers;
    LoggingProvider logging;
    SceneSerializationContext sceneContext(logging);
    RuntimeFrameLoop runtime;
    AsyncZoneLoader loader(
        tasks,
        world,
        schema,
        serializers,
        sceneContext,
        runtime);

    loader.BeginLoad(ZoneId{ 2 }, [](ZoneLoadPackage&) {});
    EXPECT_DEATH(
        loader.BeginLoad(ZoneId{ 2 }, [](ZoneLoadPackage&) {}),
        "already in flight");
}
