#pragma once

#include "commands/ICommand.h"
#include "brush/BrushRecord.h"
#include "document/EntitySnapshot.h"

#include "selection/ISelectionContext.h"

#include <ecs/EntityId.h>

#include <memory>
#include <span>
#include <vector>

class EditorDocument;
class EditorScene;
class SelectionService;

// Joins the selected brushes into ONE brush entity (the target): what you see
// is what you merge. Every participant, the target included, contributes its
// EVALUATED pieces (modifiers applied), rebased into the target's local frame
// and appended (materials and texture placement preserved through the world UV
// bridge); the result carries an empty modifier stack, since keeping the
// target's would re-apply it to geometry that already went through it. The
// sources are destroyed. No volume boolean: overlapping or interior faces stay
// and can be deleted in Face mode. Undo restores the target record and the
// source entities. Merging INTO an instanced target edits the shared record,
// so every instance grows the merged geometry (instancing semantics, not a
// bug).
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
    BrushRecord TargetBefore;
    BrushMesh Merged;
    bool Captured = false;
};

// nullptr unless the target and at least one source resolve to brush meshes.
[[nodiscard]] std::unique_ptr<ICommand> MakeMergeBrushesCommand(
    EntityId target, std::span<const EntityId> sources,
    EditorScene& scene, EditorDocument& document, SelectionService& selection);
