#pragma once

#include <assets/cook/AssetImporter.h>

#include <cstddef>
#include <string_view>

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
