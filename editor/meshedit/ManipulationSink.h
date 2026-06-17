#pragma once

#include "IMeshEditTarget.h" // MeshEditTargetMesh, BrushMesh, EntityId, Transform3f

#include <optional>
#include <vector>

// The single generic seam through which manipulators read, preview, and commit
// edits. Manipulators (editor/editmodes) depend only on this interface; the lone
// implementation (BrushManipulationSink, editor/level) is the only place in the
// manipulation path that touches LevelScene and the command stack. This is what
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

    virtual ~ManipulationSink() = default;
};
