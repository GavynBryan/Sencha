#include "project/SourceReloadRoots.h"

#include <assets/cook/AssetImporter.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/hotreload/AssetSourceWatcher.h>

struct SourceReloadRoots::Root
{
    std::string Path;
    AssetSourceWatcher Watcher;
    AssetHotReloader Reloader;
};

SourceReloadRoots::SourceReloadRoots(LoggingProvider& logging, JobSystem* jobs, AsyncTaskQueue& tasks)
    : Logging(logging)
    , Tasks(tasks)
    , Importers(jobs)
{
}

SourceReloadRoots::~SourceReloadRoots() = default;

void SourceReloadRoots::AddRoot(std::string root,
                                std::vector<std::string> extensions,
                                AssetSystem& assets,
                                AssetRegistry& registry)
{
    auto entry = std::unique_ptr<Root>(new Root{
        root,
        AssetSourceWatcher(Logging, root, std::move(extensions)),
        AssetHotReloader(Logging, assets, registry, Importers.Registry(), Tasks, root),
    });
    entry->Watcher.Initialize();
    Roots.push_back(std::move(entry));
}

std::size_t SourceReloadRoots::Poll(std::chrono::steady_clock::time_point now)
{
    // On an interval, not per frame: the watcher is a content-hash-confirmed
    // mtime scan over whole roots.
    if (now < NextPoll)
        return 0;
    NextPoll = now + Interval;

    std::size_t reloaded = 0;
    for (auto& root : Roots)
    {
        for (const std::string& changed : root->Watcher.PollChanged())
        {
            // An import-settings sidecar edit re-cooks its source.
            std::string_view source = changed;
            if (source.ends_with(kImportSettingsSuffix))
                source.remove_suffix(kImportSettingsSuffix.size());
            root->Reloader.ReloadSource(source);
            ++reloaded;
        }
    }
    return reloaded;
}

bool SourceReloadRoots::ReloadSource(std::string_view root, std::string_view sourceRelPath)
{
    for (auto& entry : Roots)
    {
        if (entry->Path != root)
            continue;
        entry->Reloader.ReloadSource(sourceRelPath);
        return true;
    }
    return false;
}

void SourceReloadRoots::Rescan()
{
    for (auto& root : Roots)
        root->Watcher.Initialize();
}

std::size_t SourceReloadRoots::WatchedFileCount() const
{
    std::size_t count = 0;
    for (const auto& root : Roots)
        count += root->Watcher.WatchCount();
    return count;
}
