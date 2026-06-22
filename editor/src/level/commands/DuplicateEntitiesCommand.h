#pragma once

#include "../../commands/ICommand.h"
#include "../../selection/ISelectionContext.h" // SelectionSnapshot
#include "../EntitySnapshot.h"
#include "../LevelDocument.h"

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
// selection. RestoreEntity copies the brush sidecar mesh into a fresh BrushId, so
// the copies are independent of their sources. The Shift-drag object duplicate
// builds this via BrushManipulationSink::CommitDuplicate.
class DuplicateEntitiesCommand : public ICommand
{
public:
    DuplicateEntitiesCommand(std::span<const EntityId> sources,
                             std::span<const Transform3f> transforms,
                             LevelScene& scene, LevelDocument& document,
                             SelectionService& selection);

    void Execute() override;
    void Undo() override;

private:
    LevelScene&        Scene;
    LevelDocument&     Document;
    SelectionService&  Selection;
    std::vector<EntityId>       Sources;
    std::vector<Transform3f>    Transforms;
    std::vector<EntitySnapshot> Snapshots;
    std::vector<EntityId>       Created; // the copies, for Undo to destroy
    SelectionSnapshot           PreviousSelection;
    bool                        Captured = false;
};
