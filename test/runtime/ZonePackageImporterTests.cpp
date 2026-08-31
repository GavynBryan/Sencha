#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <world/build/EntityBuildPackage.h>
#include <zone/ZonePackageImporter.h>

#include <gtest/gtest.h>

struct PackageValue
{
    int Value = 0;
};

struct PackageOnAdd
{
    int Value = 0;
};

struct PackageUnknown
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(PackageValue, "test.package_value");
SENCHA_DECLARE_COMPONENT_TYPE(PackageOnAdd, "test.package_on_add");
SENCHA_DECLARE_COMPONENT_TYPE(PackageUnknown, "test.package_unknown");

namespace
{
int PackageOnAddCalls = 0;
}

template <>
struct ComponentTraits<PackageOnAdd>
{
    static void OnAdd(PackageOnAdd&, World&, EntityId)
    {
        ++PackageOnAddCalls;
    }
};

namespace
{
WorldComponentSchema MakePackageSchema()
{
    WorldComponentSchema schema;
    schema.Add<LocalTransform>();
    schema.Add<WorldTransform>();
    schema.Add<Parent>();
    schema.Add<PackageValue>();
    schema.Add<PackageOnAdd>();
    schema.Seal();
    return schema;
}

ZoneParticipation LogicOnly()
{
    ZoneParticipation participation;
    participation.Logic = true;
    return participation;
}

std::vector<EntityId> EntitiesInPartition(
    const World& world,
    StoragePartitionId partition)
{
    std::vector<EntityId> result;
    for (EntityId entity : world.GetAliveEntities())
        if (world.GetEntityPartition(entity) == partition)
            result.push_back(entity);
    return result;
}
} // namespace

TEST(ZonePackageImporter, PublishesComponentsHierarchyAndDerivedTransforms)
{
    const WorldComponentSchema schema = MakePackageSchema();
    RuntimeWorld runtime(schema);

    EntityBuildPackage package;
    const PackageEntityId root = package.CreateEntity();
    const PackageEntityId child = package.CreateEntity();

    LocalTransform rootTransform;
    rootTransform.Value.Position = Vec3d{ 1.0f, 2.0f, 3.0f };
    LocalTransform childTransform;
    childTransform.Value.Position = Vec3d{ 4.0f, 5.0f, 6.0f };

    ASSERT_TRUE(package.AddComponent(root, rootTransform));
    ASSERT_TRUE(package.AddComponent(root, PackageValue{ 11 }));
    ASSERT_TRUE(package.AddComponent(child, childTransform));
    ASSERT_TRUE(package.AddComponent(child, PackageValue{ 22 }));
    ASSERT_TRUE(package.AddComponent(child, PackageOnAdd{ 7 }));
    ASSERT_TRUE(package.SetParent(child, root));

    PackageOnAddCalls = 0;
    ZoneImportError error;
    ASSERT_TRUE(ImportZonePackage(
        runtime,
        schema,
        ZoneId{ 10 },
        package,
        LogicOnly(),
        &error)) << error.Message;

    RuntimeZoneRecord* zone = runtime.FindZone(ZoneId{ 10 });
    ASSERT_NE(zone, nullptr);
    EXPECT_EQ(zone->State, RuntimeZoneLoadState::Resident);
    EXPECT_EQ(zone->Participation, LogicOnly());
    EXPECT_EQ(PackageOnAddCalls, 1);

    const auto changes = runtime.BeginResidencyProcessing();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, ZoneResidencyChangeKind::Attached);
    EXPECT_EQ(changes[0].Partition, zone->Partition);
    runtime.FinalizeResidencyProcessing();

    const std::vector<EntityId> entities =
        EntitiesInPartition(runtime.Entities(), zone->Partition);
    ASSERT_EQ(entities.size(), 2u);

    EntityId rootEntity;
    EntityId childEntity;
    for (EntityId entity : entities)
    {
        const PackageValue* value =
            runtime.Entities().TryGet<PackageValue>(entity);
        ASSERT_NE(value, nullptr);
        if (value->Value == 11)
            rootEntity = entity;
        else if (value->Value == 22)
            childEntity = entity;
    }

    ASSERT_TRUE(rootEntity.IsValid());
    ASSERT_TRUE(childEntity.IsValid());
    EXPECT_NE(
        runtime.Entities().TryGet<WorldTransform>(rootEntity),
        nullptr);
    EXPECT_NE(
        runtime.Entities().TryGet<WorldTransform>(childEntity),
        nullptr);

    const Parent* parent =
        runtime.Entities().TryGet<Parent>(childEntity);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(parent->Entity, rootEntity);

    const FrameZoneView& view = runtime.BuildFrameView();
    EXPECT_TRUE(view.Resident.Contains(zone->Partition));
    EXPECT_TRUE(view.Logic.Contains(zone->Partition));
    runtime.EndFrameView();
}

TEST(ZonePackageImporter, UnknownComponentRollsBackWholeHiddenPartition)
{
    const WorldComponentSchema schema = MakePackageSchema();
    RuntimeWorld runtime(schema);

    EntityBuildPackage package;
    const PackageEntityId entity = package.CreateEntity();
    ASSERT_TRUE(package.AddComponent(entity, PackageValue{ 1 }));
    ASSERT_TRUE(package.AddComponent(entity, PackageUnknown{ 2 }));

    ZoneImportError error;
    EXPECT_FALSE(ImportZonePackage(
        runtime,
        schema,
        ZoneId{ 20 },
        package,
        {},
        &error));
    EXPECT_FALSE(error.Message.empty());
    EXPECT_EQ(runtime.FindZone(ZoneId{ 20 }), nullptr);
    EXPECT_EQ(runtime.Entities().EntityCount(), 0u);
    EXPECT_TRUE(runtime.PendingResidencyChanges().empty());
}

TEST(ZonePackageImporter, ImportsPersistentWorldSceneIntoPartitionZero)
{
    const WorldComponentSchema schema = MakePackageSchema();
    RuntimeWorld runtime(schema);

    const EntityId existing = runtime.Entities().CreateEntity();
    runtime.Entities().AddComponent<PackageValue>(
        existing,
        PackageValue{ 5 });

    EntityBuildPackage package;
    const PackageEntityId first = package.CreateEntity();
    const PackageEntityId second = package.CreateEntity();
    ASSERT_TRUE(package.AddComponent(first, PackageValue{ 11 }));
    ASSERT_TRUE(package.AddComponent(second, PackageValue{ 22 }));
    ASSERT_TRUE(package.SetParent(second, first));

    ZoneImportError error;
    ASSERT_TRUE(ImportPackageIntoPartition(
        runtime.Entities(),
        schema,
        package,
        PersistentStoragePartition,
        ZoneId{ 30 },
        &error)) << error.Message;

    EXPECT_TRUE(runtime.Entities().IsAlive(existing));
    const std::vector<EntityId> persistent = EntitiesInPartition(
        runtime.Entities(),
        PersistentStoragePartition);
    EXPECT_EQ(persistent.size(), 3u);
    EXPECT_TRUE(runtime.PendingResidencyChanges().empty());
}

TEST(ZonePackageImporter, PersistentImportFailurePreservesExistingEntities)
{
    const WorldComponentSchema schema = MakePackageSchema();
    RuntimeWorld runtime(schema);

    const EntityId existing = runtime.Entities().CreateEntity();
    runtime.Entities().AddComponent<PackageValue>(
        existing,
        PackageValue{ 5 });

    EntityBuildPackage package;
    const PackageEntityId valid = package.CreateEntity();
    const PackageEntityId invalid = package.CreateEntity();
    ASSERT_TRUE(package.AddComponent(valid, PackageValue{ 11 }));
    ASSERT_TRUE(package.AddComponent(invalid, PackageUnknown{ 22 }));

    ZoneImportError error;
    EXPECT_FALSE(ImportPackageIntoPartition(
        runtime.Entities(),
        schema,
        package,
        PersistentStoragePartition,
        ZoneId{ 40 },
        &error));
    EXPECT_FALSE(error.Message.empty());
    EXPECT_TRUE(runtime.Entities().IsAlive(existing));
    EXPECT_EQ(runtime.Entities().EntityCount(), 1u);
    EXPECT_EQ(
        runtime.Entities().TryGet<PackageValue>(existing)->Value,
        5);
}

TEST(ZonePackageImporter, PackageRejectsDuplicateComponentsAndParents)
{
    EntityBuildPackage package;
    const PackageEntityId first = package.CreateEntity();
    const PackageEntityId second = package.CreateEntity();

    EXPECT_TRUE(package.AddComponent(first, PackageValue{ 1 }));
    EXPECT_FALSE(package.AddComponent(first, PackageValue{ 2 }));
    EXPECT_TRUE(package.SetParent(second, first));
    EXPECT_FALSE(package.SetParent(second, first));
    EXPECT_FALSE(package.SetParent(first, first));
}
