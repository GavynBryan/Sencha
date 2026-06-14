#pragma once

#include <assets/cook/AssetImporter.h>

#include <cstddef>
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
