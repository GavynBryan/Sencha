#pragma once

#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSource.h>
#include <core/assets/AssetStoreTable.h>
#include <core/logging/Logger.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

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

    // The synchronous load. A resident path gains a reference instead of
    // re-staging.
    //
    // A lease rather than a raw handle because the caller usually is not the
    // final owner: a component that names the asset takes its own reference
    // when it is added, and the load's reference has to be let go afterwards.
    // A lease that goes out of scope does that on its own.
    [[nodiscard]] AssetLease LoadLease(std::string_view path, AssetType type);

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

    // Owner-thread commit of a staged payload, dispatched through the kind's
    // registered Commit. Returns the creation reference.
    [[nodiscard]] AssetLease Commit(AssetStaging&& staged);

    // Owner-thread in-place reload of a staged payload, through the kind's
    // registered Reload. False when the kind cannot reload or the asset is
    // not resident.
    [[nodiscard]] bool Reload(AssetStaging&& staged);

    // The staged-load surface (Decision C), exposed for async drivers: the
    // preloader runs LoaderFor(type)->LoadStaged on a task thread against
    // DefaultSource(), and commits at the drain point.
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

private:
    [[nodiscard]] IAssetStore* StoreFor(AssetType type, AssetArity arity = AssetArity::Single);
    [[nodiscard]] const IAssetStore* StoreFor(AssetType type,
                                              AssetArity arity = AssetArity::Single) const;
    [[nodiscard]] IAssetListStore* ListStoreFor(AssetType type);

    Logger& Log;
    AssetRegistry& Registry;
    FileAssetSource Source;
    AssetKindRegistry KindRegistry;
};
