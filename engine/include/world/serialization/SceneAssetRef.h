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
// A reference reads in three shapes and writes in one. Authoring writes a bare
// path string and that is what a hand-edited scene contains. The cook rewrites
// it as {"id": "<hex>", "path": "..."} so a stamped reference survives a rename
// the path predates; the id wins where the registry knows it, and the path is
// the fallback. A third, older {"type": ..., "path": ...} form still reads.
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

// Writes the bare-path form. An empty path is refused: a field that names an
// asset the process cannot name back is a reference about to be lost.
[[nodiscard]] bool WriteSceneAssetRef(IWriteArchive& archive,
                                      std::string_view key,
                                      std::string_view path,
                                      SceneSerializationContext& context);
