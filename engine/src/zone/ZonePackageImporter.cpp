#include <zone/ZonePackageImporter.h>

#include <core/serialization/JsonArchive.h>
#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZoneLoadPackage.h>
#include <zone/ZoneStateStore.h>

#include <cstddef>
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

// Every path that builds an entity from data owes the derived world transform,
// and there is more than one of them, so the obligation itself lives in the
// transform module. The registration check stays here because whether the
// destination world has the pair at all is a property of the world, resolved
// once per import rather than per entity.
void SeedDerivedTransform(World& world, EntityId entity, bool worldHasTransforms)
{
    if (!worldHasTransforms)
        return;
    SeedDerivedWorldTransform(world, entity);
}

// ZonePackageEntity::PersistentId is import metadata lifted from the entity's
// persistent_id component at package build, and the two decide different things:
// the metadata drives destroyed-entity suppression before the row exists, the
// component drives PersistentEntityIndex registration after it does. A package
// whose two copies disagree would suppress under one identity and resolve under
// another, so agreement is checked rather than assumed. A world that never
// registered the component (minimal fixtures) reads as no identity, which only
// agrees with metadata that is likewise unset.
bool PersistentIdentityAgrees(const World& world,
                              EntityId entity,
                              PersistentEntityId metadata)
{
    PersistentEntityId component;
    if (world.IsRegistered<PersistentIdComponent>())
        if (const auto* id = world.TryGet<PersistentIdComponent>(entity))
            component = id->Id;
    return component == metadata;
}

// Every column the entity will carry, so its row is built once. Declared
// component types, plus the derived WorldTransform, plus Parent only when this
// entity actually has a parent — an unwritten Parent column would silently
// reparent the entity to a null id.
bool BuildEntitySignature(
    const World& world,
    const WorldComponentSchema& schema,
    const ZonePackageEntity& packageEntity,
    bool parented,
    ArchetypeSignature& signature,
    std::string& failure)
{
    signature.reset();
    bool hasLocalTransform = false;

    for (const ZonePackageComponent& component : packageEntity.Components)
    {
        if (schema.Find(component.Type) == nullptr)
        {
            failure =
                "Package contains a component absent from the runtime schema.";
            return false;
        }

        const ComponentId id = world.GetComponentIdByType(component.Type);
        if (id == InvalidComponentId)
        {
            failure =
                "Package component is not registered in the destination world.";
            return false;
        }
        signature.set(id);
        hasLocalTransform =
            hasLocalTransform
            || component.Type == ResolveComponentTypeId<LocalTransform>();
    }

    if (hasLocalTransform && world.IsRegistered<WorldTransform>())
        signature.set(world.GetComponentId<WorldTransform>());
    if (parented && world.IsRegistered<Parent>())
        signature.set(world.GetComponentId<Parent>());
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
    if (!schema.InitializeComponent(
            world,
            entity,
            component.Type,
            component.RuntimeBytes))
    {
        failure = "Package component import failed.";
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
            if (it->IsValid() && world.IsAlive(*it))
                world.DestroyEntity(*it);
        SetError(error, std::move(message));
        return false;
    };

    // The zone state overlay: entities recorded destroyed in a prior residency
    // are not re-created. Worlds without the store (editor documents, plain
    // test worlds) import the cooked scene verbatim.
    const ZoneStateStore* zoneState = world.TryGetResource<ZoneStateStore>();

    // Which entities are parented has to be known before any row is built, so
    // Parent joins the initial signature instead of costing a later archetype
    // transition per child.
    std::vector<bool> parented(package.EntityCount(), false);
    for (const ZonePackageParent& relation : package.Parents())
    {
        if (!package.ContainsEntity(relation.Child)
            || !package.ContainsEntity(relation.Parent))
        {
            return fail("Package hierarchy references an unknown entity.");
        }
        parented[relation.Child.Value] = true;
    }

    // Resolved once: per entity these are two hash lookups that cannot change
    // mid-import, because component registration is sealed before any entity
    // exists.
    const bool worldHasTransforms =
        world.IsRegistered<LocalTransform>() && world.IsRegistered<WorldTransform>();

    ArchetypeSignature signature;
    std::size_t packageIndex = 0;
    for (const ZonePackageEntity& packageEntity : package.Entities())
    {
        if (zoneState != nullptr && packageEntity.PersistentId.IsValid()
            && zoneState->IsRecordedDestroyed(package.Zone(), packageEntity.PersistentId))
        {
            // The slot stays positionally aligned with the package's local
            // ids; parent relations that touch it are dropped below.
            entities.push_back(EntityId{});
            ++packageIndex;
            continue;
        }

        std::string failure;
        if (!BuildEntitySignature(
                world,
                schema,
                packageEntity,
                parented[packageIndex],
                signature,
                failure))
        {
            return fail(std::move(failure));
        }
        ++packageIndex;

        // One row at its final archetype, then every column written in place.
        // Growing the entity component by component would migrate its row once
        // per component, each migration copying the columns added before it.
        const EntityId entity =
            world.CreateEntityWithSignature(partition, signature);
        entities.push_back(entity);

        for (const ZonePackageComponent& component : packageEntity.Components)
        {
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

        SeedDerivedTransform(world, entity, worldHasTransforms);

        if (!PersistentIdentityAgrees(world, entity, packageEntity.PersistentId))
        {
            return fail("Package entity identity metadata disagrees with its "
                        "persistent_id component.");
        }
    }

    for (const ZonePackageParent& relation : package.Parents())
    {
        const EntityId child = entities[relation.Child.Value];
        const EntityId parent = entities[relation.Parent.Value];
        // A suppressed end leaves the relation unwired: the child imports
        // unparented, matching how destruction leaves a live orphan rather
        // than cascading.
        if (!child.IsValid() || !parent.IsValid())
            continue;
        // Pre-created by BuildEntitySignature whenever Parent is registered;
        // the add is the path for a world that registered it later.
        if (!world.IsRegistered<Parent>()
            || !world.InitializeComponent<Parent>(child, Parent{ parent }))
        {
            world.AddComponent<Parent>(child, Parent{ parent });
        }
    }

    // Successful import records what the cooked scene authored, so the detach
    // capture can diff live state against it.
    if (auto* mutableState = world.TryGetResource<ZoneStateStore>())
    {
        std::vector<PersistentEntityId> authored;
        authored.reserve(package.EntityCount());
        for (const ZonePackageEntity& packageEntity : package.Entities())
            if (packageEntity.PersistentId.IsValid())
                authored.push_back(packageEntity.PersistentId);
        if (!authored.empty())
            mutableState->RecordAuthoredSet(package.Zone(), authored);
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
