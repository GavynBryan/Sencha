#include <assets/animation/AnimationClipAssetLoader.h>

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/animation/AnimationClipSerializer.h>
#include <core/assets/AssetSystem.h>
#include <core/logging/LoggingProvider.h>

#include <format>
#include <utility>

AnimationClipAssetLoader::AnimationClipAssetLoader(LoggingProvider& logging,
                                                   AssetSystem& assets,
                                                   AnimationClipCache* cache,
                                                   SkeletonCache* skeletons)
    : Log(logging.GetLogger<AnimationClipAssetLoader>())
    , Assets(assets)
    , Cache(cache)
    , Skeletons(skeletons)
{
}

AssetStaging AnimationClipAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read animation clip source for '{}'", record.Path);
        return staging;
    }

    AnimationClipData data;
    std::string error;
    if (!LoadSanimFromBytes(bytes, data, &error))
    {
        staging.Error = std::format("failed to parse .sanim for '{}': {}", record.Path, error);
        return staging;
    }

    staging.Payload = std::move(data);
    return staging;
}

AssetCommitResult AnimationClipAssetLoader::Commit(AssetStaging&& staged)
{
    return { CommitTyped(std::move(staged)).IsValid() };
}

AnimationClipHandle AnimationClipAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("AnimationClipAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    AnimationClipData* data = std::any_cast<AnimationClipData>(&staged.Payload);
    if (data == nullptr)
    {
        Log.Error("AnimationClipAssetLoader: staging payload for '{}' is not AnimationClipData",
                  staged.Record.Path);
        return {};
    }

    if (!Cache || !Skeletons)
    {
        Log.Error("AnimationClipAssetLoader: missing cache for '{}'", staged.Record.Path);
        return {};
    }

    // Resolve the skeleton through the front door (loads inline if not yet
    // resident — the material→texture pattern) and hold the reference: the
    // clip→skeleton refcount chain.
    SkeletonHandle skeleton = Assets.LoadSkeleton(data->SkeletonPath);
    if (!skeleton.IsValid())
    {
        Log.Error("AnimationClipAssetLoader: clip '{}' references skeleton '{}' that failed to load",
                  staged.Record.Path, data->SkeletonPath);
        return {};
    }
    // LoadSkeleton already incremented the refcount -- wrap without attaching.
    SkeletonCacheHandle ownedSkeleton(Skeletons, skeleton, SkeletonCacheHandle::NoAttachTag{});

    // The clip alone can only bound joint indices by the joint cap; only here,
    // with the skeleton resolved, can they be checked against the real count.
    if (const SkeletonData* skeletonData = Skeletons->Get(skeleton))
    {
        const uint32_t jointCount = static_cast<uint32_t>(skeletonData->Joints.size());
        for (const AnimationJointTrack& track : data->Tracks)
        {
            if (track.JointIndex >= jointCount)
            {
                Log.Error("AnimationClipAssetLoader: clip '{}' track targets joint {} but skeleton "
                          "'{}' has {} joints", staged.Record.Path, track.JointIndex,
                          data->SkeletonPath, jointCount);
                return {};
            }
        }
    }

    AnimationClipHandle handle =
        Cache->Register(staged.Record.Path, std::move(*data), std::move(ownedSkeleton));
    if (!handle.IsValid())
        Log.Error("AnimationClipAssetLoader: failed to register animation clip '{}'",
                  staged.Record.Path);

    return handle;
}
