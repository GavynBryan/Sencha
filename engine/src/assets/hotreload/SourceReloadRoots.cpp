#include <assets/hotreload/SourceReloadRoots.h>

#include <assets/cook/AssetImporter.h>
#include <assets/cook/ContentImporters.h>
#include <assets/hotreload/AssetHotReloader.h>
#include <assets/hotreload/AssetSourceWatcher.h>

#include <algorithm>

struct SourceReloadRoots::Root
{
    std::string Path;
    std::vector<std::string> Extensions;
    AssetSourceWatcher Watcher;
    AssetHotReloader Reloader;
};

SourceReloadRoots::SourceReloadRoots(LoggingProvider& logging,
                                     JobSystem* jobs,
                                     AsyncTaskQueue& tasks,
                                     AssetSystem& assets,
                                     AssetRegistry& registry)
    : Logging(logging)
    , Tasks(tasks)
    , Assets(assets)
    , Registry(registry)
    , Importers(std::make_unique<ContentImporterSet>(jobs))
{
}

SourceReloadRoots::~SourceReloadRoots() = default;

void SourceReloadRoots::AddRoot(std::string root, std::vector<std::string> extensions)
{
    const auto existing = std::find_if(Roots.begin(), Roots.end(),
                                       [&](const std::unique_ptr<Root>& entry) { return entry->Path == root; });
    if (existing != Roots.end())
    {
        for (const std::string& extension : (*existing)->Extensions)
            if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end())
                extensions.push_back(extension);
        Roots.erase(existing);
    }

    // Built in place: neither the watcher nor the reloader is movable.
    auto entry = std::unique_ptr<Root>(new Root{
        root,
        extensions,
        AssetSourceWatcher(Logging, root, extensions),
        AssetHotReloader(Logging, Assets, Registry, Importers->Registry(), Tasks, root),
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
