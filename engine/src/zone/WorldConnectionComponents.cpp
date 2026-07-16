#include <zone/WorldConnectionComponents.h>

#include <core/serialization/Archive.h>

#include <optional>
#include <string>

namespace
{

template <typename Id, typename ToString>
bool SaveId(IWriteArchive& archive, std::string_view key, Id id, ToString toString)
{
    archive.Field(key, std::string_view(toString(id)));
    return archive.Ok();
}

template <typename Id, typename FromString>
bool LoadId(IReadArchive& archive, std::string_view key, Id& id, FromString fromString)
{
    std::string text;
    archive.Field(key, text);
    if (!archive.Ok())
        return false;
    // Authored scenes must preserve unresolved references and invalid stable
    // identities so the editor can decorate and repair them. Cooked manifests
    // retain strict nonzero ID parsing in WorldPartitionManifest.cpp.
    if (text == "0000000000000000")
    {
        id = Id{};
        return true;
    }
    const std::optional<Id> parsed = fromString(text);
    if (!parsed)
    {
        archive.MarkInvalidField(key);
        return false;
    }
    id = *parsed;
    return true;
}

} // namespace

bool SceneFieldCodec<ZoneId>::Save(IWriteArchive& archive, std::string_view key,
                                   ZoneId value, SceneSerializationContext&)
{
    return SaveId(archive, key, value, ZoneIdToString);
}

bool SceneFieldCodec<ZoneId>::Load(IReadArchive& archive, std::string_view key,
                                   ZoneId& value, SceneSerializationContext&)
{
    return LoadId(archive, key, value, ZoneIdFromString);
}

bool SceneFieldCodec<DockId>::Save(IWriteArchive& archive, std::string_view key,
                                   DockId value, SceneSerializationContext&)
{
    return SaveId(archive, key, value, DockIdToString);
}

bool SceneFieldCodec<DockId>::Load(IReadArchive& archive, std::string_view key,
                                   DockId& value, SceneSerializationContext&)
{
    return LoadId(archive, key, value, DockIdFromString);
}

bool SceneFieldCodec<LinkId>::Save(IWriteArchive& archive, std::string_view key,
                                   LinkId value, SceneSerializationContext&)
{
    return SaveId(archive, key, value, LinkIdToString);
}

bool SceneFieldCodec<LinkId>::Load(IReadArchive& archive, std::string_view key,
                                   LinkId& value, SceneSerializationContext&)
{
    return LoadId(archive, key, value, LinkIdFromString);
}
