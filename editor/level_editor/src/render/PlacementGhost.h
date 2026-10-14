#pragma once

#include "SceneSourceStage.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

class LoggingProvider;
class PlacementSession;
struct BrushEvaluationPolicy;
struct RuntimeAssets;

// The pending prefab placement, ready to draw: a stage per prefab recently in
// hand, prepared once per version of its sources, posed every frame.
// Main-thread only (preparing leases assets). docs/editor/placement.md
class PlacementGhost
{
public:
    // Without assets, its stages are placements alone, with nothing to draw.
    PlacementGhost(RuntimeAssets* assets, LoggingProvider& logging);
    ~PlacementGhost();

    PlacementGhost(const PlacementGhost&) = delete;
    PlacementGhost& operator=(const PlacementGhost&) = delete;

    void SetContentRoots(std::vector<std::filesystem::path> roots);

    // Before this frame draws: the stage for the session's pending prefab,
    // posed and built under `policy`, keeping at most `budget` prepared.
    void Update(const PlacementSession& session, const BrushEvaluationPolicy& policy, std::size_t budget);
    // What to draw this frame; null when no prefab is pending or it did not
    // prepare.
    [[nodiscard]] const SceneSourceStage* Shown() const { return Current; }
    // Why the pending prefab did not prepare; empty when it did.
    [[nodiscard]] const std::string& Failure() const { return CurrentFailure; }

    // Prepares afresh, when next in hand, every prefab placing one of
    // `assetPaths`.
    void Invalidate(std::span<const std::string> assetPaths);

private:
    struct Entry
    {
        std::string Path;
        std::unique_ptr<SceneSourceStage> Stage = {};
        std::string Failure = {};
        std::uint64_t LastUsed = 0;
        bool Stale = false;
    };

    [[nodiscard]] Entry& Acquire(const std::string& path);
    void Prepare(Entry& entry);
    void Trim(std::size_t budget);

    RuntimeAssets* Assets = nullptr;
    LoggingProvider& Logging;
    std::vector<std::filesystem::path> ContentRoots;
    std::vector<Entry> Entries;
    std::uint64_t Clock = 0;
    std::string CurrentPath;
    SceneSourceStage* Current = nullptr;
    std::string CurrentFailure;
};
