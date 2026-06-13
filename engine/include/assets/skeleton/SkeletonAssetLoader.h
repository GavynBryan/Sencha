#pragma once

#include <anim/SkeletonHandle.h>
#include <core/assets/AssetLoader.h>
#include <core/logging/Logger.h>

class LoggingProvider;
class SkeletonCache;

//=============================================================================
// SkeletonAssetLoader (docs/assets/pipeline.md, Decision J)
//
// Staged-load contract for .sskel. Stage: bytes -> SkeletonData (a validated
// parse — pure, no engine state). Commit: register with SkeletonCache.
// Payload type: SkeletonData. Fully CPU-side, no GPU half — the MaterialCache
// pattern for a leaf asset.
//=============================================================================
class SkeletonAssetLoader final : public IAssetLoader
{
public:
    SkeletonAssetLoader(LoggingProvider& logging, SkeletonCache* cache);

    [[nodiscard]] AssetType Type() const override { return AssetType::Skeleton; }
    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;
    AssetCommitResult Commit(AssetStaging&& staged) override;

    [[nodiscard]] SkeletonHandle CommitTyped(AssetStaging&& staged);

private:
    Logger& Log;
    SkeletonCache* Cache = nullptr;
};
