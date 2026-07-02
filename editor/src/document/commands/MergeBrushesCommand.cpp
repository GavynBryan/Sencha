#include "MergeBrushesCommand.h"

#include "../EditorDocument.h"
#include "../EditorScene.h"
#include "../../brush/BrushTransform.h"
#include "../../brush/BrushValidation.h"
#include "../../brush/FaceMaterial.h"
#include "../../selection/SelectionService.h"

#include <cstdint>
#include <utility>

namespace
{
// Appends `source` (expressed in sourceTransform's frame) onto `target`
// (expressed in targetTransform's frame): vertices rebase source-local ->
// world -> target-local, loops re-index, and each face's UV projection converts
// through world space so the texture renders exactly where it did before the
// merge (world-aligned projections are brush-local axes; a straight copy would
// shift them whenever the frames differ).
void AppendRebased(BrushMesh& target, const Transform3f& targetTransform,
                   const BrushMesh& source, const Transform3f& sourceTransform)
{
    const std::uint32_t base = static_cast<std::uint32_t>(target.Vertices.size());
    target.Vertices.reserve(target.Vertices.size() + source.Vertices.size());
    for (const BrushVertex& vertex : source.Vertices)
    {
        const Vec3d world = sourceTransform.TransformPoint(vertex.Position);
        target.Vertices.push_back(BrushVertex{ InverseTransformPoint(targetTransform, world) });
    }

    target.Faces.reserve(target.Faces.size() + source.Faces.size());
    for (const BrushFace& face : source.Faces)
    {
        BrushFace rebased = face;
        for (std::uint32_t& index : rebased.Loop)
            index += base;
        rebased.Material.Uv = UvProjectionToLocal(
            UvProjectionToWorld(face.Material.Uv, sourceTransform), targetTransform);
        target.Faces.push_back(std::move(rebased));
    }
}
}

MergeBrushesCommand::MergeBrushesCommand(EntityId target, std::span<const EntityId> sources,
                                         EditorScene& scene, EditorDocument& document,
                                         SelectionService& selection)
    : Scene(scene)
    , Document(document)
    , Selection(selection)
    , Target(target)
    , Sources(sources.begin(), sources.end())
{
}

void MergeBrushesCommand::Execute()
{
    // Capture once (redo reuses): the target's before-mesh, the merged result,
    // and a full snapshot of each source for undo restoration.
    if (!Captured)
    {
        TargetBefore = *Scene.TryGetBrushMesh(Target);
        const Transform3f targetTransform = *Scene.TryGetTransform(Target);

        Merged = TargetBefore;
        SourceSnapshots.reserve(Sources.size());
        for (EntityId source : Sources)
        {
            SourceSnapshots.push_back(Document.CaptureEntity(source));
            AppendRebased(Merged, targetTransform,
                          *Scene.TryGetBrushMesh(source), *Scene.TryGetTransform(source));
        }
        BrushValidateAndRepair(Merged);
        Captured = true;
    }

    Scene.SetBrushMesh(Target, Merged);
    for (EntityId source : Sources)
        Scene.DestroyEntity(source);

    Selection.SetSelection({ SelectableRef::EntitySelection(Scene.GetRegistry().Id, Target) });
    Document.MarkDirty();
}

void MergeBrushesCommand::Undo()
{
    Scene.SetBrushMesh(Target, TargetBefore);
    // Restoration mints fresh entity ids, so the captured selection would point
    // at dead handles: select the restored set (target + sources) instead.
    Sources.clear();
    std::vector<SelectableRef> restored;
    restored.push_back(SelectableRef::EntitySelection(Scene.GetRegistry().Id, Target));
    for (const EntitySnapshot& snapshot : SourceSnapshots)
    {
        const EntityId source = Document.RestoreEntity(snapshot);
        Sources.push_back(source);
        restored.push_back(SelectableRef::EntitySelection(Scene.GetRegistry().Id, source));
    }
    Selection.SetSelection(std::move(restored));
    Document.MarkDirty();
}

std::unique_ptr<ICommand> MakeMergeBrushesCommand(EntityId target,
                                                  std::span<const EntityId> sources,
                                                  EditorScene& scene, EditorDocument& document,
                                                  SelectionService& selection)
{
    if (scene.TryGetBrushMesh(target) == nullptr || scene.TryGetTransform(target) == nullptr)
        return nullptr;

    std::vector<EntityId> valid;
    for (EntityId source : sources)
        if (source != target && scene.TryGetBrushMesh(source) != nullptr
            && scene.TryGetTransform(source) != nullptr)
            valid.push_back(source);
    if (valid.empty())
        return nullptr;

    return std::make_unique<MergeBrushesCommand>(target, valid, scene, document, selection);
}
