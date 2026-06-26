#pragma once

#include "IMeshEditTarget.h" // MeshEditTargetMesh, BrushMesh, EntityId, Transform3f

#include "../selection/SelectableRef.h"

#include <optional>
#include <span>
#include <vector>

// The single generic seam through which manipulators read, preview, and commit
// edits. Manipulators (editor/editmodes) depend only on this interface; the lone
// implementation (BrushManipulationSink, editor/level) is the only place in the
// manipulation path that touches EditorScene and the command stack. This is what
// keeps the manipulator/verb/target axes orthogonal. (08-select-tool-v2.md)
//
// Preview mutates live scene state without recording undo; the driving
// interaction restores the pre-drag state on cancel by previewing the captured
// original. Commit records one undoable command.
// One entity's transform change, for committing a multi-entity move as a single
// undoable step.
struct TransformEdit
{
    EntityId Entity = {};
    Transform3f Before = Transform3f::Identity();
    Transform3f After = Transform3f::Identity();
};

struct ManipulationSink
{
    [[nodiscard]] virtual std::optional<Transform3f> ResolveTransform(EntityId entity) const = 0;
    [[nodiscard]] virtual std::optional<MeshEditTargetMesh> ResolveMesh(EntityId entity) const = 0;

    virtual void PreviewTransform(EntityId entity, const Transform3f& transform) = 0;
    virtual void PreviewMesh(EntityId entity, const BrushMesh& mesh) = 0;

    // Commit all edits as one undoable step (move of one or many entities).
    virtual void CommitTransforms(const std::vector<TransformEdit>& edits) = 0;
    virtual void CommitMesh(EntityId entity, BrushMesh before, BrushMesh after) = 0;

    // Replace the selection with the given refs (empty clears it). The extrude
    // drag reindexes the mesh, so it selects the freshly created cap/edge on
    // commit; in element mode the selection holds only element refs, so a
    // replace-all is the right semantics.
    virtual void SelectElements(std::span<const SelectableRef> refs) = 0;

    // Live, non-undoable deep copies of `sources` (each with its own brush mesh),
    // for the duplicate drag's preview. Returns the new ids in source order.
    [[nodiscard]] virtual std::vector<EntityId> CreatePreviewDuplicates(
        std::span<const EntityId> sources) = 0;
    // Destroy live preview entities (drag cancel, or before the undoable commit).
    virtual void DestroyPreviewEntities(std::span<const EntityId> entities) = 0;

    // Commit, as one undoable step, deep copies of `sources` placed at the
    // matching `transforms`, leaving the copies selected (the originals are
    // untouched). The Shift-drag object duplicate uses this.
    virtual void CommitDuplicate(std::span<const EntityId> sources,
                                 std::span<const Transform3f> transforms) = 0;

    virtual ~ManipulationSink() = default;
};
