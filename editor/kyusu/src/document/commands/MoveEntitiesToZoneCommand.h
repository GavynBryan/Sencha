#pragma once

#include "document/EntitySnapshot.h"

#include "commands/ICommand.h"

#include <ecs/EntityId.h>
#include <zone/ZoneId.h>

#include <memory>
#include <span>
#include <vector>

class EditorDocument;
class SelectionService;
class WorldDocument;

// Moves entities between two zone documents by capture/destroy/restore: the
// snapshot path already carries every registered component, the brush mesh
// sidecar entry, and the view flags, so a move is a delete in one document and
// a restore in the other. Restore into the target mints fresh BrushIds (ids
// are per-document); undo re-seats the originals in the source. Moving one
// placement of an instanced brush un-instances the moved copy: mesh sharing is
// per-document state and does not survive the crossing.
class MoveEntitiesToZoneCommand : public ICommand
{
public:
    MoveEntitiesToZoneCommand(std::span<const EntityId> entities,
                              EditorDocument& source, EditorDocument& target);

    void Execute() override;   // capture once; destroy in source; restore in target (fresh mesh ids)
    void Undo() override;      // destroy in target; restore in source (original mesh ids re-seated)

private:
    EditorDocument& Source;
    EditorDocument& Target;
    std::vector<EntitySnapshot> Snapshots;
    // Live ids in whichever document currently holds the entities (target after
    // Execute, source after Undo). Fresh generational handles each time; nothing
    // outside the command may cache them.
    std::vector<EntityId> CurrentIds;
    bool Captured = false;
};

// The user-facing assembly: SelectCommand(clear) first, then the move, as one
// CompositeCommand (the MakeDeleteEntitiesCommand pattern). Returns nullptr and
// logs when the move is refused:
//   - the document is not in world mode,
//   - target names no open zone (loading it as context is an explicit prior step),
//   - target is the focus zone's own id: the entities are already there,
//   - the entity list is empty.
[[nodiscard]] std::unique_ptr<ICommand> MakeMoveEntitiesToZoneCommand(
    std::span<const EntityId> entities, WorldDocument& world, ZoneId target,
    SelectionService& selection);
