#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class AssetRegistry;
class AssetSystem;
class AsyncTaskQueue;
class ContentImporterSet;
class JobSystem;
class LoggingProvider;

//=============================================================================
// SourceReloadRoots. Dev-only, compiled under SENCHA_ENABLE_COOK.
//
// The save-and-look loop over one asset stack: content roots whose authored
// sources are watched and, when one changes, re-cooked into that stack, with
// the resident cooked artifacts swapped in place at the engine's async drain
// so live handles never change. RuntimeContent owns the instance over the
// engine's stack; a tool with a stack of its own owns another.
//
// Only files present when a root was added (or last rescanned) are watched;
// a rescan is a directory walk with a content hash per file, cheap for the
// roots a process mounts and the honest answer to "a file appeared".
// Owner-thread only.
//=============================================================================
class SourceReloadRoots
{
public:
    SourceReloadRoots(LoggingProvider& logging,
                      JobSystem* jobs,
                      AsyncTaskQueue& tasks,
                      AssetSystem& assets,
                      AssetRegistry& registry);
    ~SourceReloadRoots();

    SourceReloadRoots(const SourceReloadRoots&) = delete;
    SourceReloadRoots& operator=(const SourceReloadRoots&) = delete;

    // Watches `root` for sources with these extensions (leading dot). Adding a
    // root already watched widens its extensions. An empty list watches
    // nothing and still lets ReloadSource re-cook on demand.
    void AddRoot(std::string root, std::vector<std::string> extensions);

    // Polls every root at most once per Interval and re-cooks what changed.
    // An import-settings sidecar edit re-cooks the source it describes.
    // Returns how many sources were reloaded.
    std::size_t Poll(std::chrono::steady_clock::time_point now);

    // Re-cooks one source by hand. False when `root` is not one of ours.
    bool ReloadSource(std::string_view root, std::string_view sourceRelPath);

    // Re-walks every root so files created since are watched too.
    void Rescan();

    [[nodiscard]] std::size_t RootCount() const { return Roots.size(); }
    [[nodiscard]] std::size_t WatchedFileCount() const;

    std::chrono::milliseconds Interval{ 300 };

private:
    struct Root;

    LoggingProvider& Logging;
    AsyncTaskQueue& Tasks;
    AssetSystem& Assets;
    AssetRegistry& Registry;
    std::unique_ptr<ContentImporterSet> Importers;
    std::vector<std::unique_ptr<Root>> Roots;
    std::chrono::steady_clock::time_point NextPoll{};
};
