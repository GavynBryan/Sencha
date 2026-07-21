#include <components/ActiveCameraService.h>
#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>

#include <gtest/gtest.h>

struct RuntimeWorldValue
{
    int Value = 0;
};

struct RuntimeWorldTracked
{
    int Value = 0;
};

struct RuntimeWorldHookResource
{
    int RemoveCalls = 0;
};

struct RuntimeZoneResourceProbe
{
    explicit RuntimeZoneResourceProbe(int* destroyed)
        : Destroyed(destroyed)
    {
    }

    ~RuntimeZoneResourceProbe()
    {
        if (Destroyed != nullptr)
            ++*Destroyed;
    }

    int* Destroyed = nullptr;
};

SENCHA_DECLARE_COMPONENT_TYPE(RuntimeWorldValue, "test.runtime_world_value");
SENCHA_DECLARE_COMPONENT_TYPE(RuntimeWorldTracked, "test.runtime_world_tracked");

template <>
struct ComponentTraits<RuntimeWorldTracked>
{
    static void OnRemove(
        const RuntimeWorldTracked&,
        World& world,
        EntityId)
    {
        EXPECT_TRUE(world.HasResource<RuntimeWorldHookResource>());
        ++world.GetResource<RuntimeWorldHookResource>().RemoveCalls;
    }
};

namespace
{
WorldComponentSchema MakeRuntimeSchema()
{
    WorldComponentSchema schema;
    schema.Add<RuntimeWorldValue>();
    schema.Add<RuntimeWorldTracked>();
    schema.Seal();
    return schema;
}

void ProcessResidency(RuntimeWorld& runtime)
{
    (void)runtime.BeginResidencyProcessing();
    runtime.FinalizeResidencyProcessing();
}

ZoneParticipation LogicOnly()
{
    ZoneParticipation participation;
    participation.Logic = true;
    return participation;
}

ZoneParticipation PhysicsAndVisible()
{
    ZoneParticipation participation;
    participation.Visible = true;
    participation.Physics = true;
    return participation;
}
} // namespace

TEST(UnifiedRuntimeWorld, AppliesSchemaAndOwnsSimulationScopedResources)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);

    EXPECT_TRUE(runtime.Entities().IsRegistered<RuntimeWorldValue>());
    EXPECT_TRUE(runtime.Entities().HasResource<ActiveCameraService>());

    const EntityId persistent = runtime.Entities().CreateEntity();
    runtime.Entities().AddComponent<RuntimeWorldValue>(persistent, { 7 });

    EXPECT_EQ(
        runtime.Entities().GetEntityPartition(persistent),
        PersistentStoragePartition);
}

TEST(UnifiedRuntimeWorld, AttachAllocatesDenseZonePartitionsAndRecordsTransitions)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);

    RuntimeZoneRecord& first = runtime.AttachZone(ZoneId{ 10 }, LogicOnly());
    RuntimeZoneRecord& second = runtime.AttachZone(
        ZoneId{ 20 },
        PhysicsAndVisible());

    EXPECT_EQ(first.Partition, StoragePartitionId{ 1 });
    EXPECT_EQ(second.Partition, StoragePartitionId{ 2 });
    EXPECT_EQ(runtime.ZoneCount(), 2u);

    const auto changes = runtime.PendingResidencyChanges();
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].Kind, ZoneResidencyChangeKind::Attached);
    EXPECT_EQ(changes[0].Zone, ZoneId{ 10 });
    EXPECT_EQ(changes[0].Partition, first.Partition);
    EXPECT_EQ(changes[0].Current, LogicOnly());
    EXPECT_EQ(changes[1].Zone, ZoneId{ 20 });
}

TEST(UnifiedRuntimeWorld, FrameViewContainsPersistentAndParticipatingPartitions)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);

    const StoragePartitionId logic =
        runtime.AttachZone(ZoneId{ 1 }, LogicOnly()).Partition;
    const StoragePartitionId physics =
        runtime.AttachZone(ZoneId{ 2 }, PhysicsAndVisible()).Partition;
    const StoragePartitionId dormant =
        runtime.AttachZone(ZoneId{ 3 }, ZoneParticipation{}).Partition;
    ProcessResidency(runtime);

    const FrameZoneView& view = runtime.BuildFrameView();

    EXPECT_EQ(view.Entities, &runtime.Entities());
    EXPECT_TRUE(view.Resident.Contains(PersistentStoragePartition));
    EXPECT_TRUE(view.Visible.Contains(PersistentStoragePartition));
    EXPECT_TRUE(view.Physics.Contains(PersistentStoragePartition));
    EXPECT_TRUE(view.Logic.Contains(PersistentStoragePartition));
    EXPECT_TRUE(view.Audio.Contains(PersistentStoragePartition));

    EXPECT_TRUE(view.Resident.Contains(logic));
    EXPECT_TRUE(view.Resident.Contains(physics));
    EXPECT_TRUE(view.Resident.Contains(dormant));

    EXPECT_TRUE(view.Logic.Contains(logic));
    EXPECT_FALSE(view.Physics.Contains(logic));
    EXPECT_TRUE(view.Physics.Contains(physics));
    EXPECT_TRUE(view.Visible.Contains(physics));
    EXPECT_FALSE(view.Logic.Contains(physics));
    EXPECT_FALSE(view.Logic.Contains(dormant));
    EXPECT_FALSE(view.Physics.Contains(dormant));

    runtime.EndFrameView();
}

TEST(UnifiedRuntimeWorld, MidFrameParticipationRequestIsDeferred)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);

    const StoragePartitionId partition =
        runtime.AttachZone(ZoneId{ 1 }, ZoneParticipation{}).Partition;
    ProcessResidency(runtime);

    const FrameZoneView& before = runtime.BuildFrameView();
    EXPECT_FALSE(before.Logic.Contains(partition));
    ASSERT_TRUE(runtime.RequestParticipation(ZoneId{ 1 }, LogicOnly()));
    EXPECT_FALSE(before.Logic.Contains(partition));
    runtime.EndFrameView();

    runtime.FlushLifecycleRequests();
    const auto changes = runtime.PendingResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, ZoneResidencyChangeKind::ParticipationChanged);
    EXPECT_FALSE(changes[0].Previous.Any());
    EXPECT_EQ(changes[0].Current, LogicOnly());
    ProcessResidency(runtime);

    const FrameZoneView& after = runtime.BuildFrameView();
    EXPECT_TRUE(after.Logic.Contains(partition));
    runtime.EndFrameView();
}

TEST(UnifiedRuntimeWorld, RequestsRaisedDuringResidencyApplyNextDrain)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);

    runtime.AttachZone(ZoneId{ 1 }, ZoneParticipation{});
    const auto attached = runtime.BeginResidencyProcessing();
    ASSERT_EQ(attached.size(), 1u);

    ASSERT_TRUE(runtime.RequestParticipation(ZoneId{ 1 }, LogicOnly()));
    EXPECT_TRUE(runtime.PendingResidencyChanges().empty());
    runtime.FinalizeResidencyProcessing();

    runtime.FlushLifecycleRequests();
    const auto next = runtime.BeginResidencyProcessing();
    ASSERT_EQ(next.size(), 1u);
    EXPECT_EQ(next[0].Kind, ZoneResidencyChangeKind::ParticipationChanged);
    EXPECT_EQ(next[0].Current, LogicOnly());
    runtime.FinalizeResidencyProcessing();
}

TEST(UnifiedRuntimeWorld, ParticipationRoundTripWithinOneDrainEmitsNothing)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);

    runtime.AttachZone(ZoneId{ 1 }, LogicOnly());
    ProcessResidency(runtime);

    ASSERT_TRUE(runtime.RequestParticipation(ZoneId{ 1 }, PhysicsAndVisible()));
    ASSERT_TRUE(runtime.RequestParticipation(ZoneId{ 1 }, LogicOnly()));
    runtime.FlushLifecycleRequests();

    EXPECT_TRUE(runtime.PendingResidencyChanges().empty());
}

TEST(UnifiedRuntimeWorld, DetachFinalVisitPrecedesEntityAndZoneResourceDestruction)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);
    RuntimeWorldHookResource& hookResource =
        runtime.Entities().AddResource<RuntimeWorldHookResource>();

    RuntimeZoneRecord& zone = runtime.AttachZone(ZoneId{ 1 }, LogicOnly());
    const StoragePartitionId partition = zone.Partition;
    int zoneResourceDestructions = 0;
    zone.Resources.Register<RuntimeZoneResourceProbe>(&zoneResourceDestructions);

    const EntityId entity = runtime.Entities().CreateEntity(partition);
    runtime.Entities().AddComponent<RuntimeWorldTracked>(entity, { 1 });
    const EntityId persistent = runtime.Entities().CreateEntity();
    runtime.Entities().AddComponent<RuntimeWorldValue>(persistent, { 2 });
    ProcessResidency(runtime);

    ASSERT_TRUE(runtime.RequestDetach(ZoneId{ 1 }));
    runtime.FlushLifecycleRequests();

    EXPECT_FALSE(runtime.IsZoneResident(ZoneId{ 1 }));
    ASSERT_NE(runtime.FindZone(ZoneId{ 1 }), nullptr);
    EXPECT_EQ(zoneResourceDestructions, 0);
    EXPECT_TRUE(runtime.Entities().IsAlive(entity));

    const auto changes = runtime.BeginResidencyProcessing();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, ZoneResidencyChangeKind::Detaching);
    EXPECT_EQ(changes[0].Partition, partition);
    ASSERT_NE(runtime.FindZone(ZoneId{ 1 }), nullptr);
    EXPECT_TRUE(runtime.FindZone(ZoneId{ 1 })
                    ->Resources.Has<RuntimeZoneResourceProbe>());

    runtime.FinalizeResidencyProcessing();

    EXPECT_EQ(runtime.FindZone(ZoneId{ 1 }), nullptr);
    EXPECT_FALSE(runtime.Entities().IsAlive(entity));
    EXPECT_TRUE(runtime.Entities().IsAlive(persistent));
    EXPECT_EQ(hookResource.RemoveCalls, 1);
    EXPECT_EQ(zoneResourceDestructions, 1);
}

TEST(UnifiedRuntimeWorld, AttachThenDetachBeforeFirstResidencyStillGetsFinalVisit)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);

    RuntimeZoneRecord& zone = runtime.AttachZone(
        ZoneId{ 1 },
        PhysicsAndVisible());
    const StoragePartitionId partition = zone.Partition;
    const EntityId entity = runtime.Entities().CreateEntity(partition);
    runtime.Entities().AddComponent<RuntimeWorldTracked>(entity, { 1 });

    ASSERT_TRUE(runtime.RequestDetach(ZoneId{ 1 }));
    runtime.FlushLifecycleRequests();

    const auto changes = runtime.BeginResidencyProcessing();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, ZoneResidencyChangeKind::Detaching);
    EXPECT_EQ(changes[0].Previous, PhysicsAndVisible());
    EXPECT_FALSE(changes[0].Current.Any());
    runtime.FinalizeResidencyProcessing();

    EXPECT_FALSE(runtime.Entities().IsAlive(entity));
    EXPECT_EQ(runtime.FindZone(ZoneId{ 1 }), nullptr);
}

TEST(UnifiedRuntimeWorld, PartitionSlotReusesOnlyAfterDetachFinalization)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);

    const StoragePartitionId first =
        runtime.AttachZone(ZoneId{ 1 }, LogicOnly()).Partition;
    ProcessResidency(runtime);

    ASSERT_TRUE(runtime.RequestDetach(ZoneId{ 1 }));
    runtime.FlushLifecycleRequests();

    RuntimeZoneRecord& beforeFinalization =
        runtime.AttachZone(ZoneId{ 2 }, LogicOnly());
    EXPECT_NE(beforeFinalization.Partition, first);

    ProcessResidency(runtime);

    RuntimeZoneRecord& afterFinalization =
        runtime.AttachZone(ZoneId{ 3 }, LogicOnly());
    EXPECT_EQ(afterFinalization.Partition, first);
}

TEST(UnifiedRuntimeWorld, ZoneResourcesAreIndependent)
{
    const WorldComponentSchema schema = MakeRuntimeSchema();
    RuntimeWorld runtime(schema);

    RuntimeZoneRecord& first = runtime.AttachZone(ZoneId{ 1 });
    RuntimeZoneRecord& second = runtime.AttachZone(ZoneId{ 2 });

    int firstDestroyed = 0;
    int secondDestroyed = 0;
    first.Resources.Register<RuntimeZoneResourceProbe>(&firstDestroyed);
    second.Resources.Register<RuntimeZoneResourceProbe>(&secondDestroyed);

    EXPECT_NE(
        first.Resources.TryGet<RuntimeZoneResourceProbe>(),
        second.Resources.TryGet<RuntimeZoneResourceProbe>());
    EXPECT_EQ(firstDestroyed, 0);
    EXPECT_EQ(secondDestroyed, 0);
}
