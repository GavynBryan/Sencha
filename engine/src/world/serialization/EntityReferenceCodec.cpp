#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/SceneFieldCodec.h>

#include <ecs/World.h>
#include <world/identity/PendingReferences.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>

#include <cstddef>
#include <string>

bool SceneFieldCodec<EntityId>::Save(IWriteArchive& archive, std::string_view key, const EntityId& value,
                                     SceneSerializationContext& context)
{
    const World* world = context.Entities;
    const PersistentIdComponent* identity = world != nullptr && world->IsAlive(value)
            && world->IsRegistered<PersistentIdComponent>()
        ? world->TryGet<PersistentIdComponent>(value)
        : nullptr;
    if (identity != nullptr && identity->Id.IsValid())
        archive.Field(key, std::string_view(PersistentEntityIdToString(identity->Id)));
    return archive.Ok();
}

bool SceneFieldCodec<EntityId>::Load(IReadArchive& archive, std::string_view key, EntityId& value,
                                     SceneSerializationContext& context)
{
    std::string text;
    archive.Field(key, text);
    if (!archive.Ok())
        return false;
    value = EntityId{};
    const std::optional<PersistentEntityId> target = PersistentEntityIdFromString(text);
    if (!target.has_value() || !target->IsValid())
    {
        archive.MarkInvalidField(key);
        return false;
    }
    const World* world = context.Entities;
    const PersistentEntityIndex* index = world != nullptr ? world->TryGetResource<PersistentEntityIndex>() : nullptr;
    const EntityId resident = index != nullptr ? index->TryResolve(*target) : EntityId{};
    if (world != nullptr && world->IsAlive(resident))
        value = resident;
    else if (context.Unresolved != nullptr)
        context.Unresolved->push_back(UnresolvedEntityReference{ .Field = &value, .Target = *target });
    return true;
}

void SceneComponentSerialization::AwaitEntityReferences(World& world, EntityId owner, ComponentTypeId type,
                                                        const void* component,
                                                        std::span<const UnresolvedEntityReference> unresolved)
{
    PendingReferences& pending = world.HasResource<PendingReferences>() ? world.GetResource<PendingReferences>()
                                                                         : world.AddResource<PendingReferences>();
    for (const UnresolvedEntityReference& reference : unresolved)
    {
        const std::ptrdiff_t offset =
            reinterpret_cast<const std::byte*>(reference.Field) - static_cast<const std::byte*>(component);
        pending.Await(owner, type, static_cast<std::uint32_t>(offset), reference.Target);
    }
}
