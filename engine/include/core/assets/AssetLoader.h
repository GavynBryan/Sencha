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
//   LoadStaged  — file IO + decode into plain CPU data. Pure with respect
//                 to engine state (no caches, no services, no Vulkan), so
//                 it may run on a task thread. Errors return in
//                 AssetStaging::Error; the stage half does not log.
//   Commit      — owner thread only. Inserts into the cache, performs the
//                 GPU upload, returns the handle. Logs its own failures.
//
// The synchronous path is LoadStaged + Commit called back-to-back on the
// owner thread — one code path, two schedulings, which is what makes every
// loader testable deterministically with zero threads.
//
// Chunking is the loader's duty: v1 is one commit per asset; a payload
// that would blow the drain budget must be reshaped as multiple staged
// tasks when a profile demands it, not split by the committer.
//=============================================================================

struct AssetStaging
{
    AssetRecord Record;

    // Loader-defined CPU payload (StaticMeshData, Image, MaterialDescription,
    // ...). Type-erased so heterogeneous loads can flow through one driver;
    // each loader's Commit knows its own payload type and rejects others.
    std::any Payload;

    // Non-empty means staging failed. Set instead of logging because the
    // stage half may run off the owner thread; the committer/driver logs.
    std::string Error;

    [[nodiscard]] bool IsValid() const { return Error.empty() && Payload.has_value(); }
};

struct AssetCommitResult
{
    bool Success = false;
};

class IAssetLoader
{
public:
    virtual ~IAssetLoader() = default;

    [[nodiscard]] virtual AssetType Type() const = 0;

    // Task-thread half. `source` is the byte seam (Decision I).
    [[nodiscard]] virtual AssetStaging LoadStaged(const AssetRecord& record,
                                                  IAssetSource& source) = 0;

    // Owner-thread half. Consumes the staging payload.
    virtual AssetCommitResult Commit(AssetStaging&& staged) = 0;
};
