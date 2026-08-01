#pragma once

#include "PendingBridgeEdit.h"

#include "brush/BrushMesh.h"
#include "document/EntitySnapshot.h"

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class CommandStack;
class LoggingProvider;
class MeshEditService;
class SelectionService;
class WorldDocument;
class IMeshEditTarget;
struct RuntimeAssets;

// The verbs that act on the current selection as a whole: copy it, join it,
// split it, bake it. They share one shape (read the selection, build one
// undoable step, run it), which is why they sit together rather than spreading
// across the workspace and the composition root.
//
// This is not a general command service. Editing a specific thing belongs to the
// mechanism that owns it; what belongs here is only "the selection, as a group".
class SelectionActions
{
public:
    SelectionActions(WorldDocument& world, SelectionService& selection, CommandStack& commands,
                     MeshEditService& meshEdit, PendingBridgeEdit& bridgeEdit,
                     std::function<IMeshEditTarget*()> editTarget,
                     std::function<void(std::vector<EntitySnapshot>&)> duplicateRemap);

    // Baking writes a mesh asset into the project, so those verbs stay inert
    // until the composition root has an asset environment to hand over.
    void SetAssetEnvironment(RuntimeAssets& assets, std::string contentRoot,
                             LoggingProvider& logging);

    // Independent copies in place (Ctrl+D), or copies SHARING the source mesh
    // (Alt+D): editing any instance edits them all, and baking them shares one
    // asset. Both select the copies, as one undoable step.
    void Duplicate(bool asInstance);

    // Copies displaced by `offset`, as one undoable step. The copies end up
    // selected, so repeated calls chain placements.
    void DuplicateWithOffset(Vec3d offset);

    // Records a drag-committed duplicate as the repeatable action, and replays
    // it (Ctrl+R). No-op until something records itself.
    void RecordRepeatableDuplicate(Vec3d offset);
    void RepeatLast();

    // Breaks every selected instanced brush out of its group, each taking its own
    // copy of the live shared mesh, as one undoable step.
    void MakeUnique();

    // Joins the selected brushes into the primary one (faces rebased, texture
    // placement preserved, sources destroyed), as one undoable step.
    void Merge();

    // Splits the selected faces (one brush) into a new brush entity, as one
    // undoable step; the source keeps at least one face.
    void SeparateFaces();

    // Bridges two selected edge paths. Across two brushes this stages a preview
    // to tune before it commits; within one brush it is an immediate mesh edit.
    void Bridge(int segments);

    // Replaces each selected brush with a placed static mesh, writing the asset
    // into the project's first content root. Reversible: RevertBaked restores the
    // source brush the level file kept.
    void Bake();
    void RevertBaked();

    [[nodiscard]] bool HasBakedSelection() const;
    [[nodiscard]] bool HasInstancedSelection() const;

    // The first selected entity's brush geometry (live or dormant), for the
    // shell's glTF export. Null when nothing selected resolves to a mesh; the
    // caller owns presenting the file dialog.
    [[nodiscard]] const BrushMesh* SelectedExportMesh() const;

private:
    // The selected entities that resolve to a brush, snapshotted before any
    // command runs: executing one must not invalidate the span being walked.
    [[nodiscard]] std::vector<EntityId> SelectedBrushEntities() const;
    void ExecuteAsOneStep(std::vector<std::unique_ptr<class ICommand>> commands);

    WorldDocument& World;
    SelectionService& Selection;
    CommandStack& Commands;
    MeshEditService& MeshEdit;
    PendingBridgeEdit& BridgeEdit;
    std::function<IMeshEditTarget*()> EditTargetResolver;
    std::function<void(std::vector<EntitySnapshot>&)> DuplicateRemap;

    RuntimeAssets* Assets = nullptr;
    std::string ContentRoot;
    LoggingProvider* Logging = nullptr;

    // The last repeatable action, recorded by the action itself. Today that is a
    // duplicate-with-offset from the gizmo's Shift-drag.
    std::function<void()> LastRepeatable;
};
