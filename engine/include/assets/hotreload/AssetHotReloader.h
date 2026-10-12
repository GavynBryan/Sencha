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

// Re-cooks a changed source and stages an in-place reload of each resident
// artifact through AssetSystem::StageReload; a source no importer claims is
// its own asset. Cook-only, owner thread (docs/assets/pipeline.md, Decision H).
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
    // Re-cooks every source whose cook recorded this file as an input. True when
    // it found any, which is what tells ReloadSource the edit was meaningful
    // even though the file is not an asset in its own right.
    [[nodiscard]] bool ReloadDependents(std::string_view sourceRelPath);

    void StageReload(const AssetRecord& record);

    Logger& Log;
    LoggingProvider& Logging;
    AssetSystem& Assets;
    AssetRegistry& Registry;
    const AssetImporterRegistry& Importers;
    AsyncTaskQueue& Tasks;
    std::string AssetsRoot;
};
