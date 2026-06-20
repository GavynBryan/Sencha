#include <assets/cook/CookPrune.h>

#include <assets/cook/CookedCache.h>

#include <algorithm>
#include <string>
#include <vector>

std::size_t PruneOrphanedGeneratedArtifacts(
    CookedCacheIndex& index,
    const std::filesystem::path& assetsRoot,
    const std::function<bool(std::string_view sourceRelPath)>& sourceIsLive)
{
    // Snapshot the source keys first: erasing while iterating the map would
    // invalidate iterators. Sorted so the deletes happen in a stable order
    // (the set pruned is order-independent; the order keeps logs reproducible).
    std::vector<std::string> sources;
    sources.reserve(index.Entries().size());
    for (const auto& [source, entry] : index.Entries())
        sources.push_back(source);
    std::sort(sources.begin(), sources.end());

    const auto isLive = [&](const std::string& source) {
        if (sourceIsLive)
            return sourceIsLive(source);
        std::error_code ec;
        return std::filesystem::exists(assetsRoot / source, ec);
    };

    std::size_t pruned = 0;
    for (const std::string& source : sources)
    {
        if (isLive(source))
            continue;

        const CookedSourceEntry* entry = index.Find(source);
        for (const CookedArtifact& artifact : entry->Artifacts)
        {
            std::error_code ec;
            std::filesystem::remove(assetsRoot / artifact.FileRelPath, ec);
        }
        index.Erase(source);
        ++pruned;
    }

    return pruned;
}
