#pragma once

#include <core/assets/AssetId.h>

#include <filesystem>
#include <optional>
#include <string_view>

class JsonValue;

//=============================================================================
// AssetIdentity (docs/assets/pipeline.md, Decision A)
//
// Where an asset states its id: Sencha's own documents at their root ("id"),
// every other file in the ".meta" sidecar beside it, which also holds the
// file's import settings. An asset that states none has the id of where it is
// (AssetIdOfPath).
//=============================================================================

inline constexpr std::string_view kAssetSidecarSuffix = ".meta";
inline constexpr std::string_view kAssetIdKey = "id";

// True for the documents whose root states their own id (.smat .sscene .sdata
// .sworld .sanimscenario); case-sensitive, with the dot.
[[nodiscard]] bool StatesIdInside(std::string_view extension);
[[nodiscard]] std::filesystem::path SidecarOf(const std::filesystem::path& file);
// The file that states `file`'s id: the file itself or its sidecar.
[[nodiscard]] std::filesystem::path IdStatementOf(const std::filesystem::path& file);

// The id `root` states, when it is an object stating a well-formed one.
[[nodiscard]] std::optional<AssetId> StatedAssetId(const JsonValue& root);
// The same, of JSON text: a document's or a sidecar's.
[[nodiscard]] std::optional<AssetId> StatedAssetIdIn(std::string_view text);
// A copy of a document is a new asset: it states no id until it is given one.
void ForgetStatedAssetId(JsonValue& root);

// `text` -- a JSON or JSON5 object, a document's or a sidecar's -- stating `id`
// as its first member, everything else exactly as it was written; nullopt when
// it is not an object. An empty `text` is a new sidecar stating only the id.
[[nodiscard]] std::optional<std::string> StatingAssetId(std::string_view text, AssetId id);

// Reads the id `file` states, inside or in its sidecar. Nullopt when it states
// none, or when what would state it is missing or is not JSON.
[[nodiscard]] std::optional<AssetId> ReadStatedAssetId(const std::filesystem::path& file);
