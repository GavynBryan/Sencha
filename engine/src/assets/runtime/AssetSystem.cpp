#include <assets/runtime/AssetSystem.h>

#include <core/logging/LoggingProvider.h>

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

AssetSystem::AssetSystem(LoggingProvider& logging, AssetRegistry& registry)
    : Log(logging.GetLogger<AssetSystem>())
    , Registry(registry)
{
}

IAssetStore* AssetSystem::StoreFor(AssetType type, AssetArity arity)
{
    AssetKindRegistration* kind = KindRegistry.Find(type);
    if (kind == nullptr)
        return nullptr;
    return arity == AssetArity::List ? kind->ListStore : kind->Store;
}

const IAssetStore* AssetSystem::StoreFor(AssetType type, AssetArity arity) const
{
    const AssetKindRegistration* kind = KindRegistry.Find(type);
    if (kind == nullptr)
        return nullptr;
    return arity == AssetArity::List ? kind->ListStore : kind->Store;
}

IAssetListStore* AssetSystem::ListStoreFor(AssetType type)
{
    AssetKindRegistration* kind = KindRegistry.Find(type);
    return kind ? kind->ListStore : nullptr;
}

bool AssetSystem::HasStore(AssetType type) const
{
    return StoreFor(type) != nullptr;
}

AssetStoreTable AssetSystem::Stores() const
{
    AssetStoreTable stores;
    for (const AssetKindRegistration& kind : KindRegistry.Entries())
    {
        if (kind.Store != nullptr)
            stores.Add(kind.Type, AssetArity::Single, *kind.Store);
        if (kind.ListStore != nullptr)
            stores.Add(kind.Type, AssetArity::List, *kind.ListStore);
    }
    return stores;
}

AssetLease AssetSystem::TryAcquireLease(std::string_view path, AssetType type)
{
    IAssetStore* store = StoreFor(type);
    return store ? store->TryAcquireLease(path) : AssetLease{};
}

AssetLease AssetSystem::LoadLease(std::string_view path, AssetType type)
{
    if (path.empty())
    {
        Log.Error("AssetSystem: empty asset path");
        return {};
    }

    // By where it was, too: a document read before a move names it there.
    const AssetRecord* record = Registry.Find(AssetRef{ .Path = std::string(path) });
    if (record == nullptr)
    {
        // A stand-in already waits here with its reason recorded; asking
        // again is not a new failure.
        if (AssetLease standIn = TryAcquireLease(path, type); standIn.IsValid())
            return standIn;
        return FailLoad(path, type, std::format("'{}' is in no mounted content root", path));
    }
    if (record->Type != type)
        return FailLoad(path, type, std::format("'{}' is a {} asset, not a {}", record->Path,
                                                AssetTypeToString(record->Type), AssetTypeToString(type)));

    if (AssetLease resident = TryAcquireLease(record->Path, type); resident.IsValid())
        return resident;

    const std::string recordPath = record->Path;

    // A procedural asset is put into its store by whoever built it; there are
    // no bytes to stage, and a stand-in under its path would take its place.
    if (record->SourceKind == AssetSourceKind::Procedural)
    {
        std::string reason = std::format("no runtime resource registered for procedural {} '{}'",
                                         AssetTypeToString(type), recordPath);
        Log.Error("AssetSystem: {}", reason);
        RecordFailure(recordPath, type, std::move(reason));
        return {};
    }

    IAssetStager* stager = LoaderFor(type);
    if (stager == nullptr)
        return FailLoad(recordPath, type, std::format("no loader registered for {} '{}'", AssetTypeToString(type),
                                                      recordPath));

    AssetStaging staged = stager->LoadStagedResolving(*record, Source, *Registry.PathsById());
    if (!staged.IsValid())
        return FailLoad(recordPath, type, std::move(staged.Error));

    // A dependency the commit loads and fails records its own reason first;
    // this one then names it.
    const std::uint64_t beforeCommit = FailuresRecorded;
    AssetLease lease = Commit(std::move(staged));
    if (lease.IsValid())
        return lease;
    if (FailuresRecorded != beforeCommit && LastFailedPath != recordPath)
        return FailLoad(recordPath, type,
                        std::format("'{}' depends on '{}', which did not load", recordPath, LastFailedPath));
    return FailLoad(recordPath, type, std::format("'{}' was staged but could not be committed", recordPath));
}

AssetLease AssetSystem::FailLoad(std::string_view path, AssetType type, std::string reason)
{
    Log.Error("AssetSystem: {}", reason);
    RecordFailure(path, type, std::move(reason));

    const AssetKindRegistration* kind = KindRegistry.Find(type);
    if (kind == nullptr || !kind->StandIn)
        return {};
    if (AssetLease standIn = TryAcquireLease(path, type); standIn.IsValid())
        return standIn;
    return kind->StandIn(path);
}

void AssetSystem::RetryStandIns()
{
    if (Registry.Revision() == RetriedAtRevision)
        return;
    RetriedAtRevision = Registry.Revision();

    // A copy: a retry that loads clears its own entry.
    const std::vector<LoadFailure> failures = Failures;
    for (const LoadFailure& failure : failures)
        RetryStandIn(failure);
}

void AssetSystem::RetryStandIn(const LoadFailure& failure)
{
    if (!IsResident(failure.Path, failure.Type))
        return;
    const AssetRecord* record = Registry.FindByPath(failure.Path);
    if (record == nullptr || record->Type != failure.Type || record->SourceKind != AssetSourceKind::File)
        return;
    IAssetStager* stager = LoaderFor(failure.Type);
    if (stager == nullptr)
        return;
    (void)Reload(stager->LoadStagedResolving(*record, Source, *Registry.PathsById()));
}

void AssetSystem::RecordFailure(std::string_view path, AssetType type, std::string reason)
{
    LastFailedPath = path;
    ++FailuresRecorded;
    const auto found = std::ranges::find_if(Failures, [&](const LoadFailure& failure) {
        return failure.Path == path && failure.Type == type;
    });
    if (found != Failures.end())
    {
        if (found->Reason == reason)
            return;
        found->Reason = std::move(reason);
    }
    else
    {
        Failures.push_back({ std::string(path), std::move(reason), type });
    }
    ++FailuresVersion;
}

void AssetSystem::ClearFailure(std::string_view path, AssetType type)
{
    if (std::erase_if(Failures, [&](const LoadFailure& failure) { return failure.Path == path && failure.Type == type; })
        > 0)
        ++FailuresVersion;
}

std::string_view AssetSystem::GetPathForLease(AssetType type,
                                              std::uint64_t token) const
{
    const IAssetStore* store = StoreFor(type);
    return store != nullptr ? store->GetPath(token) : std::string_view{};
}

void AssetSystem::ReleaseLease(AssetType type, std::uint64_t token, AssetArity arity)
{
    if (IAssetStore* store = StoreFor(type, arity); store != nullptr)
        store->ReleaseToken(token);
}

AssetLease AssetSystem::InternList(AssetType type, std::span<const std::uint64_t> members)
{
    IAssetListStore* store = ListStoreFor(type);
    if (store == nullptr)
    {
        Log.Error("AssetSystem: no list store registered for {}", AssetTypeToString(type));
        return {};
    }
    return store->InternList(members);
}

std::vector<std::uint64_t> AssetSystem::ListMembers(AssetType type, std::uint64_t token) const
{
    const AssetKindRegistration* kind = KindRegistry.Find(type);
    if (kind == nullptr || kind->ListStore == nullptr)
        return {};
    return kind->ListStore->ListMembers(token);
}

AssetLease AssetSystem::Commit(AssetStaging&& staged)
{
    const AssetKindRegistration* kind = KindRegistry.Find(staged.Record.Type);
    if (kind == nullptr || !kind->Commit)
    {
        Log.Error("AssetSystem: no commit registered for {} '{}'",
                  AssetTypeToString(staged.Record.Type), staged.Record.Path);
        return {};
    }

    const std::string path = staged.Record.Path;
    const AssetType type = staged.Record.Type;
    AssetLease lease = kind->Commit(std::move(staged));
    if (lease.IsValid())
        ClearFailure(path, type);
    return lease;
}

bool AssetSystem::Reload(AssetStaging&& staged)
{
    const std::string path = staged.Record.Path;
    const AssetType type = staged.Record.Type;
    if (!staged.IsValid())
    {
        Log.Error("AssetSystem: reload of '{}' failed: {}", path, staged.Error);
        RecordFailure(path, type, std::move(staged.Error));
        return false;
    }

    const AssetKindRegistration* kind = KindRegistry.Find(type);
    if (kind == nullptr || !kind->Reload)
    {
        Log.Error("AssetSystem: no reload registered for {} '{}'", AssetTypeToString(type), path);
        return false;
    }

    if (!kind->Reload(std::move(staged)))
        return false;
    ClearFailure(path, type);
    return true;
}

const AssetRecord* AssetSystem::Resolve(std::string_view path, AssetType expectedType) const
{
    if (path.empty())
        return nullptr;
    // By where it was, too: a document read before a move names it there.
    const AssetRecord* record = Registry.Find(AssetRef{ .Path = std::string(path) });
    return record != nullptr && record->Type == expectedType ? record : nullptr;
}

bool AssetSystem::IsResident(std::string_view path, AssetType type) const
{
    const IAssetStore* store = StoreFor(type);
    return store != nullptr && store->IsResident(path);
}

std::string_view AssetSystem::ResolveRefPath(AssetId id,
                                             std::string_view fallbackPath,
                                             AssetType expectedType) const
{
    if (!id.IsValid())
        return fallbackPath;

    const AssetRecord* record = Registry.FindById(id);
    if (record == nullptr)
        return fallbackPath;

    if (record->Type != expectedType)
    {
        Log.Error("AssetSystem: id {} is a {} asset, expected {}; falling back to path '{}'",
                  AssetIdToString(id),
                  AssetTypeToString(record->Type),
                  AssetTypeToString(expectedType),
                  fallbackPath);
        return fallbackPath;
    }

    return record->Path;
}

IAssetStager* AssetSystem::LoaderFor(AssetType type)
{
    AssetKindRegistration* kind = KindRegistry.Find(type);
    return kind ? kind->Stager : nullptr;
}
