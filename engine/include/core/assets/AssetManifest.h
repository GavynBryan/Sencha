#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class JsonValue;

//=============================================================================
// AssetManifest (docs/assets/pipeline.md, Decision D)
//
// The flat list of asset paths a scene (or any JSON payload) references.
// Derived data, never authored: hand-maintained manifests rot. The cook step
// (and until it exists, the dev-asset generator) derives a manifest next to
// each scene; AsyncZoneLoader preloads from it so a zone's assets stream
// through the async lane before the zone attaches. A missing or stale
// manifest is not an error — the zone load falls back to resolve-on-attach,
// which is slower but correct.
//
// Entries are paths only; the type comes from the registry record at load
// time, so the manifest never duplicates (and can never contradict) the
// registry's knowledge.
//=============================================================================
struct AssetManifest
{
    std::vector<std::string> Paths;
};

inline constexpr uint32_t kAssetManifestVersion = 1;

// Collects every unique "asset://" string in a JSON document, in encounter
// order. Deliberately schema-agnostic: any component or payload that
// serializes an asset reference as a path string is covered without this
// code knowing the component exists (the Decision O discipline).
[[nodiscard]] std::vector<std::string> CollectAssetPaths(const JsonValue& root);

[[nodiscard]] JsonValue AssetManifestToJson(const AssetManifest& manifest);
[[nodiscard]] bool ParseAssetManifestJson(const JsonValue& root,
                                          AssetManifest& out,
                                          std::string* error = nullptr);

[[nodiscard]] bool LoadAssetManifestFile(std::string_view path,
                                         AssetManifest& out,
                                         std::string* error = nullptr);
[[nodiscard]] bool WriteAssetManifestFile(std::string_view path, const AssetManifest& manifest);
