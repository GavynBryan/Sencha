#include "MoveEntitiesToZoneCommand.h"

#include "document/EditorDocument.h"
#include "document/WorldDocument.h"

#include "commands/CompositeCommand.h"
#include "selection/SelectableRef.h"
#include "selection/commands/SelectCommand.h"

#include <core/logging/LoggingProvider.h>
#include <zone/WorldPartitionIds.h>

#include <utility>

MoveEntitiesToZoneCommand::MoveEntitiesToZoneCommand(std::span<const EntityId> entities,
                                                     EditorDocument& source,
                                                     EditorDocument& target)
    : Source(source)
    , Target(target)
    , CurrentIds(entities.begin(), entities.end())
{
}

void MoveEntitiesToZoneCommand::Execute()
{
    // Capture once: snapshots are id-independent, so they stay valid across
    // undo/redo even though every restore mints fresh entity ids.
    if (!Captured)
    {
        Snapshots.reserve(CurrentIds.size());
        for (EntityId entity : CurrentIds)
            Snapshots.push_back(Source.CaptureEntity(entity));
        Captured = true;
    }

    for (EntityId entity : CurrentIds)
        Source.GetScene().DestroyEntity(entity);
    CurrentIds.clear();
    for (const EntitySnapshot& snapshot : Snapshots)
        CurrentIds.push_back(Target.RestoreEntity(snapshot, /*freshMesh*/ true));

    Source.MarkDirty();
    Target.MarkDirty();
}

void MoveEntitiesToZoneCommand::Undo()
{
    for (EntityId entity : CurrentIds)
        Target.GetScene().DestroyEntity(entity);
    CurrentIds.clear();
    // freshMesh false re-seats the original BrushId, free again because
    // Execute's destroy released it (or still live for an instanced source,
    // which the restore then rejoins).
    for (const EntitySnapshot& snapshot : Snapshots)
        CurrentIds.push_back(Source.RestoreEntity(snapshot, /*freshMesh*/ false));

    Source.MarkDirty();
    Target.MarkDirty();
}

std::unique_ptr<ICommand> MakeMoveEntitiesToZoneCommand(std::span<const EntityId> entities,
                                                        WorldDocument& world, ZoneId target,
                                                        SelectionService& selection)
{
    auto& log = world.Logging().GetLogger<MoveEntitiesToZoneCommand>();
    if (!world.IsWorld())
    {
        log.Warn("move to zone: not in world mode");
        return nullptr;
    }
    if (target == world.FocusZone())
    {
        log.Warn("move to zone: {} is the focus zone; the entities are already there",
                 ZoneIdToString(target));
        return nullptr;
    }
    EditorDocument* targetDocument = world.ZoneDocument(target);
    if (targetDocument == nullptr)
    {
        log.Warn("move to zone: zone {} is not open; load it as context first",
                 ZoneIdToString(target));
        return nullptr;
    }
    if (entities.empty())
    {
        log.Warn("move to zone: nothing selected to move");
        return nullptr;
    }

    std::vector<std::unique_ptr<ICommand>> commands;
    commands.reserve(2);
    // Clear selection first: the moved entities land in a non-focus document,
    // where they cannot stay selected. Undo restores the prior selection last.
    commands.push_back(std::make_unique<SelectCommand>(selection, SelectableRef{}));
    commands.push_back(std::make_unique<MoveEntitiesToZoneCommand>(
        entities, world.FocusDocument(), *targetDocument));
    return std::make_unique<CompositeCommand>(std::move(commands));
}
