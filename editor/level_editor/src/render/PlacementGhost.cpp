#include "PlacementGhost.h"

#include "SceneSourceStage.h"

#include "brush/BrushEvaluation.h"
#include "document/placement/PlacementSession.h"

#include <algorithm>

PlacementGhost::PlacementGhost(RuntimeAssets* assets, LoggingProvider& logging)
    : Assets(assets)
    , Logging(logging)
{
}

PlacementGhost::~PlacementGhost() = default;

void PlacementGhost::SetContentRoots(std::vector<std::filesystem::path> roots)
{
    if (roots == ContentRoots)
        return;
    ContentRoots = std::move(roots);
    Entries.clear();
    CurrentPath.clear();
    Current = nullptr;
}

void PlacementGhost::Update(const PlacementSession& session, const BrushEvaluationPolicy& policy,
                            std::size_t budget)
{
    ++Clock;
    const PendingPlacement* pending = session.Pending();
    if (pending == nullptr || pending->Subject.What != PlacementSubject::Kind::SceneSource)
    {
        CurrentPath.clear();
        Current = nullptr;
        CurrentFailure.clear();
        Trim(budget);
        return;
    }

    // Coming into hand is when a prefab's sources are checked; a frame that
    // keeps it in hand only poses it.
    const bool arriving = pending->Subject.Key != CurrentPath;
    Entry& entry = Acquire(pending->Subject.Key);
    if (arriving && (entry.Stale || entry.Stage == nullptr || entry.Stage->ChangedOnDisk()))
        Prepare(entry);
    entry.LastUsed = Clock;
    CurrentPath = entry.Path;
    Current = entry.Stage.get();
    CurrentFailure = entry.Failure;
    Trim(budget);

    if (Current != nullptr)
    {
        Current->Pose(pending->Placement);
        Current->Staged().SetEvaluationPolicy(policy);
        Current->Staged().Build();
    }
}

void PlacementGhost::Invalidate(std::span<const std::string> assetPaths)
{
    for (Entry& entry : Entries)
        if (std::ranges::find(assetPaths, entry.Path) != assetPaths.end()
            || (entry.Stage != nullptr && entry.Stage->DependsOnAny(assetPaths)))
            entry.Stale = true;
    // The one in hand prepares afresh on the next frame.
    if (const auto held = std::ranges::find(Entries, CurrentPath, &Entry::Path);
        held != Entries.end() && held->Stale)
        CurrentPath.clear();
}

PlacementGhost::Entry& PlacementGhost::Acquire(const std::string& path)
{
    if (const auto found = std::ranges::find(Entries, path, &Entry::Path); found != Entries.end())
        return *found;
    return Entries.emplace_back(Entry{ .Path = path });
}

void PlacementGhost::Prepare(Entry& entry)
{
    entry.Failure.clear();
    entry.Stale = false;
    entry.Stage = SceneSourceStage::Prepare(entry.Path, ContentRoots, Assets, Logging, &entry.Failure);
}

void PlacementGhost::Trim(std::size_t budget)
{
    while (Entries.size() > std::max<std::size_t>(budget, 1))
    {
        const auto oldest = std::ranges::min_element(Entries, {}, &Entry::LastUsed);
        if (oldest->Path == CurrentPath)
            return;
        Entries.erase(oldest);
    }
}
