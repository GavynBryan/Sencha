#pragma once

#include <string>
#include <string_view>

class AssetImporterRegistry;
class AssetRegistry;
class AssetSystem;
class AsyncTaskQueue;
class Logger;
class LoggingProvider;
struct AssetRecord;

//=============================================================================
// AssetHotReloader (Stage 6 hot reload, Decision H). Dev-only — compiled
// under SENCHA_ENABLE_COOK.
//
// The reaction half (AssetSourceWatcher is the detection half): given a
// changed source file, re-cooks it and stages an in-place reload of each of
// its cooked artifacts that is currently resident. The reload mirrors the
// staged-load contract — LoadStaged (decode) on a task thread, a commit that
// *swaps* the existing cache slot in place at the drain point — so live
// handles never change and the old GPU resource retires through the deletion
// queue. Owner-thread only.
//
// Stage 6a wires textures. Static/skinned meshes (6b) and materials (6c) slot
// into StageReload once their loaders gain CommitReload and their caches gain
// ReloadInPlace.
//=============================================================================
class AssetHotReloader
{
public:
    AssetHotReloader(LoggingProvider& logging,
                     AssetSystem& assets,
                     AssetRegistry& registry,
                     const AssetImporterRegistry& importers,
                     AsyncTaskQueue& tasks,
                     std::string assetsRoot);

    // Re-cooks `sourceRelPath` (assets-root-relative) and stages reloads of
    // its resident cooked artifacts. A failed re-cook leaves the live assets
    // untouched (best-effort, logged).
    void ReloadSource(std::string_view sourceRelPath);

private:
    void StageReload(const AssetRecord& record);

    Logger& Log;
    LoggingProvider& Logging;
    AssetSystem& Assets;
    AssetRegistry& Registry;
    const AssetImporterRegistry& Importers;
    AsyncTaskQueue& Tasks;
    std::string AssetsRoot;
};
