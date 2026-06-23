#pragma once

#include "../meshedit/IMeshEditTarget.h"
#include "../meshedit/ManipulationSink.h"

class CommandStack;
class EditorDocument;
class EditorScene;
class SelectionService;

// The one brush-backed edit backend: previews by writing the live scene and
// commits via the value-command factories (MakeMoveCommand / MakeEditBrushMeshCommand).
// Implements both the
// manipulator-facing ManipulationSink (preview/commit during drags) and the
// verb-facing IMeshEditTarget (resolve + make-command for MeshEditService). The
// only class in the edit path that knows EditorScene + the command stack, so
// manipulators, the session, and the verb service all stay generic.
// (docs/architecture/hardening-and-consolidation.md W5 — merged from the former
// separate BrushEditTarget.)
class BrushManipulationSink : public ManipulationSink, public IMeshEditTarget
{
public:
    BrushManipulationSink(EditorScene& scene, EditorDocument& document, CommandStack& commands,
                          SelectionService& selection);

    // ManipulationSink
    [[nodiscard]] std::optional<Transform3f> ResolveTransform(EntityId entity) const override;
    [[nodiscard]] std::optional<MeshEditTargetMesh> ResolveMesh(EntityId entity) const override;
    void PreviewTransform(EntityId entity, const Transform3f& transform) override;
    void PreviewMesh(EntityId entity, const BrushMesh& mesh) override;
    void CommitTransforms(const std::vector<TransformEdit>& edits) override;
    void CommitMesh(EntityId entity, BrushMesh before, BrushMesh after) override;
    void SelectElements(std::span<const SelectableRef> refs) override;
    [[nodiscard]] std::vector<EntityId> CreatePreviewDuplicates(
        std::span<const EntityId> sources) override;
    void DestroyPreviewEntities(std::span<const EntityId> entities) override;
    void CommitDuplicate(std::span<const EntityId> sources,
                         std::span<const Transform3f> transforms) override;

    // IMeshEditTarget (verb path)
    [[nodiscard]] std::optional<MeshEditTargetMesh> Resolve(EntityId entity) const override;
    [[nodiscard]] std::unique_ptr<ICommand> MakeEditCommand(EntityId entity,
                                                            BrushMesh before,
                                                            BrushMesh after) override;

private:
    EditorScene& Scene;
    EditorDocument& Document;
    CommandStack& Commands;
    SelectionService& Selection;
};
