#include <navigation/NavLinkComponent.h>

#include <world/serialization/StableIdFieldCodec.h>

bool SceneFieldCodec<NavLinkId>::Save(IWriteArchive& archive, std::string_view key,
                                      NavLinkId value, SceneSerializationContext&)
{
    return SaveStableIdField(archive, key, value, NavLinkIdToString);
}

bool SceneFieldCodec<NavLinkId>::Load(IReadArchive& archive, std::string_view key,
                                      NavLinkId& value, SceneSerializationContext&)
{
    return LoadStableIdField(archive, key, value, NavLinkIdFromString, NavLinkIdToString);
}
