#pragma once

#include <core/assets/AssetId.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

class AssetRegistry;
class JsonValue;

//=============================================================================
// AssetIdMap (docs/assets/pipeline.md, Decision A / Stage 4e)
//
// The persisted id map the cook step maintains: virtual asset path →
// (stable id, last-seen content hash). New assets get an id at first sight;
// renames keep theirs because the cook matches a vanished path's content
// hash before minting. The map lives at <assets-root>/asset_ids.json and is
// meant to be committed — it is the durable identity record, not a cache
// (a lost map costs rename history, not correctness: first-sight ids are
// deterministic from the path, so unrenamed assets re-mint identically).
//
// The cook is the only writer. The runtime reads the map at startup and
// applies it to the registry so id-stamped refs in cooked scenes and
// manifests resolve; paths remain the fallback for anything unmapped.
//=============================================================================

struct AssetIdMapEntry
{
    AssetId Id;

    // HashBytes64 of the asset's bytes when the cook last saw it — the
    // rename-detection key. Zero when the bytes were never hashable.
    uint64_t ContentHash = 0;
};

inline constexpr uint32_t kAssetIdMapVersion = 1;
inline constexpr std::string_view kAssetIdMapFileName = "asset_ids.json";

class AssetIdMap
{
public:
    // Invalid id when the path has no entry.
    [[nodiscard]] AssetId FindId(std::string_view path) const;

    [[nodiscard]] const std::unordered_map<std::string, AssetIdMapEntry>& Entries() const
    {
        return EntriesByPath;
    }

    // The cook-side assignment step, one path at a time:
    //   - a mapped path keeps its id (the content hash is refreshed);
    //   - an unmapped path first looks for a rename donor — an entry with
    //     the same nonzero content hash whose path `isPathLive` reports
    //     dead — and inherits its id, retiring the donor entry;
    //   - otherwise a new id is minted: HashBytes64 of the path, probed
    //     with a salt until it collides with nothing already in the map.
    //     Path-hash minting is deliberate — two branches adding the same
    //     asset assign the same id, so committed maps merge cleanly.
    // With no liveness callback, rename detection is skipped.
    AssetId EnsureId(std::string_view path,
                     uint64_t contentHash,
                     const std::function<bool(std::string_view)>& isPathLive = nullptr);

    // True when the map changed since construction, load, or the last save.
    [[nodiscard]] bool IsDirty() const { return Dirty; }

    // JSON round trip; entries serialize sorted by path so the committed
    // file diffs deterministically. Ids and hashes ride as hex strings
    // (the cooked-cache index rule: JSON numbers cannot carry 64 bits).
    [[nodiscard]] JsonValue ToJson() const;
    [[nodiscard]] static bool FromJson(const JsonValue& root,
                                       AssetIdMap& out,
                                       std::string* error = nullptr);

    [[nodiscard]] static bool LoadFromFile(std::string_view path,
                                           AssetIdMap& out,
                                           std::string* error = nullptr);
    [[nodiscard]] bool SaveToFile(std::string_view path) const;

private:
    std::unordered_map<std::string, AssetIdMapEntry> EntriesByPath;
    std::unordered_map<AssetId, std::string> PathsById;
    mutable bool Dirty = false;
};

// Fills AssetRecord ids on every registered record whose path the map
// knows. Runs after import + scan, owner thread. Returns how many records
// received an id; unmapped records keep the invalid id and resolve by
// path, as before.
std::size_t ApplyAssetIds(const AssetIdMap& map, AssetRegistry& registry);

// Returns a copy of `root` with every "asset://" string the map knows
// replaced by the id-stamped ref object {"id": "<hex>", "path": "<same>"}.
// Strings the map does not know stay plain paths — the cooked output is
// never less resolvable than the authored input. The walk is
// schema-agnostic, same discipline as CollectAssetPaths (Decision O).
[[nodiscard]] JsonValue StampAssetRefIds(const JsonValue& root, const AssetIdMap& map);
