#include <world/identity/PersistentIdComponent.h>

#include <core/serialization/Archive.h>
#include <ecs/World.h>
#include <world/identity/PersistentEntityIndex.h>

#include <string>

void ComponentTraits<PersistentIdComponent>::OnAdd(PersistentIdComponent& component,
                                                   World& world, EntityId entity)
{
    if (!component.Id.IsValid())
        return;
    if (auto* index = world.TryGetResource<PersistentEntityIndex>())
        (void)index->Register(component.Id, entity);
}

void ComponentTraits<PersistentIdComponent>::OnRemove(const PersistentIdComponent& component,
                                                      World& world, EntityId entity)
{
    if (!component.Id.IsValid())
        return;
    if (auto* index = world.TryGetResource<PersistentEntityIndex>())
        index->Unregister(component.Id, entity);
}

bool SceneFieldCodec<PersistentEntityId>::Save(IWriteArchive& archive, std::string_view key,
                                               PersistentEntityId value,
                                               SceneSerializationContext&)
{
    archive.Field(key, std::string_view(PersistentEntityIdToString(value)));
    return archive.Ok();
}

bool SceneFieldCodec<PersistentEntityId>::Load(IReadArchive& archive, std::string_view key,
                                               PersistentEntityId& value,
                                               SceneSerializationContext&)
{
    std::string text;
    archive.Field(key, text);
    if (!archive.Ok())
        return false;
    const auto parsed = PersistentEntityIdFromString(text);
    if (!parsed)
    {
        archive.MarkInvalidField(key);
        return false;
    }
    value = *parsed;
    return true;
}
