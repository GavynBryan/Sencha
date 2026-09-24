#include <zone/WorldConnectionComponents.h>

#include <world/serialization/StableIdFieldCodec.h>

bool SceneFieldCodec<ZoneId>::Save(IWriteArchive& archive, std::string_view key,
                                   ZoneId value, SceneSerializationContext&)
{
    return SaveStableIdField(archive, key, value, ZoneIdToString);
}

bool SceneFieldCodec<ZoneId>::Load(IReadArchive& archive, std::string_view key,
                                   ZoneId& value, SceneSerializationContext&)
{
    return LoadStableIdField(archive, key, value, ZoneIdFromString, ZoneIdToString);
}

bool SceneFieldCodec<DockId>::Save(IWriteArchive& archive, std::string_view key,
                                   DockId value, SceneSerializationContext&)
{
    return SaveStableIdField(archive, key, value, DockIdToString);
}

bool SceneFieldCodec<DockId>::Load(IReadArchive& archive, std::string_view key,
                                   DockId& value, SceneSerializationContext&)
{
    return LoadStableIdField(archive, key, value, DockIdFromString, DockIdToString);
}

bool SceneFieldCodec<LinkId>::Save(IWriteArchive& archive, std::string_view key,
                                   LinkId value, SceneSerializationContext&)
{
    return SaveStableIdField(archive, key, value, LinkIdToString);
}

bool SceneFieldCodec<LinkId>::Load(IReadArchive& archive, std::string_view key,
                                   LinkId& value, SceneSerializationContext&)
{
    return LoadStableIdField(archive, key, value, LinkIdFromString, LinkIdToString);
}
