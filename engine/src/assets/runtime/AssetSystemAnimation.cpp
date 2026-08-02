#include <assets/runtime/AssetSystem.h>

#include <core/logging/LoggingProvider.h>

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>

#include <utility>

// Skeleton and animation clip loads. A clip resolves against its skeleton,
// so the two share a translation unit.

std::string_view AssetSystem::GetPathForSkeleton(SkeletonHandle handle) const
{
    return Skeletons ? Skeletons->GetName(handle) : std::string_view{};
}

std::string_view AssetSystem::GetPathForAnimationClip(AnimationClipHandle handle) const
{
    return AnimationClips ? AnimationClips->GetName(handle) : std::string_view{};
}

SkeletonHandle AssetSystem::LoadSkeleton(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::Skeleton);
    if (!record)
        return {};

    if (!Skeletons)
    {
        Log.Error("AssetSystem: missing SkeletonCache for skeleton asset {}", record->Path);
        return {};
    }

    if (SkeletonHandle existing = Skeletons->Acquire(record->Path); existing.IsValid())
        return existing;

    if (record->SourceKind != AssetSourceKind::File)
    {
        Log.Error("AssetSystem: skeleton cache has no runtime resource for path {}", record->Path);
        return {};
    }

    AssetStaging staging = SkelLoader.LoadStaged(*record, Source);
    if (!staging.IsValid())
    {
        Log.Error("AssetSystem: {}", staging.Error);
        return {};
    }

    return SkelLoader.CommitTyped(std::move(staging));
}

AnimationClipHandle AssetSystem::LoadAnimationClip(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::AnimationClip);
    if (!record)
        return {};

    if (!AnimationClips)
    {
        Log.Error("AssetSystem: missing AnimationClipCache for animation asset {}", record->Path);
        return {};
    }

    if (AnimationClipHandle existing = AnimationClips->Acquire(record->Path); existing.IsValid())
        return existing;

    if (record->SourceKind != AssetSourceKind::File)
    {
        Log.Error("AssetSystem: animation clip cache has no runtime resource for path {}", record->Path);
        return {};
    }

    AssetStaging staging = AnimLoader.LoadStaged(*record, Source);
    if (!staging.IsValid())
    {
        Log.Error("AssetSystem: {}", staging.Error);
        return {};
    }

    return AnimLoader.CommitTyped(std::move(staging));
}

SkeletonHandle AssetSystem::TryAcquireSkeleton(std::string_view path)
{
    return Skeletons ? Skeletons->Acquire(path) : SkeletonHandle{};
}

AnimationClipHandle AssetSystem::TryAcquireAnimationClip(std::string_view path)
{
    return AnimationClips ? AnimationClips->Acquire(path) : AnimationClipHandle{};
}

void AssetSystem::ReleaseSkeleton(SkeletonHandle handle)
{
    if (Skeletons)
        Skeletons->Release(handle);
}

void AssetSystem::ReleaseAnimationClip(AnimationClipHandle handle)
{
    if (AnimationClips)
        AnimationClips->Release(handle);
}
