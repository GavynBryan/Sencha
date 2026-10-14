#pragma once

#include <core/assets/AssetRef.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class JsonValue;
struct IReadArchive;
struct IWriteArchive;

// The one text form of an asset reference: "asset://..." or {"id", "path"}, the
// object whenever the id is known. Reading leaves Type to the caller and the id's
// current path to whoever holds the registry (docs/assets/pipeline.md, Decision A).

// False, with the field marked invalid, for a non-path string, a malformed id,
// or neither id nor path. An object with any other member is not a reference:
// a document stating its own id is not one.
[[nodiscard]] bool ReadAssetRef(IReadArchive& archive, std::string_view key, AssetRef& out);
void WriteAssetRef(IWriteArchive& archive, std::string_view key, const AssetRef& ref);

[[nodiscard]] std::optional<AssetRef> AssetRefFromJson(const JsonValue& value);

// Every reference anywhere in `root`, in either form, in the order met: the
// schema-agnostic walk, for a reader that does not know which fields name
// assets. An object that is a reference is not searched further.
[[nodiscard]] std::vector<AssetRef> CollectAssetRefs(const JsonValue& root);

// Where assets named by id now are: a snapshot an owner hands to work that may
// run on any thread (AssetRegistry::PathsById).
using AssetPathsById = std::unordered_map<AssetId, std::string>;

// `root` with every reference made the path its asset is now at -- the hint,
// when the id is not in `current` -- for readers that know assets by path.
[[nodiscard]] JsonValue ResolveAssetRefs(const JsonValue& root, const AssetPathsById& current);
[[nodiscard]] JsonValue AssetRefToJson(const AssetRef& ref);
