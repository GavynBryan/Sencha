#include "DerivedComponents.h"

#include <ecs/World.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <algorithm>
#include <span>

namespace
{
    // Which component on the entity declared it owes `type`. First in id order,
    // so two owners of the same column resolve the same way every frame rather
    // than by whichever the scan reached first this time.
    ComponentId OwnerOf(const World& world,
                        const std::vector<ComponentId>& carried,
                        ComponentTypeId type)
    {
        for (const ComponentId candidate : carried)
        {
            const std::span<const ComponentTypeId> owed =
                world.DeclaredOwedComponents(candidate);
            if (std::find(owed.begin(), owed.end(), type) != owed.end())
                return candidate;
        }
        return InvalidComponentId;
    }
}

std::vector<DerivedComponentRow> DerivedComponentsOn(
    const World& world,
    const ComponentSerializerRegistry& serializers,
    EntityId entity)
{
    std::vector<ComponentId> carried;
    world.ComponentIdsOn(entity, carried);

    std::vector<DerivedComponentRow> rows;
    for (const ComponentId id : carried)
    {
        const ComponentMeta* meta = world.GetMeta(id);
        if (meta == nullptr)
            continue;
        // The authored surface is exactly what a serializer describes. Anything
        // the registry knows belongs in the rows above this group, whether or
        // not the file happened to mention it.
        if (serializers.FindByType(meta->TypeId) != nullptr)
            continue;

        rows.push_back(DerivedComponentRow{
            id, meta->Name, OwnerOf(world, carried, meta->TypeId) });
    }
    return rows;
}
