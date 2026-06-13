#include <assets/hotreload/AssetSourceWatcher.h>

#include <core/assets/AssetRegistry.h> // kCookedCacheDirName
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>

#include <algorithm>

namespace
{
    struct AssetSourceWatcherTag {};
}

AssetSourceWatcher::AssetSourceWatcher(LoggingProvider& logging,
                                       std::string assetsRoot,
                                       std::vector<std::string> extensions)
    : Log(logging.GetLogger<AssetSourceWatcherTag>())
    , Root(std::move(assetsRoot))
    , Extensions(std::move(extensions))
{
}

bool AssetSourceWatcher::HasWatchedExtension(std::string_view extension) const
{
    return std::find(Extensions.begin(), Extensions.end(), extension) != Extensions.end();
}

void AssetSourceWatcher::Initialize()
{
    Watched.clear();

    std::error_code ec;
    if (!std::filesystem::is_directory(Root, ec))
    {
        Log.Warn("AssetSourceWatcher: assets root '{}' is not a directory", Root.generic_string());
        return;
    }

    for (std::filesystem::recursive_directory_iterator it(Root, ec), end; it != end; it.increment(ec))
    {
        if (ec) { ec.clear(); continue; }

        if (!it->is_regular_file(ec))
        {
            if (it->path().filename() == kCookedCacheDirName)
                it.disable_recursion_pending();
            continue;
        }
        if (!HasWatchedExtension(it->path().extension().generic_string()))
            continue;

        const std::string rel = std::filesystem::relative(it->path(), Root, ec).generic_string();
        WatchEntry entry;
        entry.Mtime = std::filesystem::last_write_time(it->path(), ec);
        (void)HashFileContents(it->path().generic_string(), entry.ContentHash);
        Watched.emplace(rel, entry);
    }

    Log.Debug("AssetSourceWatcher: watching {} source file(s) under '{}'",
              Watched.size(), Root.generic_string());
}

std::vector<std::string> AssetSourceWatcher::PollChanged()
{
    std::vector<std::string> changed;

    for (auto& [rel, entry] : Watched)
    {
        const std::filesystem::path full = Root / rel;
        std::error_code ec;

        const auto mtime = std::filesystem::last_write_time(full, ec);
        if (ec)
            continue; // unreadable (e.g. mid-save); try again next poll
        if (mtime == entry.Mtime)
            continue; // unchanged

        // Mtime moved — confirm the *content* actually changed before
        // reacting, so a touch or no-op save doesn't trigger a reload.
        uint64_t hash = 0;
        if (!HashFileContents(full.generic_string(), hash))
            continue;

        entry.Mtime = mtime;
        if (hash == entry.ContentHash)
            continue; // touch-only; baseline mtime already updated

        entry.ContentHash = hash;
        changed.push_back(rel);
    }

    return changed;
}
