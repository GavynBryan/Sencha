#pragma once

#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSource.h>

#include <any>
#include <string>

//=============================================================================
// The staged-load contract (docs/assets/pipeline.md, Decision C)
//
// Every asset type splits its load into two halves, mirroring the
// AsyncZoneLoader build/finalize split:
//
//   LoadStaged  -- file IO + decode into plain CPU data. Pure with respect
//                  to engine state (no caches, no services, no Vulkan), so
//                  it may run on a task thread. Errors return in
//                  AssetStaging::Error; the stage half does not log.
//   CommitTyped -- owner thread only. Inserts into the cache, performs the
//                  GPU upload, returns the handle. Logs its own failures.
//
// Only the staging half is virtual. It is the half the async driver runs
// without knowing the asset type, against a loader it resolved from a type
// tag. A commit is always issued against a loader the caller already names,
// and each loader's CommitTyped returns that loader's own handle type, so
// routing it through a type-erased virtual would only lose the handle.
//
// The synchronous path is LoadStaged + CommitTyped called back-to-back on
// the owner thread -- one code path, two schedulings, which is what makes
// every loader testable deterministically with zero threads.
//
// Chunking is the loader's duty: v1 is one commit per asset; a payload
// that would blow the drain budget must be reshaped as multiple staged
// tasks when a profile demands it, not split by the committer.
//=============================================================================

struct AssetStaging
{
    AssetRecord Record;

    // Loader-defined CPU payload (MeshGeometry, Image, MaterialDescription,
    // ...). Type-erased so heterogeneous loads can flow through one driver;
    // each loader's commit knows its own payload type and rejects others.
    std::any Payload;

    // Non-empty means staging failed. Set instead of logging because the
    // stage half may run off the owner thread; the committer/driver logs.
    std::string Error;

    [[nodiscard]] bool IsValid() const { return Error.empty() && Payload.has_value(); }
};

class IAssetStager
{
public:
    virtual ~IAssetStager() = default;

    // Task-thread half. `source` is the byte seam (Decision I).
    [[nodiscard]] virtual AssetStaging LoadStaged(const AssetRecord& record,
                                                  IAssetSource& source) = 0;
};
