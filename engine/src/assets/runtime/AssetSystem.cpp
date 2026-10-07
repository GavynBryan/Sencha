#include <assets/runtime/AssetSystem.h>

#include <core/logging/LoggingProvider.h>

#include <algorithm>
#include <format>
#include <utility>

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
    const AssetRecord* record = Resolve(path, type);
    if (record == nullptr)
        return {};

    if (AssetLease resident = TryAcquireLease(record->Path, type); resident.IsValid())
        return resident;

    const std::string recordPath = record->Path;
    const auto fail = [&](std::string reason) {
        Log.Error("AssetSystem: {}", reason);
        RecordFailure(recordPath, std::move(reason));
        return AssetLease{};
    };

    // A procedural asset is put into its store by whoever built it; there are
    // no bytes to stage.
    if (record->SourceKind == AssetSourceKind::Procedural)
        return fail(std::format("no runtime resource registered for procedural {} '{}'", AssetTypeToString(type),
                                recordPath));

    IAssetStager* stager = LoaderFor(type);
    if (stager == nullptr)
        return fail(std::format("no loader registered for {} '{}'", AssetTypeToString(type), recordPath));

    AssetStaging staged = stager->LoadStagedResolving(*record, Source, *Registry.PathsById());
    if (!staged.IsValid())
        return fail(staged.Error);

    // A dependency the commit loads and fails records its own reason first;
    // this one then names it.
    const std::uint64_t beforeCommit = FailuresRecorded;
    AssetLease lease = Commit(std::move(staged));
    if (lease.IsValid())
        ClearFailure(recordPath);
    else if (FailuresRecorded != beforeCommit && LastFailedPath != recordPath)
        RecordFailure(recordPath, std::format("'{}' depends on '{}', which did not load", recordPath, LastFailedPath));
    else
        RecordFailure(recordPath, std::format("'{}' was staged but could not be committed", recordPath));
    return lease;
}

void AssetSystem::RecordFailure(std::string_view path, std::string reason)
{
    LastFailedPath = path;
    ++FailuresRecorded;
    const auto found = std::ranges::find(Failures, path, &LoadFailure::Path);
    if (found != Failures.end())
    {
        if (found->Reason == reason)
            return;
        found->Reason = std::move(reason);
    }
    else
    {
        Failures.push_back({ std::string(path), std::move(reason) });
    }
    ++FailuresVersion;
}

void AssetSystem::ClearFailure(std::string_view path)
{
    if (std::erase_if(Failures, [&](const LoadFailure& failure) { return failure.Path == path; }) > 0)
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

    return kind->Commit(std::move(staged));
}

bool AssetSystem::Reload(AssetStaging&& staged)
{
    const AssetKindRegistration* kind = KindRegistry.Find(staged.Record.Type);
    if (kind == nullptr || !kind->Reload)
    {
        Log.Error("AssetSystem: no reload registered for {} '{}'",
                  AssetTypeToString(staged.Record.Type), staged.Record.Path);
        return false;
    }

    return kind->Reload(std::move(staged));
}

const AssetRecord* AssetSystem::Resolve(std::string_view path, AssetType expectedType) const
{
    if (path.empty())
    {
        Log.Error("AssetSystem: empty asset path");
        return nullptr;
    }

    const AssetRecord* record = Registry.FindByPath(path);
    if (!record)
    {
        Log.Error("AssetSystem: failed to resolve asset '{}'", path);
        return nullptr;
    }

    if (record->Type != expectedType)
    {
        Log.Error("AssetSystem: expected {} asset, got {} for path {}",
                  AssetTypeToString(expectedType),
                  AssetTypeToString(record->Type),
                  record->Path);
        return nullptr;
    }

    return record;
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
