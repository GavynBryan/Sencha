#pragma once

#include <assets/cook/AssetImporter.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class AssetRegistry;
class LoggingProvider;

//=============================================================================
// Import-on-demand (docs/assets/pipeline.md, Decision B)
//
// The dev-build resolve path: walk the assets root for source files whose
// extensions have a registered importer; for each, check the cooked cache
// (keyed by source content hash) and re-import on miss or stale; register
// every cooked artifact with the asset registry under its virtual path.
//
// Runs on the owner thread before the directory scan, dev builds only.
// Shipping builds never call this — they load cooked artifacts that the
// batch cook produced, and the importer code is not in the binary.
//
// Failures are per-source and advisory in shape (a failed import means a
// missing asset with a clear log line, not a crashed boot), but the return
// value reports them so tools and tests can be strict.
//=============================================================================

struct ImportOnDemandStats
{
    std::size_t SourcesSeen = 0;  // files matching a registered importer
    std::size_t CookedFresh = 0;  // served from the cooked cache
    std::size_t Imported = 0;     // (re)cooked this run
    std::size_t Failed = 0;       // read or import failures
};

[[nodiscard]] bool ImportAssetsOnDemand(std::string_view rootDirectory,
                                        const AssetImporterRegistry& importers,
                                        AssetRegistry& registry,
                                        LoggingProvider& logging,
                                        ImportOnDemandStats* outStats = nullptr);

//=============================================================================
// Prepare / publish / register split
//
// The three side effects `ImportAssetsOnDemand` fuses, separated so a document
// cook can prepare imports without touching active state and publish them
// through its own transaction:
//   1. Prepare  cooks stale sources into private staging, resolves hashes, and
//      produces an index delta. Reads active state only; writes nothing active.
//   2. Publish  transfers the staged bytes and index delta through a supplied
//      IImportPublisher owner. Runs no importer.
//   3. Register binds the produced records into a live registry, after a
//      successful publish, so a failed publish registers nothing.
//=============================================================================

// One newly imported artifact and the private staging file holding its bytes.
struct PreparedCookedArtifact
{
    CookedArtifact        Artifact;     // final FileRelPath and resolved hash
    std::filesystem::path PreparedFile; // staged bytes, published later
};

// The cooked-index changes a prepared import carries. Import only ever upserts
// (new sources, and fresh entries whose stats or hashes were refreshed).
struct CookedCacheIndexDelta
{
    std::vector<CookedSourceEntry> Puts;
};

// A move-only plan value produced by PrepareAssetsOnDemand. Owns a temporary
// staging directory holding the prepared bytes and removes it on destruction,
// so abandoning a pending import leaves no stray output. Names no registry and
// no transaction.
class PendingAssetImport
{
public:
    PendingAssetImport() = default;
    PendingAssetImport(PendingAssetImport&&) noexcept;
    PendingAssetImport& operator=(PendingAssetImport&&) noexcept;
    ~PendingAssetImport();

    PendingAssetImport(const PendingAssetImport&) = delete;
    PendingAssetImport& operator=(const PendingAssetImport&) = delete;

    std::vector<PreparedCookedArtifact> Artifacts;    // stale/new sources, staged
    CookedCacheIndexDelta               IndexDelta;    // new plus upgraded fresh entries
    std::vector<CookedArtifact>         Registrations; // every fresh and imported record
    ImportOnDemandStats                 Stats;

private:
    friend bool PrepareAssetsOnDemand(const std::filesystem::path&,
                                      const AssetImporterRegistry&,
                                      LoggingProvider&, PendingAssetImport&);
    void Cleanup() noexcept;

    std::filesystem::path TempRoot; // owned staging dir; removed on destruction
};

// Pure producer. Reads the active index and artifact files read-only; writes no
// active file, no active index, and touches no registry. Per-source failures
// stay advisory (logged, counted in Stats); the returned false reports that any
// source failed while `out` still carries every import that succeeded.
[[nodiscard]] bool PrepareAssetsOnDemand(const std::filesystem::path& assetsRoot,
                                         const AssetImporterRegistry& importers,
                                         LoggingProvider& logging,
                                         PendingAssetImport& out);

// Transfers prepared bytes and index entries through `publisher`, one artifact
// at a time. Validates that no destination path is claimed twice before writing
// any bytes. Runs no scan and no importer. Committing the publisher's index is
// the publisher owner's job, not this call's.
[[nodiscard]] bool PublishAssetImport(PendingAssetImport pending,
                                      IImportPublisher& publisher,
                                      std::string* error = nullptr);

// Binds produced records into a live registry. Called after a successful
// publish so a failed publish registers nothing. Takes records, not the pending
// value: publication consumes the pending by move, so callers copy
// `Registrations` out first.
[[nodiscard]] bool RegisterImportedAssets(std::span<const CookedArtifact> records,
                                          const std::filesystem::path& assetsRoot,
                                          AssetRegistry& registry,
                                          LoggingProvider& logging);

// Writes prepared imports to the active `.cooked/` tree and owns its own cooked
// index, loaded on construction. Saves that index once, last, through
// CookedCacheIndex::SaveToFile's temp-and-rename, so set membership flips
// atomically even though byte files land in place. The standalone publisher
// behind `ImportAssetsOnDemand`.
class FilesystemImportArtifactWriter final : public IImportPublisher
{
public:
    explicit FilesystemImportArtifactWriter(std::filesystem::path assetsRoot);

    bool WriteBytes(std::string_view fileRelPath,
                    std::span<const std::byte> bytes) override;
    void PutIndexEntry(CookedSourceEntry entry) override;

    // Persists the owned index if any entry changed. No-op (true) otherwise.
    [[nodiscard]] bool Save();

private:
    std::filesystem::path Root;
    CookedCacheIndex Index;
    bool Dirty = false;
};

//=============================================================================
// Single-source re-import (Stage 6 hot reload).
//
// Re-runs the importer for one changed source unconditionally (the caller —
// the hot-reload watcher — already knows the bytes changed, so there is no
// freshness check), writes the cooked artifacts under .cooked/, and updates
// the cooked-cache index on disk so a later cold start doesn't recook
// needlessly. Fills `outArtifactPaths` with the virtual paths produced so the
// caller can reload the resident ones. Owner-thread, dev-only.
//
// It does not touch the registry: a re-cook overwrites the existing cooked
// artifact at the same FilePath the registry already points at, so the
// resident asset's record stays valid and its loader reads the new bytes.
// (A re-cook that changes the artifact *set* — mesh topology — is out of
// Stage 6 scope.)
//=============================================================================
[[nodiscard]] bool ReimportOneSource(std::string_view rootDirectory,
                                     std::string_view sourceRelPath,
                                     const AssetImporterRegistry& importers,
                                     LoggingProvider& logging,
                                     std::vector<std::string>& outArtifactPaths);
