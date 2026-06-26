#pragma once

#include <core/assets/AssetRef.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <vector>

class JsonValue;

//=============================================================================
// Cooked-asset cache index (docs/assets/pipeline.md, Decision B)
//
// Dev-only — compiled under SENCHA_ENABLE_COOK, never shipped. Shipping
// builds load only cooked artifacts; the importer machinery that produces
// them follows the glslang rule: it never rides in a release binary.
//
// The index lives at <assets-root>/.cooked/index.json and maps each source
// file (by assets-root-relative path) to the hash its outputs were cooked
// from and the set of artifacts that cook produced. Keying is explicitly
// source → *set of outputs*: one glTF/.blend source yields several meshes
// today and skeletons/clips later (Decisions B, J, O).
//
// Freshness is decided by the import-on-demand driver, not here: an entry
// is fresh when its recorded hash matches the source bytes on disk and
// every artifact file still exists. A corrupt or missing index is a cold
// cache, never an error — everything recooks.
//=============================================================================

struct CookedArtifact
{
    // Virtual asset path the artifact registers under ("asset://...").
    std::string Path;

    // Physical location, relative to the assets root. Always under
    // .cooked/ — the driver rejects imports that write anywhere else.
    std::string FileRelPath;

    AssetType Type = AssetType::Unknown;
};

struct CookedSourceEntry
{
    // Source file, relative to the assets root, generic separators.
    std::string SourceRelPath;

    // HashBytes64 of the source file contents the artifacts were cooked from.
    uint64_t SourceHash = 0;

    std::vector<CookedArtifact> Artifacts;
};

// Bumping this is the blunt cook-invalidation knob: an old index is a cold
// cache, so every source recooks. Version 2: texture cook output changed
// from RGBA8 to BC-compressed (Decision L format table). Version 3: .smesh
// moved to v3 (skinning stream) and the glTF cook began emitting .sskel /
// .sanim artifacts (Decisions J, M, N). A per-importer cook version is the
// finer-grained eventual replacement if bumps become frequent.
inline constexpr uint32_t kCookedCacheIndexVersion = 3;

class CookedCacheIndex
{
public:
    [[nodiscard]] const CookedSourceEntry* Find(std::string_view sourceRelPath) const;

    // Inserts or replaces the entry for entry.SourceRelPath.
    void Put(CookedSourceEntry entry);

    // Drops the entry for a source. The caller deletes the artifact files; the
    // index only tracks them (it never touches the filesystem). No-op if absent.
    void Erase(std::string_view sourceRelPath);

    [[nodiscard]] const std::unordered_map<std::string, CookedSourceEntry>& Entries() const
    {
        return EntriesBySource;
    }

    [[nodiscard]] std::size_t Size() const { return EntriesBySource.size(); }

    // JSON round trip. Hashes serialize as 16-digit hex strings — JSON
    // numbers are doubles and cannot carry 64 bits. Serialization orders
    // sources by path so the index diffs deterministically.
    [[nodiscard]] JsonValue ToJson() const;
    [[nodiscard]] static bool FromJson(const JsonValue& root,
                                       CookedCacheIndex& out,
                                       std::string* error = nullptr);

    [[nodiscard]] static bool LoadFromFile(std::string_view path,
                                           CookedCacheIndex& out,
                                           std::string* error = nullptr);
    [[nodiscard]] bool SaveToFile(std::string_view path) const;

private:
    std::unordered_map<std::string, CookedSourceEntry> EntriesBySource;
};
