#include <world/identity/PersistentIdComponent.h>

#include <core/serialization/Archive.h>

#include <string>

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
    // Authored scenes must preserve unset identities so the editor can backfill
    // them on load rather than fail the parse.
    if (text == "0000000000000000")
    {
        value = PersistentEntityId{};
        return true;
    }
    const auto parsed = PersistentEntityIdFromString(text);
    if (!parsed)
    {
        archive.MarkInvalidField(key);
        return false;
    }
    value = *parsed;
    return true;
}
