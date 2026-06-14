#include <assets/skinned_mesh/SkinnedMeshAssetLoader.h>

#include <anim/SkeletonCache.h>
#include <core/assets/AssetSystem.h>
#include <core/logging/LoggingProvider.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>

#include <format>
#include <utility>

SkinnedMeshAssetLoader::SkinnedMeshAssetLoader(LoggingProvider& logging,
                                               AssetSystem& assets,
                                               SkinnedMeshCache* cache,
                                               SkeletonCache* skeletons)
    : Log(logging.GetLogger<SkinnedMeshAssetLoader>())
    , Assets(assets)
    , Cache(cache)
    , Skeletons(skeletons)
    , FileLoader(logging)
{
}

AssetStaging SkinnedMeshAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read skinned mesh source for '{}'", record.Path);
        return staging;
    }

    SkinnedMeshData data;
    if (!FileLoader.LoadSkinnedFromBytes(bytes, data))
    {
        staging.Error = std::format("failed to parse .skmesh data for '{}'", record.Path);
        return staging;
    }

    staging.Payload = std::move(data);
    return staging;
}

AssetCommitResult SkinnedMeshAssetLoader::Commit(AssetStaging&& staged)
{
    return { CommitTyped(std::move(staged)).IsValid() };
}

SkinnedMeshHandle SkinnedMeshAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("SkinnedMeshAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    SkinnedMeshData* data = std::any_cast<SkinnedMeshData>(&staged.Payload);
    if (data == nullptr)
    {
        Log.Error("SkinnedMeshAssetLoader: staging payload for '{}' is not SkinnedMeshData",
                  staged.Record.Path);
        return {};
    }

    if (!Cache || !Skeletons)
    {
        Log.Error("SkinnedMeshAssetLoader: missing cache for '{}'", staged.Record.Path);
        return {};
    }

    // Resolve and hold the skeleton (loads inline if not yet resident — the
    // material→texture pattern), then bind the mesh→skeleton chain.
    SkeletonHandle skeleton = Assets.LoadSkeleton(data->Skinning.SkeletonPath);
    if (!skeleton.IsValid())
    {
        Log.Error("SkinnedMeshAssetLoader: skinned mesh '{}' references skeleton '{}' that "
                  "failed to load", staged.Record.Path, data->Skinning.SkeletonPath);
        return {};
    }
    // LoadSkeleton already incremented the refcount -- wrap without attaching.
    SkeletonCacheHandle ownedSkeleton(Skeletons, skeleton, SkeletonCacheHandle::NoAttachTag{});

    // The joint count the mesh was cooked against must match the skeleton it
    // resolved to; a mismatch means mesh and skeleton are out of sync.
    if (const SkeletonData* skeletonData = Skeletons->Get(skeleton))
    {
        if (data->Skinning.JointCount != skeletonData->Joints.size())
        {
            Log.Error("SkinnedMeshAssetLoader: mesh '{}' was cooked for {} joints but skeleton "
                      "'{}' has {}", staged.Record.Path, data->Skinning.JointCount,
                      data->Skinning.SkeletonPath, skeletonData->Joints.size());
            return {};
        }
    }

    SkinnedMeshHandle handle =
        Cache->CreateFromData(staged.Record.Path, *data, std::move(ownedSkeleton));
    if (!handle.IsValid())
        Log.Error("SkinnedMeshAssetLoader: failed to upload skinned mesh '{}'", staged.Record.Path);

    return handle;
}
