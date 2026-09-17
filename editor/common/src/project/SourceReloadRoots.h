#pragma once

#include <assets/cook/ContentImporters.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class AssetRegistry;
class AssetSystem;
class AsyncTaskQueue;
class JobSystem;
class LoggingProvider;

//=============================================================================
// SourceReloadRoots
//
// The save-and-look loop: content roots whose authored sources are watched
// and, when one changes, re-cooked into the asset stack that mounted them,
// with the resident cooked artifacts swapped in place at the engine's async
// drain so live handles never change.
//
// One assembly for every editor. Kyusu and Shudei each built the same
// watcher-plus-reloader-plus-importer-set-plus-poll by hand; a third copy is
// where the drift starts. A root is added with the asset stack it was mounted
// into -- the engine's for an editor's own authored UI, the editor's for a
// project's content -- because the reloader must swap the slot that is
// actually resident.
//
// Only files present when a root was added (or last rescanned) are watched;
// a rescan is a directory walk with a content hash per file, cheap for the
// roots an editor mounts and the honest answer to "a file appeared".
//=============================================================================
class SourceReloadRoots
{
public:
    SourceReloadRoots(LoggingProvider& logging, JobSystem* jobs, AsyncTaskQueue& tasks);
    ~SourceReloadRoots();

    SourceReloadRoots(const SourceReloadRoots&) = delete;
    SourceReloadRoots& operator=(const SourceReloadRoots&) = delete;

    // Watches `root` for sources with these extensions (leading dot) and
    // re-cooks a changed one into `assets`/`registry`. An empty extension
    // list watches nothing and still lets ReloadSource re-cook on demand.
    void AddRoot(std::string root,
                 std::vector<std::string> extensions,
                 AssetSystem& assets,
                 AssetRegistry& registry);

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

    std::chrono::milliseconds Interval{ 500 };

private:
    struct Root;

    LoggingProvider& Logging;
    AsyncTaskQueue& Tasks;
    ContentImporterSet Importers;
    std::vector<std::unique_ptr<Root>> Roots;
    std::chrono::steady_clock::time_point NextPoll{};
};
