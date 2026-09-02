#pragma once

#include <anim/AnimationClipHandle.h>
#include <core/assets/AssetStager.h>
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
// the front door it is handed, then register with AnimationClipCache holding
// that skeleton reference (the clip→skeleton refcount chain, the
// material→texture pattern). Payload type: AnimationClipData.
//=============================================================================
class AnimationClipAssetLoader final : public IAssetStager
{
public:
    AnimationClipAssetLoader(LoggingProvider& logging,
                             AnimationClipCache* cache,
                             SkeletonCache* skeletons);

    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;

    [[nodiscard]] AnimationClipHandle CommitTyped(AssetStaging&& staged, AssetSystem& assets);

private:
    Logger& Log;
    AnimationClipCache* Cache = nullptr;
    SkeletonCache* Skeletons = nullptr;
};
