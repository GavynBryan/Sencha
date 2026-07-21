#include <zone/ZonePackageImporter.h>

#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
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

    if (component.Bytes.size() != sizeof(LocalTransform))
        return false;

    LocalTransform local{};
    std::memcpy(&local, component.Bytes.data(), sizeof(LocalTransform));
    if (!world.HasComponent<WorldTransform>(entity))
    {
        world.AddComponent<WorldTransform>(
            entity,
            WorldTransform{ local.Value });
    }
    return true;
}
} // namespace

bool ImportZonePackage(
    RuntimeWorld& runtime,
    const WorldComponentSchema& schema,
    const ZoneLoadPackage& package,
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
        SetError(error, "Zone package targets an already loaded or importing zone.");
        return false;
    }

    RuntimeZoneRecord& importing = runtime.BeginZoneImport(package.Zone());
    World& world = runtime.Entities();

    std::vector<EntityId> entities;
    entities.reserve(package.EntityCount());

    const auto fail = [&](std::string message) {
        const bool cancelled = runtime.CancelZoneImport(package.Zone());
        (void)cancelled;
        SetError(error, std::move(message));
        return false;
    };

    for (const ZonePackageEntity& packageEntity : package.Entities())
    {
        const EntityId entity = world.CreateEntity(importing.Partition);
        entities.push_back(entity);

        for (const ZonePackageComponent& component : packageEntity.Components)
        {
            const WorldComponentSchema::Entry* entry = schema.Find(component.Type);
            if (entry == nullptr)
            {
                return fail(
                    "Zone package contains a component absent from the runtime schema.");
            }
            if (entry->Size != component.Bytes.size())
            {
                return fail(
                    "Zone package component byte size does not match the runtime schema.");
            }
            if (!schema.ImportComponent(
                    world,
                    entity,
                    component.Type,
                    component.Bytes))
            {
                return fail("Zone package component import failed.");
            }
            if (!SeedDerivedTransform(world, entity, component))
            {
                return fail("Zone package LocalTransform payload is invalid.");
            }
        }
    }

    for (const ZonePackageParent& relation : package.Parents())
    {
        if (!package.ContainsEntity(relation.Child)
            || !package.ContainsEntity(relation.Parent))
        {
            return fail("Zone package hierarchy references an unknown entity.");
        }

        const EntityId child = entities[relation.Child.Value];
        const EntityId parent = entities[relation.Parent.Value];
        if (Parent* existing = world.TryGet<Parent>(child))
            existing->Entity = parent;
        else
            world.AddComponent<Parent>(child, Parent{ parent });
    }

    if (!runtime.PublishZone(package.Zone(), participation))
        return fail("Zone package could not publish its hidden import partition.");

    if (error != nullptr)
        error->Message.clear();
    return true;
}
