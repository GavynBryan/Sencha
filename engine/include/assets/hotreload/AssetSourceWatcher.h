#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class LoggingProvider;
class Logger;

//=============================================================================
// AssetSourceWatcher (Stage 6 hot reload, Decision H). Dev-only — compiled
// under SENCHA_ENABLE_COOK.
//
// Detection only, no reaction: it remembers a baseline (mtime + content hash)
// for a set of source files and reports which have *changed content* since
// the last poll. Splitting detection from the reload action keeps this fully
// headless-testable and lets the reaction (re-cook + GPU swap) live in
// AssetHotReloader.
//
// Polling is the deliberate mechanism: a content-hash-confirmed mtime check
// over a few hundred files is portable (no inotify/FSEvents/ReadDirectory…)
// and cheap. Throttling is the caller's job — call PollChanged on an interval
// (e.g. a few times a second), not every frame.
//
// Only files present at Initialize() are watched; a newly created or deleted
// file mid-session is out of scope (topology is editor territory, Decision H).
// Owner-thread only.
//=============================================================================
class AssetSourceWatcher
{
public:
    // `extensions` are the source extensions to watch (with the leading dot,
    // e.g. ".png"). The `.cooked/` directory is always skipped.
    AssetSourceWatcher(LoggingProvider& logging,
                       std::string assetsRoot,
                       std::vector<std::string> extensions);

    // Walks the assets root once, recording a baseline for every matching
    // source file. Call after the assets are cooked/scanned at startup.
    void Initialize();

    // Returns the assets-root-relative paths whose *content* changed since the
    // last call (mtime-gated, then content-hash-confirmed so a touch-only save
    // is ignored), updating the baseline as it goes.
    [[nodiscard]] std::vector<std::string> PollChanged();

    [[nodiscard]] std::size_t WatchCount() const { return Watched.size(); }

private:
    struct WatchEntry
    {
        std::filesystem::file_time_type Mtime{};
        uint64_t ContentHash = 0;
    };

    [[nodiscard]] bool HasWatchedExtension(std::string_view extension) const;

    Logger& Log;
    std::filesystem::path Root;
    std::vector<std::string> Extensions;
    std::unordered_map<std::string, WatchEntry> Watched; // keyed by rel path
};
