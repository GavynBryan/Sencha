#pragma once

#include <core/assets/AssetRef.h>

#include <string>
#include <string_view>

struct IReadArchive;
struct IWriteArchive;
struct SceneSerializationContext;

//=============================================================================
// SceneAssetRef
//
// How a scene field names an asset, in one place, because more than one kind of
// serializer needs it: the schema-driven field codecs resolve typed handles
// through it, and a component whose persisted form is hand-written resolves its
// own the same way.
//
// A reference is read in AssetRefCodec's form: a bare path, or {"id", "path"}
// where the id wins when the registry knows it and the path is the fallback.
//
// Reading is strict: an unresolvable reference is a scene that refuses to load
// rather than an entity quietly missing what it names.
//=============================================================================

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
