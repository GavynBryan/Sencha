#pragma once

#include <core/assets/AssetCommitResult.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSource.h>
#include <core/assets/AssetStoreTable.h>
#include <core/logging/Logger.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class AsyncTaskQueue;
class LoggingProvider;

//=============================================================================
// AssetSystem
//
// The asset front door: a registry of records, a byte source, and the table of
// registered kinds. A load resolves a virtual path through the registry and
// composes the kind's staged loader (docs/assets/pipeline.md, Decision C):
// dedup check, LoadStaged, Commit -- back-to-back on the owner thread here;
// split across the async lane by the manifest-driven zone path. One code path,
// two schedulings.
//
// Every kind, built-in or module-defined, attaches through Kinds(); this class
// names no loader, cache, or handle type. The owner that composes the caches
// registers the kinds (RuntimeAssets does for the engine's nine).
//=============================================================================
class AssetSystem
{
public:
    AssetSystem(LoggingProvider& logging, AssetRegistry& registry);

    // The record `path` names when it is of `expectedType`. A lookup, so it
    // says nothing about an absence; LoadLease records why a load failed.
    [[nodiscard]] const AssetRecord* Resolve(std::string_view path, AssetType expectedType) const;

    // True if `path` currently has a live entry in the store for `type`,
    // without touching its refcount. The hot-reload driver uses this to skip
    // re-staging assets that aren't loaded.
    [[nodiscard]] bool IsResident(std::string_view path, AssetType type) const;

    // Id-first ref resolution (Decision A): when the registry knows the id,
    // the record's current path wins -- that is what makes an id-stamped ref
    // survive a rename the stamped path predates. An unknown id (or a type
    // mismatch, which is logged) falls back to the stamped path; an invalid
    // id is simply "no id", not an error, so path-only refs flow through
    // unchanged.
    [[nodiscard]] std::string_view ResolveRefPath(AssetId id,
                                                  std::string_view fallbackPath,
                                                  AssetType expectedType) const;

    // Whether this process can hold a loaded asset of `type` at all.
    //
    // A composition query, never a health check. A kind's store is fixed when
    // the kind is registered and nothing detaches one later, so a false answer
    // means this process was built without that capability -- a headless host
    // with no graphics services has no mesh or texture store -- and never that
    // a cache failed or went away.
    //
    // That is what lets a consumer treat an unresolvable asset of an
    // unsupported kind as expected rather than as an error, while an
    // unresolvable asset of a supported kind stays a real failure.
    [[nodiscard]] bool HasStore(AssetType type) const;

    // Cached-only: a reference if the asset is already resident, an invalid
    // lease otherwise. Never loads, never logs -- the preload path uses this to
    // dedup against the stores before submitting staged work.
    [[nodiscard]] AssetLease TryAcquireLease(std::string_view path, AssetType type);

    // The synchronous load: a resident path gains a reference; one that cannot
    // load records why and yields its kind's stand-in, or an invalid lease. A
    // lease, so the load's reference lets go once the real holder takes its own.
    [[nodiscard]] AssetLease LoadLease(std::string_view path, AssetType type);

    // Records why `path` did not load and yields what it resolves to instead:
    // the kind's stand-in, or an invalid lease. Every scheduling of a load
    // fails through here, so the synchronous and the async path agree.
    [[nodiscard]] AssetLease FailLoad(std::string_view path, AssetType type, std::string reason);

    // Stages again every failed path whose stand-in is still held, replacing
    // the stand-in in place at the drain where the asset now loads. Does
    // nothing until the registry changes; the engine calls it once a frame.
    void RetryStandIns(AsyncTaskQueue& tasks);

    // Stages `record` on a task thread and swaps the result into its resident
    // entry at the drain, through Reload.
    void StageReload(const AssetRecord& record, AsyncTaskQueue& tasks);

    // Where assets named by id are now. Owner thread; the snapshot itself may
    // travel to a stage on any thread.
    [[nodiscard]] std::shared_ptr<const AssetPathsById> PathsById() const { return Registry.PathsById(); }

    // `ref` as the registry knows its asset now (AssetRegistry::Current): what a
    // declared dependency is loaded as.
    [[nodiscard]] AssetRef Current(AssetRef ref) const { return Registry.Current(std::move(ref)); }

    // The path a held token names. Empty when the kind is unregistered or the
    // token names nothing.
    [[nodiscard]] std::string_view GetPathForLease(AssetType type,
                                                   std::uint64_t token) const;

    // Drops a reference the caller took as a lease and then relinquished into
    // storage of its own.
    void ReleaseLease(AssetType type, std::uint64_t token,
                      AssetArity arity = AssetArity::Single);

    // The list form of a kind (IAssetListStore): one reference to an ordered
    // list of that kind's tokens. Invalid lease, logged, when the kind has no
    // list store.
    [[nodiscard]] AssetLease InternList(AssetType type, std::span<const std::uint64_t> members);
    [[nodiscard]] std::vector<std::uint64_t> ListMembers(AssetType type,
                                                         std::uint64_t token) const;

    // Owner-thread commit through the kind's registered Commit: the creation
    // reference, or the dependency that refused it. A path committed over its
    // stand-in yields the stand-in; only Reload replaces one.
    [[nodiscard]] AssetCommitOutcome Commit(AssetStaging&& staged);
    // Why a commit of `path` that yielded no reference failed, as it is recorded.
    [[nodiscard]] static std::string CommitFailureReason(std::string_view path, const AssetCommitOutcome& outcome);

    // Owner-thread in-place reload through the kind's registered Reload. False
    // when the staging failed, the kind cannot reload or the asset is not
    // resident; a failed staging is recorded and leaves what is resident as it is.
    [[nodiscard]] bool Reload(AssetStaging&& staged);

    // The staged-load surface (Decision C), exposed for async drivers: the
    // preloader runs LoaderFor(type)->LoadStaged on a task thread against
    // DefaultSource() and PathsById(), and commits at the drain point.
    [[nodiscard]] IAssetStager* LoaderFor(AssetType type);
    [[nodiscard]] IAssetSource& DefaultSource() { return Source; }

    // The registered kinds. Scanning, preload, and hot reload read this
    // instead of carrying their own switch over AssetType.
    [[nodiscard]] AssetKindRegistry& Kinds() { return KindRegistry; }
    [[nodiscard]] const AssetKindRegistry& Kinds() const { return KindRegistry; }

    // The single and list store of every kind registered so far: what a
    // World's components retain their asset fields through. A snapshot, so
    // take it after the last kind is registered.
    [[nodiscard]] AssetStoreTable Stores() const;

    // Why a load of a path failed, kept until a load or reload of that path
    // succeeds: what a tool shows a person instead of a log line.
    struct LoadFailure
    {
        std::string Path;
        std::string Reason;
        AssetType Type = AssetType::Unknown;
    };
    [[nodiscard]] std::span<const LoadFailure> LoadFailures() const { return Failures; }
    // Changes whenever a failure is recorded or cleared.
    [[nodiscard]] std::uint64_t LoadFailuresVersion() const { return FailuresVersion; }

private:
    void RecordFailure(std::string_view path, AssetType type, std::string reason);
    void ClearFailure(std::string_view path, AssetType type);
    void RetryStandIn(const LoadFailure& failure, AsyncTaskQueue& tasks);

    [[nodiscard]] IAssetStore* StoreFor(AssetType type, AssetArity arity = AssetArity::Single);
    [[nodiscard]] const IAssetStore* StoreFor(AssetType type,
                                              AssetArity arity = AssetArity::Single) const;
    [[nodiscard]] IAssetListStore* ListStoreFor(AssetType type);

    Logger& Log;
    AssetRegistry& Registry;
    FileAssetSource Source;
    AssetKindRegistry KindRegistry;
    std::vector<LoadFailure> Failures;
    std::uint64_t FailuresVersion = 0;
    std::uint64_t RetriedAtRevision = 0;
};
