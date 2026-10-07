#pragma once

#include <core/assets/AssetRef.h>

#include <optional>
#include <string_view>
#include <vector>

class JsonValue;
struct IReadArchive;
struct IWriteArchive;

//=============================================================================
// AssetRefCodec (docs/assets/pipeline.md, Decision A)
//
// The one text form of an asset reference, wherever text names an asset: a
// bare "asset://..." path, or {"id": "<16 hex>", "path": "asset://..."} once
// the asset's id is known, with the path then optional. Writing chooses the
// object whenever the reference carries an id. Reading fills Id and Path and
// leaves Type to the caller, who knows what the field holds; resolving the id
// to the asset's current path belongs to whoever holds the registry.
//=============================================================================

// False, with the field marked invalid, when `key` holds anything else: a
// non-path string, a malformed id, or neither id nor path.
[[nodiscard]] bool ReadAssetRef(IReadArchive& archive, std::string_view key, AssetRef& out);
void WriteAssetRef(IWriteArchive& archive, std::string_view key, const AssetRef& ref);

[[nodiscard]] std::optional<AssetRef> AssetRefFromJson(const JsonValue& value);

// Every reference anywhere in `root`, in either form, in the order met: the
// schema-agnostic walk, for a reader that does not know which fields name
// assets. An object that is a reference is not searched further.
[[nodiscard]] std::vector<AssetRef> CollectAssetRefs(const JsonValue& root);
[[nodiscard]] JsonValue AssetRefToJson(const AssetRef& ref);
