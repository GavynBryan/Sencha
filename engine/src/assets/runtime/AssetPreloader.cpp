#include <assets/runtime/AssetPreloader.h>

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <jobs/AsyncTaskQueue.h>

#include <cassert>
#include <utility>

// -- AssetPreload ---------------------------------------------------------------

void AssetPreload::SetOnComplete(std::function<void()> callback)
{
    if (Cancelled || !callback)
        return;

    if (IsComplete())
    {
        callback();
        return;
    }

    OnComplete = std::move(callback);
}

void AssetPreload::ReleaseAll()
{
    HeldAssets.clear();
}

void AssetPreload::Cancel()
{
    Cancelled = true;
    OnComplete = nullptr;
    ReleaseAll();
}

void AssetPreload::AddPending()
{
    ++Pending;
}

void AssetPreload::FinishOne(bool failed)
{
    if (failed)
        ++Failures;

    if (Pending > 0)
        --Pending;
}

void AssetPreload::Store(AssetLease lease)
{
    // A cancelled preload lets the lease die here, which releases the
    // reference the commit took.
    if (!Cancelled && lease.IsValid())
        HeldAssets.push_back(std::move(lease));
}

void AssetPreload::FireOnComplete()
{
    if (Cancelled || !OnComplete)
        return;

    auto callback = std::move(OnComplete);
    OnComplete = nullptr;
    callback();
}

// -- AssetPreloader ---------------------------------------------------------------

AssetPreloader::AssetPreloader(LoggingProvider& logging,
                               AssetRegistry& registry,
                               AssetSystem& assets,
                               AsyncTaskQueue& tasks)
    : Log(logging.GetLogger<AssetPreloader>())
    , Registry(registry)
    , Assets(assets)
    , Tasks(tasks)
{
}

bool AssetPreloader::CanStage(const AssetRecord& record) const
{
    const AssetKindRegistration* kind = Assets.Kinds().Find(record.Type);
    // Only File records have bytes to stage; a procedural asset exists only
    // because something registered it directly.
    return kind != nullptr && kind->IsLoadable()
        && record.SourceKind == AssetSourceKind::File;
}

std::shared_ptr<AssetPreload> AssetPreloader::Begin(std::span<const std::string> paths)
{
    std::shared_ptr<AssetPreload> preload(new AssetPreload());

    for (const std::string& path : paths)
    {
        const AssetRecord* record = Registry.FindByPath(path);
        if (record == nullptr)
        {
            Log.Warn("AssetPreloader: '{}' has no registry record; sync fallback will report it",
                     path);
            ++preload->Failures;
            continue;
        }

        if (AssetLease resident = Assets.TryAcquireLease(record->Path, record->Type))
        {
            preload->Store(std::move(resident));
            continue;
        }

        if (!CanStage(*record))
        {
            Log.Warn("AssetPreloader: '{}' ({}, {}) cannot be staged; "
                     "sync fallback will report it",
                     path, AssetTypeToString(record->Type),
                     AssetSourceKindToString(record->SourceKind));
            ++preload->Failures;
            continue;
        }

        preload->AddPending();
        RequestLoad(*record, LoadWaiter{ .Preload = preload });
    }

    return preload;
}

void AssetPreloader::RequestLoad(const AssetRecord& record, LoadWaiter waiter)
{
    if (AssetLease resident = Assets.TryAcquireLease(record.Path, record.Type))
    {
        if (waiter.IsDependency())
        {
            OnDependencyFinished(waiter.ParentPath, std::move(resident), /*failed*/ false);
        }
        else if (waiter.Preload)
        {
            waiter.Preload->Store(std::move(resident));
            waiter.Preload->FinishOne(/*failed*/ false);
        }
        return;
    }

    if (!CanStage(record))
    {
        Deliver(waiter, record.Type, record.Path, /*failed*/ true);
        return;
    }

    if (InFlight.Begin(record.Path, std::move(waiter))
        == AssetInFlightTable<LoadWaiter>::BeginResult::Started)
    {
        SubmitStagedLoad(record);
    }
}

void AssetPreloader::SubmitStagedLoad(const AssetRecord& record)
{
    IAssetStager* stager = Assets.LoaderFor(record.Type);
    assert(stager != nullptr && "AssetPreloader: CanStage admitted a kind with no stager");

    IAssetSource* source = &Assets.DefaultSource();

    Tasks.Submit<AssetStaging>(
        // Work, task thread: pure decode against the byte seam. The record
        // is captured by value — plain data, no shared state.
        [stager, source, record]() -> AssetStaging
        {
            return stager->LoadStaged(record, *source);
        },
        // Commit, owner thread at the drain point.
        [this, type = record.Type, path = record.Path](AssetStaging staging)
        {
            OnAssetStaged(type, path, std::move(staging));
        });
}

void AssetPreloader::OnAssetStaged(AssetType type,
                                   const std::string& path,
                                   AssetStaging&& staging)
{
    if (!staging.IsValid())
    {
        Log.Error("AssetPreloader: '{}' failed to stage: {}", path, staging.Error);
        CompleteLoad(type, path, /*failed*/ true);
        return;
    }

    std::vector<AssetRecord> toLoad;
    std::vector<AssetLease> residentDependencies;
    std::vector<std::string> edges;
    std::unordered_set<std::string> unique;

    for (const AssetRef& dependency : staging.Dependencies)
    {
        if (!dependency.IsValid() || !unique.insert(dependency.Path).second)
            continue;

        const AssetRecord* record = Registry.FindByPath(dependency.Path);
        if (record == nullptr || record->Type != dependency.Type)
        {
            Log.Error("AssetPreloader: '{}' declares unresolvable dependency '{}'",
                      path, dependency.Path);
            CompleteLoad(type, path, /*failed*/ true);
            return;
        }

        // Checked before the edge is recorded: a cycle would otherwise leave
        // both payloads waiting on each other forever.
        if (dependency.Path == path || WouldCreateCycle(path, dependency.Path))
        {
            Log.Error("AssetPreloader: dependency cycle between '{}' and '{}'",
                      path, dependency.Path);
            CompleteLoad(type, path, /*failed*/ true);
            return;
        }

        edges.push_back(dependency.Path);

        if (AssetLease resident = Assets.TryAcquireLease(dependency.Path, dependency.Type))
            residentDependencies.push_back(std::move(resident));
        else
            toLoad.push_back(*record);
    }

    DependencyEdges[path] = std::move(edges);

    PendingCommit pending;
    pending.Type = type;
    pending.Staging = std::move(staging);
    pending.DependencyLeases = std::move(residentDependencies);
    pending.PendingDependencies = static_cast<uint32_t>(toLoad.size());
    PendingCommits.emplace(path, std::move(pending));

    if (toLoad.empty())
    {
        CommitReady(path);
        return;
    }

    for (const AssetRecord& dependency : toLoad)
        RequestLoad(dependency, LoadWaiter{ .ParentPath = path });
}

void AssetPreloader::OnDependencyFinished(const std::string& parentPath,
                                          AssetLease dependency,
                                          bool failed)
{
    auto it = PendingCommits.find(parentPath);
    if (it == PendingCommits.end())
        return;

    PendingCommit& pending = it->second;
    if (failed || !dependency.IsValid())
        pending.DependencyFailed = true;
    else
        pending.DependencyLeases.push_back(std::move(dependency));

    if (pending.PendingDependencies > 0)
        --pending.PendingDependencies;

    if (pending.PendingDependencies != 0)
        return;

    if (pending.DependencyFailed)
    {
        const AssetType type = pending.Type;
        CompleteLoad(type, parentPath, /*failed*/ true);
        return;
    }

    CommitReady(parentPath);
}

void AssetPreloader::CommitReady(const std::string& path)
{
    auto it = PendingCommits.find(path);
    if (it == PendingCommits.end())
        return;

    // Moved out before the commit runs: the commit resolves refs through the
    // front door and must not find this entry mid-flight.
    PendingCommit pending = std::move(it->second);
    PendingCommits.erase(it);
    DependencyEdges.erase(path);

    const AssetLease created = Assets.Commit(std::move(pending.Staging));
    const bool failed = !created.IsValid();
    if (failed)
        Log.Error("AssetPreloader: '{}' failed to commit", path);

    // The dependency leases outlive the commit that needed them and release
    // here; the committed asset holds its own references by now.
    CompleteLoad(pending.Type, path, failed);
}

void AssetPreloader::CompleteLoad(AssetType type, const std::string& path, bool failed)
{
    PendingCommits.erase(path);
    DependencyEdges.erase(path);

    ++DeliveryDepth;
    for (const LoadWaiter& waiter : InFlight.Finish(path))
        Deliver(waiter, type, path, failed);
    --DeliveryDepth;

    // Outermost delivery only: a dependency chain unwinds through nested
    // CompleteLoad calls, and a completion callback must not run while that
    // bookkeeping is still on the stack.
    if (DeliveryDepth != 0)
        return;

    std::vector<std::shared_ptr<AssetPreload>> finished = std::move(FinishedThisDrain);
    FinishedThisDrain.clear();
    for (const std::shared_ptr<AssetPreload>& preload : finished)
        preload->FireOnComplete();
}

void AssetPreloader::Deliver(const LoadWaiter& waiter,
                             AssetType type,
                             const std::string& path,
                             bool failed)
{
    AssetLease lease;
    if (!failed)
        lease = Assets.TryAcquireLease(path, type);

    const bool deliveryFailed = failed || !lease.IsValid();

    if (waiter.IsDependency())
    {
        OnDependencyFinished(waiter.ParentPath, std::move(lease), deliveryFailed);
        return;
    }

    if (!waiter.Preload)
        return;

    waiter.Preload->Store(std::move(lease));
    waiter.Preload->FinishOne(deliveryFailed);
    if (waiter.Preload->IsComplete())
        FinishedThisDrain.push_back(waiter.Preload);
}

bool AssetPreloader::WouldCreateCycle(std::string_view parent,
                                      std::string_view dependency) const
{
    std::unordered_set<std::string> visited;
    return Reaches(dependency, parent, visited);
}

bool AssetPreloader::Reaches(std::string_view from,
                             std::string_view target,
                             std::unordered_set<std::string>& visited) const
{
    if (from == target)
        return true;

    const std::string key(from);
    if (!visited.insert(key).second)
        return false;

    auto it = DependencyEdges.find(key);
    if (it == DependencyEdges.end())
        return false;

    for (const std::string& dependency : it->second)
    {
        if (Reaches(dependency, target, visited))
            return true;
    }

    return false;
}
