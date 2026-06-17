#pragma once

#include "../meshedit/ManipulationSink.h"

class CommandStack;
class LevelDocument;
class LevelScene;

// The brush-backed ManipulationSink: previews by writing the live scene and
// commits via MoveEntityCommand / EditBrushMeshCommand. The only class in the
// manipulation path that knows LevelScene + the command stack, so manipulators
// and the session stay generic. (08-select-tool-v2.md)
class BrushManipulationSink : public ManipulationSink
{
public:
    BrushManipulationSink(LevelScene& scene, LevelDocument& document, CommandStack& commands);

    [[nodiscard]] std::optional<Transform3f> ResolveTransform(EntityId entity) const override;
    [[nodiscard]] std::optional<MeshEditTargetMesh> ResolveMesh(EntityId entity) const override;

    void PreviewTransform(EntityId entity, const Transform3f& transform) override;
    void PreviewMesh(EntityId entity, const BrushMesh& mesh) override;

    void CommitTransforms(const std::vector<TransformEdit>& edits) override;
    void CommitMesh(EntityId entity, BrushMesh before, BrushMesh after) override;

private:
    LevelScene& Scene;
    LevelDocument& Document;
    CommandStack& Commands;
};
