#pragma once

#include <core/assets/AssetId.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class AssetRegistry;
class JsonValue;

//=============================================================================
// AssetManifest (docs/assets/pipeline.md, Decisions A and D)
//
// The flat list of asset refs a scene (or any JSON payload) references.
// Derived data, never authored: hand-maintained manifests rot. The cook
// derives it and stores it as the .smap's dependency table, so a zone's
// assets stream through the async lane before the zone attaches; an
// unreadable table is not an error — the zone load falls back to
// resolve-on-attach, which is slower but correct.
//
// Each entry carries the cook-assigned stable id alongside the path;
// resolution is id-first with the path as fallback (ResolveManifestPaths).
// The type still always comes from the registry record at load time, so the
// manifest never duplicates (and can never contradict) the registry's
// knowledge.
//=============================================================================
struct AssetManifestEntry
{
    // Invalid when the cook had no id for the path (version-1 manifests,
    // paths absent from the id map) — the path alone resolves it then.
    AssetId Id;

    std::string Path;

    friend bool operator==(const AssetManifestEntry&, const AssetManifestEntry&) = default;
};

struct AssetManifest
{
    std::vector<AssetManifestEntry> Entries;
};

// Collects every unique "asset://" string in a JSON document, in encounter
// order. Deliberately schema-agnostic: any component or payload that
// serializes an asset reference as a path string is covered without this
// code knowing the component exists (the Decision O discipline).
[[nodiscard]] std::vector<std::string> CollectAssetPaths(const JsonValue& root);

// Id-first resolution to the path list the preloader consumes: an id the
// registry knows yields the record's current path (rename-proof); anything
// else falls back to the manifest's stamped path. Run after import + scan
// + ApplyAssetIds, owner thread.
[[nodiscard]] std::vector<std::string> ResolveManifestPaths(const AssetManifest& manifest,
                                                            const AssetRegistry& registry);
