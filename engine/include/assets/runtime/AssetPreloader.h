#pragma once

#include <audio/AudioClipCache.h>
#include <core/assets/AssetInFlightTable.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/Logger.h>
#include <render/Material.h>
#include <render/TextureHandle.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
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
// The handles collected here are scoped to the preload itself: they exist to
// keep assets alive (and deduplicated) between their commit and the moment
// the real consumer takes its own references — for a zone load, that is the
// finalize step, whose deserialized entities retain through component
// traits. After that, ReleaseAll() lets go; the preload was scaffolding.
//
// Failures are advisory: a failed asset still counts toward completion, and
// the consumer's synchronous fallback (resolve-on-attach) covers the gap.
// Preload is an optimization; correctness never depends on it.
//=============================================================================
class AssetPreload
{
public:
    ~AssetPreload();

    AssetPreload(const AssetPreload&) = delete;
    AssetPreload& operator=(const AssetPreload&) = delete;

    [[nodiscard]] bool IsComplete() const { return Pending == 0; }
    [[nodiscard]] bool IsCancelled() const { return Cancelled; }
    [[nodiscard]] uint32_t PendingCount() const { return Pending; }
    [[nodiscard]] uint32_t FailureCount() const { return Failures; }
    [[nodiscard]] uint32_t HeldHandleCount() const
    {
        return static_cast<uint32_t>(Meshes.size() + Materials.size()
                                     + Textures.size() + AudioClips.size());
    }

    // Runs once, on the owner thread, when the last pending asset commits —
    // or immediately, if the preload is already complete. One slot; the zone
    // loader is the expected consumer.
    void SetOnComplete(std::function<void()> callback);

    // Releases every handle this preload holds. Idempotent.
    void ReleaseAll();

    // Stops collecting: held handles release now, later commits release
    // their reference immediately instead of storing it, OnComplete is
    // dropped. Pending bookkeeping continues so the preloader's accounting
    // stays consistent.
    void Cancel();

private:
    friend class AssetPreloader;

    explicit AssetPreload(AssetSystem& assets);

    // Bookkeeping only — never fires callbacks; the preloader orchestrates
    // wave submission and completion so commits stay reentrancy-safe.
    void FinishOne(bool wave1, bool failed);
    void Store(StaticMeshHandle handle);
    void Store(MaterialHandle handle);
    void Store(TextureHandle handle);
    void Store(AudioClipHandle handle);
    void FireOnComplete();

    AssetSystem& Assets;
    uint32_t Pending = 0;
    uint32_t Wave1Pending = 0;
    uint32_t Failures = 0;
    bool Cancelled = false;
    std::function<void()> OnComplete;

    // Materials wait for wave 1 (their textures) before submission, so their
    // commits always hit warm caches instead of decoding inline at the drain.
    std::vector<AssetRecord> DeferredMaterials;

    std::vector<StaticMeshHandle> Meshes;
    std::vector<MaterialHandle> Materials;
    std::vector<TextureHandle> Textures;
    std::vector<AudioClipHandle> AudioClips;
};

//=============================================================================
// AssetPreloader
//
// Drives manifest-sized batches of staged loads through the async lane
// (docs/assets/pipeline.md, Decisions C and D): dedup against the caches,
// coalesce against loads already in flight, LoadStaged on task threads,
// CommitTyped at the drain point. Two waves — leaf assets (textures,
// meshes) first, materials after — because material commits resolve their
// texture refs through the front door and must find them resident.
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
    void SubmitStagedLoad(const AssetRecord& record);
    void SubmitMaterials(const std::shared_ptr<AssetPreload>& preload,
                         std::vector<AssetRecord> records);
    void OnAssetCommitted(AssetType type, const std::string& path, struct AssetStaging&& staging);

    // Gives `preload` one ref-counted handle for `path` if it is resident
    // and the preload still wants it, then advances its bookkeeping.
    void DeliverToWaiter(const std::shared_ptr<AssetPreload>& preload,
                         AssetType type, const std::string& path, bool wave1, bool failed);

    Logger& Log;
    AssetRegistry& Registry;
    AssetSystem& Assets;
    AsyncTaskQueue& Tasks;
    AssetInFlightTable<std::shared_ptr<AssetPreload>> InFlight;
};
