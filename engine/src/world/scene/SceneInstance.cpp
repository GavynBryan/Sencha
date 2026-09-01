#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceSchema.h>

#include <world/scene/SceneInstanceIndex.h>

#include <string>

void ComponentTraits<SceneInstance>::OnAdd(SceneInstance& component, World& world,
                                           EntityId entity)
{
    if (!component.Id.IsValid())
        return;
    if (auto* index = world.TryGetResource<SceneInstanceIndex>())
        index->Register(component.Id, entity);
}

void ComponentTraits<SceneInstance>::OnRemove(const SceneInstance& component,
                                              World& world, EntityId entity)
{
    if (!component.Id.IsValid())
        return;
    if (auto* index = world.TryGetResource<SceneInstanceIndex>())
        index->Unregister(component.Id, entity);
}

bool SceneFieldCodec<SceneInstanceId>::Save(IWriteArchive& archive,
                                            std::string_view key,
                                            SceneInstanceId value,
                                            SceneSerializationContext&)
{
    archive.Field(key, std::string_view(SceneInstanceIdToString(value)));
    return archive.Ok();
}

bool SceneFieldCodec<SceneInstanceId>::Load(IReadArchive& archive,
                                            std::string_view key,
                                            SceneInstanceId& value,
                                            SceneSerializationContext&)
{
    std::string text;
    archive.Field(key, text);
    if (!archive.Ok())
        return false;
    const auto parsed = SceneInstanceIdFromString(text);
    if (!parsed)
    {
        archive.MarkInvalidField(key);
        return false;
    }
    value = *parsed;
    return true;
}

bool SceneFieldCodec<AssetId>::Save(IWriteArchive& archive, std::string_view key,
                                    AssetId value, SceneSerializationContext&)
{
    archive.Field(key, std::string_view(AssetIdToString(value)));
    return archive.Ok();
}

bool SceneFieldCodec<AssetId>::Load(IReadArchive& archive, std::string_view key,
                                    AssetId& value, SceneSerializationContext&)
{
    // The cook-stamped ref object carries the id directly.
    if (archive.IsObject(key))
    {
        archive.BeginObject(key);
        std::string idText;
        archive.Field(std::string_view{ "id" }, idText);
        archive.End();
        if (!archive.Ok())
            return false;
        const std::optional<AssetId> parsed = AssetIdFromString(idText);
        if (!parsed.has_value())
        {
            archive.MarkInvalidField(key);
            return false;
        }
        value = *parsed;
        return true;
    }

    std::string text;
    archive.Field(key, text);
    if (!archive.Ok())
        return false;

    // A bare asset:// path means the cook had no id for it, and all-zero hex
    // is how an invalid id round-trips; both leave the field invalid rather
    // than failing content that is otherwise loadable.
    if (text.starts_with("asset://") || text == "0000000000000000")
    {
        value = AssetId{};
        return true;
    }

    const std::optional<AssetId> parsed = AssetIdFromString(text);
    if (!parsed.has_value())
    {
        archive.MarkInvalidField(key);
        return false;
    }
    value = *parsed;
    return true;
}
