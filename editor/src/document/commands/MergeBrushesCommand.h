#pragma once

#include "../../commands/ICommand.h"
#include "../EntitySnapshot.h"

#include "../../selection/ISelectionContext.h"

#include <ecs/EntityId.h>

#include <memory>
#include <span>
#include <vector>

class EditorDocument;
class EditorScene;
class SelectionService;

// Joins the selected brushes into ONE brush entity (the target): every source's
// faces are rebased into the target's local frame and appended (materials and
// texture placement preserved through the world UV bridge), then the sources
// are destroyed. No volume boolean: overlapping or interior faces stay and can
// be deleted in Face mode. Undo restores the target mesh and the source
// entities. Merging INTO an instanced target edits the shared mesh, so every
// instance grows the merged geometry (instancing semantics, not a bug).
class MergeBrushesCommand : public ICommand
{
public:
    MergeBrushesCommand(EntityId target, std::span<const EntityId> sources,
                        EditorScene& scene, EditorDocument& document, SelectionService& selection);

    void Execute() override;
    void Undo() override;

private:
    EditorScene& Scene;
    EditorDocument& Document;
    SelectionService& Selection;
    EntityId Target;
    std::vector<EntityId> Sources;
    std::vector<EntitySnapshot> SourceSnapshots;
    BrushMesh TargetBefore;
    BrushMesh Merged;
    bool Captured = false;
};

// nullptr unless the target and at least one source resolve to brush meshes.
[[nodiscard]] std::unique_ptr<ICommand> MakeMergeBrushesCommand(
    EntityId target, std::span<const EntityId> sources,
    EditorScene& scene, EditorDocument& document, SelectionService& selection);
