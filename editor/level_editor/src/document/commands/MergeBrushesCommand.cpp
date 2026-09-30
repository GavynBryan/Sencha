#include "MergeBrushesCommand.h"
#include "brush/BrushWorkCounters.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "brush/BrushEvaluation.h"
#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "selection/SelectionService.h"

#include <cstdint>
#include <utility>

namespace
{
// Appends every evaluated piece of `entity` onto `target` in the target's frame.
void AppendEvaluated(BrushMesh& target, const Transform3f& targetTransform,
                     const EditorScene& scene, EntityId entity)
{
    ++BrushWorkCounters::Frame().MergeFlattens;
    const BrushEvaluated* evaluated = scene.TryGetBrushPieces(entity, BrushEvaluationPolicy::Cook());
    const Transform3f* world = scene.TryGetWorldTransform(entity);
    if (evaluated == nullptr || world == nullptr)
        return;
    for (const BrushPiece& piece : evaluated->Pieces)
        BrushOps::AppendRebased(target, targetTransform, *piece.Mesh,
                                PieceWorldTransform(*world, piece));
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
        TargetBefore = *Scene.GetBrushMeshStore().FindRecord(Scene.TryGetBrush(Target)->Id);
        const Transform3f targetTransform = *Scene.TryGetWorldTransform(Target);

        Merged = BrushMesh{};
        AppendEvaluated(Merged, targetTransform, Scene, Target);
        SourceSnapshots.reserve(Sources.size());
        for (EntityId source : Sources)
        {
            SourceSnapshots.push_back(Document.CaptureEntity(source));
            AppendEvaluated(Merged, targetTransform, Scene, source);
        }
        BrushValidateAndRepair(Merged);
        Captured = true;
    }

    Scene.SetBrushRecord(Target, BrushRecord{ Merged, {} });
    for (EntityId source : Sources)
        Scene.DestroyEntity(source);

    Selection.SetSelection({ SelectableRef::EntitySelection(Scene.GetRegistry().Id, Target) });
    Document.MarkDirty();
}

void MergeBrushesCommand::Undo()
{
    Scene.SetBrushRecord(Target, TargetBefore);
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
    if (scene.TryGetBrushMesh(target) == nullptr || scene.TryGetWorldTransform(target) == nullptr)
        return nullptr;

    std::vector<EntityId> valid;
    for (EntityId source : sources)
        if (source != target && scene.TryGetBrushMesh(source) != nullptr
            && scene.TryGetWorldTransform(source) != nullptr)
            valid.push_back(source);
    if (valid.empty())
        return nullptr;

    return std::make_unique<MergeBrushesCommand>(target, valid, scene, document, selection);
}
