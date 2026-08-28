#include <assets/runtime/AssetSystem.h>

#include <assets/scene/SceneCache.h>
#include <core/logging/LoggingProvider.h>

#include <utility>

// Cooked scene loads. Scenes are file-sourced only: nothing generates a scene
// procedurally at runtime, and a cooked artifact keeps its source's virtual
// path like every other kind.

SceneHandle AssetSystem::LoadScene(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::Scene);
    if (record == nullptr)
        return {};

    if (Scenes == nullptr)
    {
        Log.Error("AssetSystem: missing SceneCache for scene asset {}", record->Path);
        return {};
    }

    if (SceneHandle existing = Scenes->Acquire(record->Path); existing.IsValid())
        return existing;

    AssetStaging staging = SceneLoader.LoadStaged(*record, Source);
    if (!staging.IsValid())
    {
        Log.Error("AssetSystem: {}", staging.Error);
        return {};
    }

    return SceneLoader.CommitTyped(std::move(staging));
}

SceneHandle AssetSystem::TryAcquireScene(std::string_view path)
{
    return Scenes != nullptr ? Scenes->Acquire(path) : SceneHandle{};
}

void AssetSystem::ReleaseScene(SceneHandle handle)
{
    if (Scenes != nullptr)
        Scenes->Release(handle);
}

AssetStaging AssetSystem::StageScene(const AssetRecord& record)
{
    return SceneLoader.LoadStaged(record, Source);
}

SceneHandle AssetSystem::CommitScene(AssetStaging&& staged)
{
    return SceneLoader.CommitTyped(std::move(staged));
}

const SmapContents* AssetSystem::GetSceneContents(SceneHandle handle) const
{
    return Scenes != nullptr ? Scenes->Get(handle) : nullptr;
}

std::shared_ptr<const SmapContents>
AssetSystem::GetSceneContentsShared(SceneHandle handle) const
{
    return Scenes != nullptr ? Scenes->GetShared(handle) : nullptr;
}
