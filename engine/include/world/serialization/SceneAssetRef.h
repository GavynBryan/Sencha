#pragma once

#include <core/assets/AssetRef.h>

#include <string>
#include <string_view>

struct IReadArchive;
struct IWriteArchive;
struct SceneSerializationContext;

// How a scene field names an asset, for schema codecs and hand-written ones alike:
// AssetRefCodec's form, the id winning when the registry knows it. Strict: an
// unresolvable reference refuses the scene rather than dropping what it names.

// Resolves whatever shape `key` holds into a virtual path. The archive scope
// containing `key` must be open.
[[nodiscard]] bool ReadSceneAssetRef(IReadArchive& archive,
                                     std::string_view key,
                                     AssetType expected,
                                     std::string& outPath,
                                     SceneSerializationContext& context);

// Writes the asset at `path` by its id and path when the registry knows it, by
// path alone otherwise. An empty path is refused: a field that names an asset
// the process cannot name back is a reference about to be lost.
[[nodiscard]] bool WriteSceneAssetRef(IWriteArchive& archive,
                                      std::string_view key,
                                      AssetType type,
                                      std::string_view path,
                                      SceneSerializationContext& context);

// Read reserves, so a reference read before its definition is declared keeps the name.
template<typename Registry, typename Id>
[[nodiscard]] bool WriteSceneDefinitionRef(IWriteArchive& archive, std::string_view key, const Registry* registry,
                                           Id id, SceneSerializationContext& context)
{
    const std::string_view path = registry != nullptr ? registry->GetName(id) : std::string_view{};
    return path.empty() || WriteSceneAssetRef(archive, key, AssetType::Data, path, context);
}

template<typename Registry, typename Id>
[[nodiscard]] bool ReadSceneDefinitionRef(IReadArchive& archive, std::string_view key, Registry* registry, Id& id,
                                          SceneSerializationContext& context)
{
    std::string path;
    if (!ReadSceneAssetRef(archive, key, AssetType::Data, path, context))
        return false;
    id = registry != nullptr ? registry->Reserve(path) : Id{};
    return id.IsValid();
}
