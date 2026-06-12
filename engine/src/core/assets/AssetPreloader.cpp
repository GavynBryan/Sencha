#include <core/assets/AssetPreloader.h>

#include <core/assets/AssetLoader.h>
#include <core/assets/AssetSystem.h>
#include <core/logging/LoggingProvider.h>
#include <jobs/AsyncTaskQueue.h>

#include <cassert>
#include <utility>

// -- AssetPreload ---------------------------------------------------------------

AssetPreload::AssetPreload(AssetSystem& assets)
    : Assets(assets)
{
}

AssetPreload::~AssetPreload()
{
    ReleaseAll();
}

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
    for (StaticMeshHandle handle : Meshes)
        Assets.ReleaseStaticMesh(handle);
    Meshes.clear();

    for (MaterialHandle handle : Materials)
        Assets.ReleaseMaterial(handle);
    Materials.clear();

    for (TextureHandle handle : Textures)
        Assets.ReleaseTexture(handle);
    Textures.clear();

    for (AudioClipHandle handle : AudioClips)
        Assets.ReleaseAudioClip(handle);
    AudioClips.clear();
}

void AssetPreload::Cancel()
{
    Cancelled = true;
    OnComplete = nullptr;
    DeferredMaterials.clear();
    ReleaseAll();
}

void AssetPreload::FinishOne(bool wave1, bool failed)
{
    if (failed)
        ++Failures;

    if (Pending > 0)
        --Pending;
    if (wave1 && Wave1Pending > 0)
        --Wave1Pending;
}

void AssetPreload::Store(StaticMeshHandle handle) { Meshes.push_back(handle); }
void AssetPreload::Store(MaterialHandle handle) { Materials.push_back(handle); }
void AssetPreload::Store(TextureHandle handle) { Textures.push_back(handle); }
void AssetPreload::Store(AudioClipHandle handle) { AudioClips.push_back(handle); }

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

std::shared_ptr<AssetPreload> AssetPreloader::Begin(std::span<const std::string> paths)
{
    std::shared_ptr<AssetPreload> preload(new AssetPreload(Assets));

    std::vector<AssetRecord> wave1;
    std::vector<AssetRecord> materials;

    for (const std::string& path : paths)
    {
        const AssetRecord* record = Registry.FindByPath(path);
        if (record == nullptr)
        {
            Log.Warn("AssetPreloader: '{}' has no registry record; sync fallback will report it", path);
            ++preload->Failures;
            continue;
        }

        switch (record->Type)
        {
        case AssetType::StaticMesh:
        {
            if (StaticMeshHandle handle = Assets.TryAcquireStaticMesh(path); handle.IsValid())
            {
                preload->Store(handle);
                continue;
            }
            break;
        }
        case AssetType::Texture:
        {
            if (TextureHandle handle = Assets.TryAcquireTexture(path); handle.IsValid())
            {
                preload->Store(handle);
                continue;
            }
            break;
        }
        case AssetType::Material:
        {
            if (MaterialHandle handle = Assets.TryAcquireMaterial(path); handle.IsValid())
            {
                preload->Store(handle);
                continue;
            }
            break;
        }
        case AssetType::Audio:
        {
            if (AudioClipHandle handle = Assets.TryAcquireAudioClip(path); handle.IsValid())
            {
                preload->Store(handle);
                continue;
            }
            break;
        }
        default:
            Log.Warn("AssetPreloader: '{}' has unsupported type {}; skipped",
                     path, AssetTypeToString(record->Type));
            ++preload->Failures;
            continue;
        }

        // Not resident. Only File records can be staged.
        if (record->SourceKind != AssetSourceKind::File)
        {
            Log.Warn("AssetPreloader: '{}' is {} and not resident; sync fallback will report it",
                     path, AssetSourceKindToString(record->SourceKind));
            ++preload->Failures;
            continue;
        }

        if (record->Type == AssetType::Material)
            materials.push_back(*record);
        else
            wave1.push_back(*record);
    }

    preload->Wave1Pending = static_cast<uint32_t>(wave1.size());
    preload->Pending = static_cast<uint32_t>(wave1.size() + materials.size());

    if (preload->Wave1Pending > 0)
    {
        // Materials wait for their textures; the last wave-1 commit submits them.
        preload->DeferredMaterials = std::move(materials);

        for (const AssetRecord& record : wave1)
        {
            if (InFlight.Begin(record.Path, preload) ==
                AssetInFlightTable<std::shared_ptr<AssetPreload>>::BeginResult::Started)
            {
                SubmitStagedLoad(record);
            }
        }
    }
    else if (!materials.empty())
    {
        SubmitMaterials(preload, std::move(materials));
    }

    return preload;
}

void AssetPreloader::SubmitStagedLoad(const AssetRecord& record)
{
    IAssetLoader* loader = Assets.LoaderFor(record.Type);
    assert(loader != nullptr && "AssetPreloader: Begin filtered types without loaders");

    IAssetSource* source = &Assets.DefaultSource();

    Tasks.Submit<AssetStaging>(
        // Work, task thread: pure decode against the byte seam. The record
        // is captured by value — plain data, no shared state.
        [loader, source, record]() -> AssetStaging
        {
            return loader->LoadStaged(record, *source);
        },
        // Commit, owner thread at the drain point.
        [this, type = record.Type, path = record.Path](AssetStaging staging)
        {
            OnAssetCommitted(type, path, std::move(staging));
        });
}

void AssetPreloader::SubmitMaterials(const std::shared_ptr<AssetPreload>& preload,
                                     std::vector<AssetRecord> records)
{
    for (const AssetRecord& record : records)
    {
        // Another preload may have made it resident while wave 1 ran.
        if (MaterialHandle handle = Assets.TryAcquireMaterial(record.Path); handle.IsValid())
        {
            if (preload->Cancelled)
                Assets.ReleaseMaterial(handle);
            else
                preload->Store(handle);
            preload->FinishOne(/*wave1*/ false, /*failed*/ false);
            continue;
        }

        if (InFlight.Begin(record.Path, preload) ==
            AssetInFlightTable<std::shared_ptr<AssetPreload>>::BeginResult::Started)
        {
            SubmitStagedLoad(record);
        }
    }
}

void AssetPreloader::OnAssetCommitted(AssetType type, const std::string& path, AssetStaging&& staging)
{
    std::vector<std::shared_ptr<AssetPreload>> waiters = InFlight.Finish(path);

    bool failed = !staging.IsValid();
    if (failed)
    {
        Log.Error("AssetPreloader: '{}' failed to stage: {}", path, staging.Error);
    }
    else
    {
        // Commit the staged payload, then drop the creation reference only
        // after every waiter has taken its own — releasing first could free
        // the asset out from under them.
        switch (type)
        {
        case AssetType::StaticMesh:
        {
            StaticMeshHandle created = Assets.StaticMeshLoaderRef().CommitTyped(std::move(staging));
            failed = !created.IsValid();
            for (const auto& waiter : waiters)
                DeliverToWaiter(waiter, type, path, /*wave1*/ true, failed);
            if (created.IsValid())
                Assets.ReleaseStaticMesh(created);
            break;
        }
        case AssetType::Texture:
        {
            TextureHandle created = Assets.TextureLoaderRef().CommitTyped(std::move(staging));
            failed = !created.IsValid();
            for (const auto& waiter : waiters)
                DeliverToWaiter(waiter, type, path, /*wave1*/ true, failed);
            if (created.IsValid())
                Assets.ReleaseTexture(created);
            break;
        }
        case AssetType::Material:
        {
            MaterialHandle created = Assets.MaterialLoaderRef().CommitTyped(std::move(staging));
            failed = !created.IsValid();
            for (const auto& waiter : waiters)
                DeliverToWaiter(waiter, type, path, /*wave1*/ false, failed);
            if (created.IsValid())
                Assets.ReleaseMaterial(created);
            break;
        }
        case AssetType::Audio:
        {
            AudioClipHandle created = Assets.AudioClipLoaderRef().CommitTyped(std::move(staging));
            failed = !created.IsValid();
            for (const auto& waiter : waiters)
                DeliverToWaiter(waiter, type, path, /*wave1*/ true, failed);
            if (created.IsValid())
                Assets.ReleaseAudioClip(created);
            break;
        }
        default:
            failed = true;
            break;
        }
    }

    if (failed)
    {
        const bool wave1 = (type != AssetType::Material);
        for (const auto& waiter : waiters)
            waiter->FinishOne(wave1, /*failed*/ true);
    }

    // Orchestration after bookkeeping: wave-2 submission, then completion.
    // Commits may Submit follow-up tasks by contract.
    for (const auto& waiter : waiters)
    {
        if (waiter->Wave1Pending == 0 && !waiter->DeferredMaterials.empty())
            SubmitMaterials(waiter, std::move(waiter->DeferredMaterials));
    }

    for (const auto& waiter : waiters)
    {
        if (waiter->IsComplete())
            waiter->FireOnComplete();
    }
}

void AssetPreloader::DeliverToWaiter(const std::shared_ptr<AssetPreload>& preload,
                                     AssetType type, const std::string& path,
                                     bool wave1, bool failed)
{
    if (failed)
    {
        preload->FinishOne(wave1, /*failed*/ true);
        return;
    }

    bool delivered = false;
    switch (type)
    {
    case AssetType::StaticMesh:
        if (StaticMeshHandle handle = Assets.TryAcquireStaticMesh(path); handle.IsValid())
        {
            if (preload->Cancelled)
                Assets.ReleaseStaticMesh(handle);
            else
                preload->Store(handle);
            delivered = true;
        }
        break;
    case AssetType::Texture:
        if (TextureHandle handle = Assets.TryAcquireTexture(path); handle.IsValid())
        {
            if (preload->Cancelled)
                Assets.ReleaseTexture(handle);
            else
                preload->Store(handle);
            delivered = true;
        }
        break;
    case AssetType::Material:
        if (MaterialHandle handle = Assets.TryAcquireMaterial(path); handle.IsValid())
        {
            if (preload->Cancelled)
                Assets.ReleaseMaterial(handle);
            else
                preload->Store(handle);
            delivered = true;
        }
        break;
    case AssetType::Audio:
        if (AudioClipHandle handle = Assets.TryAcquireAudioClip(path); handle.IsValid())
        {
            if (preload->Cancelled)
                Assets.ReleaseAudioClip(handle);
            else
                preload->Store(handle);
            delivered = true;
        }
        break;
    default:
        break;
    }

    preload->FinishOne(wave1, /*failed*/ !delivered);
}
