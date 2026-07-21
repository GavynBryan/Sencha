#include <zone/ZonePackageImporter.h>

#include <core/serialization/JsonArchive.h>
#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZoneLoadPackage.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
void SetError(ZoneImportError* error, std::string message)
{
    if (error != nullptr)
        error->Message = std::move(message);
}

bool SeedDerivedTransform(
    World& world,
    EntityId entity,
    const ZonePackageComponent& component)
{
    if (component.Type != ResolveComponentTypeId<LocalTransform>())
        return true;
    if (component.RuntimeBytes.size() != sizeof(LocalTransform))
        return false;

    LocalTransform local{};
    std::memcpy(
        &local,
        component.RuntimeBytes.data(),
        sizeof(LocalTransform));
    if (!world.HasComponent<WorldTransform>(entity))
    {
        world.AddComponent<WorldTransform>(
            entity,
            WorldTransform{ local.Value });
    }
    return true;
}

bool ImportComponent(
    World& world,
    EntityId entity,
    const WorldComponentSchema& schema,
    const ZonePackageComponent& component,
    const ComponentSerializerRegistry* serializers,
    SceneSerializationContext* sceneContext,
    std::string& failure)
{
    const WorldComponentSchema::Entry* entry = schema.Find(component.Type);
    if (entry == nullptr)
    {
        failure =
            "Package contains a component absent from the runtime schema.";
        return false;
    }

    if (component.SerializedJson.has_value())
    {
        if (serializers == nullptr || sceneContext == nullptr)
        {
            failure =
                "Package contains serialized data without an import context.";
            return false;
        }

        IComponentSerializer* serializer =
            serializers->FindByType(component.Type);
        if (serializer == nullptr)
        {
            failure =
                "Package serialized component has no registered decoder.";
            return false;
        }

        JsonReadArchive archive(*component.SerializedJson);
        if (!serializer->LoadIntoWorld(
                archive,
                entity,
                world,
                *sceneContext)
            || !archive.Ok())
        {
            failure = "Package serialized component decode failed.";
            return false;
        }
        return true;
    }

    if (entry->Size != component.RuntimeBytes.size())
    {
        failure =
            "Package component byte size does not match the runtime schema.";
        return false;
    }
    if (!schema.ImportComponent(
            world,
            entity,
            component.Type,
            component.RuntimeBytes))
    {
        failure = "Package component import failed.";
        return false;
    }
    if (!SeedDerivedTransform(world, entity, component))
    {
        failure = "Package LocalTransform payload is invalid.";
        return false;
    }
    return true;
}

bool ImportPackageIntoPartitionImpl(
    World& world,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    StoragePartitionId partition,
    const ComponentSerializerRegistry* serializers,
    SceneSerializationContext* sceneContext,
    ZoneImportError* error)
{
    std::vector<EntityId> entities;
    entities.reserve(package.EntityCount());

    const auto fail = [&](std::string message)
    {
        for (auto it = entities.rbegin(); it != entities.rend(); ++it)
            if (world.IsAlive(*it))
                world.DestroyEntity(*it);
        SetError(error, std::move(message));
        return false;
    };

    for (const ZonePackageEntity& packageEntity : package.Entities())
    {
        const EntityId entity = world.CreateEntity(partition);
        entities.push_back(entity);

        for (const ZonePackageComponent& component : packageEntity.Components)
        {
            std::string failure;
            if (!ImportComponent(
                    world,
                    entity,
                    schema,
                    component,
                    serializers,
                    sceneContext,
                    failure))
            {
                return fail(std::move(failure));
            }
        }
    }

    for (const ZonePackageParent& relation : package.Parents())
    {
        if (!package.ContainsEntity(relation.Child)
            || !package.ContainsEntity(relation.Parent))
        {
            return fail("Package hierarchy references an unknown entity.");
        }

        const EntityId child = entities[relation.Child.Value];
        const EntityId parent = entities[relation.Parent.Value];
        if (Parent* existing = world.TryGet<Parent>(child))
            existing->Entity = parent;
        else
            world.AddComponent<Parent>(child, Parent{ parent });
    }

    if (error != nullptr)
        error->Message.clear();
    return true;
}

bool ImportZonePackageImpl(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    const ComponentSerializerRegistry* serializers,
    SceneSerializationContext* sceneContext,
    bool publish,
    ZoneParticipation participation,
    ZoneImportError* error)
{
    if (!package.Zone().IsValid())
    {
        SetError(error, "Zone package has an invalid ZoneId.");
        return false;
    }
    if (runtime.FindZone(package.Zone()) != nullptr)
    {
        SetError(
            error,
            "Zone package targets an already loaded or importing zone.");
        return false;
    }

    RuntimeZoneRecord& importing =
        runtime.BeginZoneImport(package.Zone());
    if (!ImportPackageIntoPartitionImpl(
            runtime.Entities(),
            schema,
            package,
            importing.Partition,
            serializers,
            sceneContext,
            error))
    {
        const bool cancelled = runtime.CancelZoneImport(package.Zone());
        (void)cancelled;
        return false;
    }

    if (publish
        && !runtime.PublishZone(package.Zone(), participation))
    {
        const bool cancelled = runtime.CancelZoneImport(package.Zone());
        (void)cancelled;
        SetError(
            error,
            "Zone package could not publish its hidden import partition.");
        return false;
    }

    if (error != nullptr)
        error->Message.clear();
    return true;
}
} // namespace

bool ImportPackageIntoPartition(
    World& world,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    StoragePartitionId partition,
    ZoneImportError* error)
{
    return ImportPackageIntoPartitionImpl(
        world,
        schema,
        package,
        partition,
        nullptr,
        nullptr,
        error);
}

bool ImportPackageIntoPartition(
    World& world,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    StoragePartitionId partition,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneImportError* error)
{
    return ImportPackageIntoPartitionImpl(
        world,
        schema,
        package,
        partition,
        &serializers,
        &sceneContext,
        error);
}

bool ImportZonePackageHidden(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    ZoneImportError* error)
{
    return ImportZonePackageImpl(
        runtime,
        schema,
        package,
        nullptr,
        nullptr,
        false,
        ZoneParticipation{},
        error);
}

bool ImportZonePackageHidden(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneImportError* error)
{
    return ImportZonePackageImpl(
        runtime,
        schema,
        package,
        &serializers,
        &sceneContext,
        false,
        ZoneParticipation{},
        error);
}

bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    ZoneParticipation participation,
    ZoneImportError* error)
{
    return ImportZonePackageImpl(
        runtime,
        schema,
        package,
        nullptr,
        nullptr,
        true,
        participation,
        error);
}

bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    ZoneParticipation participation,
    ZoneImportError* error)
{
    return ImportZonePackageImpl(
        runtime,
        schema,
        package,
        &serializers,
        &sceneContext,
        true,
        participation,
        error);
}
