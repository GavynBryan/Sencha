#pragma once

#include <anim/AnimationClipHandle.h>
#include <core/assets/AssetLoader.h>
#include <core/logging/Logger.h>

class AssetSystem;
class AnimationClipCache;
class LoggingProvider;
class SkeletonCache;

//=============================================================================
// AnimationClipAssetLoader (docs/assets/pipeline.md, Decision J)
//
// Staged-load contract for .sanim. Stage: bytes -> AnimationClipData (a
// validated parse — pure). Commit: resolve the clip's skeleton ref through
// the front door, then register with AnimationClipCache holding that
// skeleton reference (the clip→skeleton refcount chain, the material→texture
// pattern). Payload type: AnimationClipData.
//=============================================================================
class AnimationClipAssetLoader final : public IAssetLoader
{
public:
    AnimationClipAssetLoader(LoggingProvider& logging,
                             AssetSystem& assets,
                             AnimationClipCache* cache,
                             SkeletonCache* skeletons);

    [[nodiscard]] AssetType Type() const override { return AssetType::AnimationClip; }
    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;
    AssetCommitResult Commit(AssetStaging&& staged) override;

    [[nodiscard]] AnimationClipHandle CommitTyped(AssetStaging&& staged);

private:
    Logger& Log;
    AssetSystem& Assets;
    AnimationClipCache* Cache = nullptr;
    SkeletonCache* Skeletons = nullptr;
};
