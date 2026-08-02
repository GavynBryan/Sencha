#pragma once

#include <core/assets/AssetInFlightTable.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetStager.h>
#include <core/logging/Logger.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AssetSystem;
class AsyncTaskQueue;
class LoggingProvider;

//=============================================================================
// AssetPreload
//
// Tracks one preload request: N assets streaming toward residency through
// the async lane. Owner-thread only, like everything at the drain point.
//
// The leases collected here are scoped to the preload itself: they exist to
// keep assets alive (and deduplicated) between their commit and the moment
// the real consumer takes its own references — for a zone load, that is the
// finalize step, whose deserialized entities retain through component
// traits. After that, ReleaseAll() lets go; the preload was scaffolding.
// They are leases rather than typed handles so one vector holds every kind,
// including a kind a game module registered.
//
// Failures are advisory: a failed asset still counts toward completion, and
// the consumer's synchronous fallback (resolve-on-attach) covers the gap.
// Preload is an optimization; correctness never depends on it.
//=============================================================================
class AssetPreload
{
public:
    ~AssetPreload() = default;

    AssetPreload(const AssetPreload&) = delete;
    AssetPreload& operator=(const AssetPreload&) = delete;

    [[nodiscard]] bool IsComplete() const { return Pending == 0; }
    [[nodiscard]] bool IsCancelled() const { return Cancelled; }
    [[nodiscard]] uint32_t PendingCount() const { return Pending; }
    [[nodiscard]] uint32_t FailureCount() const { return Failures; }
    [[nodiscard]] uint32_t HeldHandleCount() const
    {
        return static_cast<uint32_t>(HeldAssets.size());
    }

    // Runs once, on the owner thread, when the last pending asset commits —
    // or immediately, if the preload is already complete. One slot; the zone
    // loader is the expected consumer.
    void SetOnComplete(std::function<void()> callback);

    // Releases every lease this preload holds. Idempotent.
    void ReleaseAll();

    // Stops collecting: held leases release now, later commits release their
    // reference immediately instead of storing it, OnComplete is dropped.
    // Pending bookkeeping continues so the preloader's accounting stays
    // consistent.
    void Cancel();

private:
    friend class AssetPreloader;

    AssetPreload() = default;

    void AddPending();

    // Bookkeeping only — never fires callbacks; the preloader orchestrates
    // completion so commits stay reentrancy-safe.
    void FinishOne(bool failed);
    void Store(AssetLease lease);
    void FireOnComplete();

    uint32_t Pending = 0;
    uint32_t Failures = 0;
    bool Cancelled = false;
    std::function<void()> OnComplete;
    std::vector<AssetLease> HeldAssets;
};

//=============================================================================
// AssetPreloader
//
// Drives manifest-sized batches of staged loads through the async lane
// (docs/assets/pipeline.md, Decisions C and D): dedup against the caches,
// coalesce against loads already in flight, LoadStaged on task threads,
// commit at the drain point.
//
// Commit order follows AssetStaging::Dependencies. A staged payload whose
// dependencies are not resident waits; its dependencies are requested through
// this same path and it commits once they land — so a material's commit finds
// its textures, and a skinned mesh's finds its skeleton, without either being
// a case in this file. That ordering is what makes skeleton-backed kinds
// preloadable at all: a commit that inline-loaded a skeleton also pending in
// the same drain would double-release the shared reference.
//
// Dependency edges are checked for cycles before they are recorded, so a
// mutually-referencing pair fails both loads instead of deadlocking.
//
// Owner-thread only. The preloader must outlive any preload it has in
// flight (same contract as AsyncZoneLoader).
//=============================================================================
class AssetPreloader
{
public:
    AssetPreloader(LoggingProvider& logging,
                   AssetRegistry& registry,
                   AssetSystem& assets,
                   AsyncTaskQueue& tasks);

    // Begins async residency for `paths` (typically a manifest). Unknown
    // paths and unsupported types count as failures, not errors — the
    // consumer's sync fallback covers them. Never returns null.
    [[nodiscard]] std::shared_ptr<AssetPreload> Begin(std::span<const std::string> paths);

private:
    // Who is waiting on one in-flight load: a preload directly, or a parent
    // staging that cannot commit until this one lands.
    struct LoadWaiter
    {
        std::shared_ptr<AssetPreload> Preload;
        std::string ParentPath;

        [[nodiscard]] bool IsDependency() const { return !ParentPath.empty(); }
    };

    // A staged payload held back until its declared dependencies are resident.
    // The leases keep those dependencies alive across the gap between their
    // commit and this one.
    struct PendingCommit
    {
        AssetType Type = AssetType::Unknown;
        AssetStaging Staging;
        std::vector<AssetLease> DependencyLeases;
        uint32_t PendingDependencies = 0;
        bool DependencyFailed = false;
    };

    [[nodiscard]] bool CanStage(const AssetRecord& record) const;

    void RequestLoad(const AssetRecord& record, LoadWaiter waiter);
    void SubmitStagedLoad(const AssetRecord& record);
    void OnAssetStaged(AssetType type, const std::string& path, AssetStaging&& staging);
    void OnDependencyFinished(const std::string& parentPath,
                              AssetLease dependency,
                              bool failed);
    void CommitReady(const std::string& path);
    void CompleteLoad(AssetType type, const std::string& path, bool failed);
    void Deliver(const LoadWaiter& waiter,
                 AssetType type,
                 const std::string& path,
                 bool failed);

    [[nodiscard]] bool WouldCreateCycle(std::string_view parent,
                                        std::string_view dependency) const;
    [[nodiscard]] bool Reaches(std::string_view from,
                               std::string_view target,
                               std::unordered_set<std::string>& visited) const;

    Logger& Log;
    AssetRegistry& Registry;
    AssetSystem& Assets;
    AsyncTaskQueue& Tasks;
    AssetInFlightTable<LoadWaiter> InFlight;
    std::unordered_map<std::string, PendingCommit> PendingCommits;
    std::unordered_map<std::string, std::vector<std::string>> DependencyEdges;

    // Preloads whose last pending asset landed during the drain currently
    // running. Completion callbacks fire after the delivery walk finishes, so
    // a callback cannot re-enter bookkeeping that is still unwinding.
    std::vector<std::shared_ptr<AssetPreload>> FinishedThisDrain;
    uint32_t DeliveryDepth = 0;
};
