#pragma once

#include "commands/ICommand.h"
#include "selection/ISelectionContext.h" // SelectionSnapshot
#include "document/EntitySnapshot.h"
#include "document/EditorDocument.h"

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <memory>
#include <span>
#include <vector>

class SelectionService;

// Inverse of DeleteEntityCommand: creates deep copies of a set of source
// entities at given transforms and leaves the copies selected (originals
// untouched). Captures each source's full state once on first Execute (reused
// across redo, like the delete command), so the snapshots stay valid even though
// each Execute mints fresh ids. Undo destroys the copies and restores the prior
// selection. By default RestoreEntity copies the brush sidecar mesh into a fresh
// BrushId, so the copies are independent of their sources. With asInstance the
// copies SHARE the source's BrushId: editing any instance edits them all, and
// baking them produces placements of one shared mesh asset. The Shift-drag
// object duplicate builds this via BrushManipulationSink::CommitDuplicate.
class DuplicateEntitiesCommand : public ICommand
{
public:
    DuplicateEntitiesCommand(std::span<const EntityId> sources,
                             std::span<const Transform3f> transforms,
                             EditorScene& scene, EditorDocument& document,
                             SelectionService& selection,
                             bool asInstance = false);

    void Execute() override;
    void Undo() override;

private:
    EditorScene&        Scene;
    EditorDocument&     Document;
    SelectionService&  Selection;
    std::vector<EntityId>       Sources;
    std::vector<Transform3f>    Transforms;
    std::vector<EntitySnapshot> Snapshots;
    std::vector<EntityId>       Created; // the copies, for Undo to destroy
    SelectionSnapshot           PreviousSelection;
    bool                        AsInstance = false;
    bool                        Captured = false;
};
