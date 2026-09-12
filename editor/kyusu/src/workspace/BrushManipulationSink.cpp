#include "BrushManipulationSink.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/DuplicateEntitiesCommand.h"
#include "document/commands/ValueCommand.h"
#include "commands/CommandStack.h"
#include "commands/CompositeCommand.h"
#include "selection/SelectionService.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

BrushManipulationSink::BrushManipulationSink(EditorScene& scene, EditorDocument& document,
                                             CommandStack& commands, SelectionService& selection,
                                             std::function<void(std::vector<EntitySnapshot>&)> duplicateRemap)
    : Scene(scene)
    , Document(document)
    , Commands(commands)
    , Selection(selection)
    , DuplicateRemap(std::move(duplicateRemap))
{
}

EntityId BrushManipulationSink::GetParent(EntityId entity) const
{
    return Scene.GetParent(entity);
}

std::optional<Transform3f> BrushManipulationSink::ResolveTransform(EntityId entity) const
{
    if (const Transform3f* transform = Scene.TryGetWorldTransform(entity))
        return *transform;
    return std::nullopt;
}

std::optional<MeshEditTargetMesh> BrushManipulationSink::ResolveMesh(EntityId entity) const
{
    const BrushMesh* mesh = Scene.TryGetBrushMesh(entity);
    const Transform3f* transform = Scene.TryGetWorldTransform(entity);
    if (mesh == nullptr || transform == nullptr)
        return std::nullopt;
    return MeshEditTargetMesh{ .Mesh = mesh, .Transform = *transform };
}

void BrushManipulationSink::PreviewTransform(EntityId entity, const Transform3f& transform)
{
    Scene.SetWorldTransform(entity, transform);
}

void BrushManipulationSink::PreviewMesh(EntityId entity, const BrushMesh& mesh)
{
    Scene.SetBrushMesh(entity, mesh);
}

void BrushManipulationSink::CommitTransforms(const std::vector<TransformEdit>& edits)
{
    if (edits.empty())
        return;

    std::vector<std::unique_ptr<ICommand>> commands;
    commands.reserve(edits.size());
    for (const TransformEdit& edit : edits)
        commands.push_back(MakeWorldMoveCommand(edit.Entity, edit.Before, edit.After, Scene, Document));

    Commands.Execute(std::make_unique<CompositeCommand>(std::move(commands)));
}

void BrushManipulationSink::CommitMeshes(std::vector<MeshEdit> edits)
{
    if (edits.empty())
        return;

    std::vector<std::unique_ptr<ICommand>> commands;
    commands.reserve(edits.size());
    for (MeshEdit& edit : edits)
        commands.push_back(MakeEditCommand(edit.Entity, std::move(edit.Before), std::move(edit.After)));

    Commands.Execute(std::make_unique<CompositeCommand>(std::move(commands)));
}

void BrushManipulationSink::CommitMesh(EntityId entity, BrushMesh before, BrushMesh after)
{
    std::vector<MeshEdit> edits;
    edits.push_back({ entity, std::move(before), std::move(after) });
    CommitMeshes(std::move(edits));
}

void BrushManipulationSink::CommitSplits(std::vector<SplitEdit> edits)
{
    if (edits.empty())
        return;

    // Per split: a copy of the entity alone, at its own world placement, whose
    // captured mesh is swapped for the other half before it is restored; then
    // the entity re-meshed to the kept half. Undo runs them backwards, so the
    // mesh comes back first and the copy is destroyed last.
    std::vector<std::unique_ptr<ICommand>> commands;
    commands.reserve(edits.size() * 2);
    std::vector<EntityId> originals;
    originals.reserve(edits.size());
    for (SplitEdit& edit : edits)
    {
        const Transform3f* world = Scene.TryGetWorldTransform(edit.Entity);
        if (world == nullptr)
            continue;
        originals.push_back(edit.Entity);
        const EntityId source = edit.Entity;
        const Transform3f placement = *world;
        for (BrushMesh& other : edit.Others)
        {
            std::function<void(std::vector<EntitySnapshot>&)> chained = DuplicateRemap;
            auto remap = [other = std::move(other), chained = std::move(chained)](std::vector<EntitySnapshot>& snapshots) {
                for (EntitySnapshot& snapshot : snapshots)
                    if (snapshot.Mesh.has_value())
                        snapshot.Mesh = other;
                if (chained)
                    chained(snapshots);
            };
            commands.push_back(std::make_unique<DuplicateEntitiesCommand>(
                std::span<const EntityId>(&source, 1), std::span<const Transform3f>(&placement, 1), Scene,
                Document, Selection, DuplicateBranchPolicy::EntityOnly, false, std::move(remap)));
        }
        commands.push_back(MakeEditCommand(edit.Entity, std::move(edit.Before), std::move(edit.Keep)));
    }
    if (commands.empty())
        return;
    // Every brush that exists after the commit and did not before is a copy
    // this split made; the selection is those and the originals together.
    std::vector<EntityId> before(Scene.GetAllEntities().begin(), Scene.GetAllEntities().end());
    Commands.Execute(std::make_unique<CompositeCommand>(std::move(commands)));
    std::vector<SelectableRef> all;
    const RegistryId registry = Scene.GetRegistry().Id;
    for (EntityId entity : originals)
        all.push_back(SelectableRef::EntitySelection(registry, entity));
    for (EntityId entity : Scene.GetAllEntities())
        if (std::find(before.begin(), before.end(), entity) == before.end())
            all.push_back(SelectableRef::EntitySelection(registry, entity));
    Selection.SetSelection(std::move(all));
}

void BrushManipulationSink::SelectElements(std::span<const SelectableRef> refs)
{
    Selection.SetSelection(std::vector<SelectableRef>(refs.begin(), refs.end()));
}

std::vector<EntityId> BrushManipulationSink::CreatePreviewDuplicates(std::span<const EntityId> sources)
{
    // A source means its branch. Copy each subtree parent-before-child, rebind
    // the interior parent links onto the copies, and hand back only the root
    // copies: the drag moves those, and the children follow through
    // propagation exactly as they do on the originals.
    std::vector<EntityId> copies;
    copies.reserve(sources.size());
    for (EntityId source : sources)
    {
        std::vector<EntityId> subtree;
        Scene.CollectSubtree(source, subtree);

        std::vector<EntityId> subtreeCopies;
        subtreeCopies.reserve(subtree.size());
        for (EntityId member : subtree)
        {
            const EntityId copy = Document.DuplicateEntity(member);
            const EntityId parent = Scene.GetParent(member);
            const auto inBranch = std::find(subtree.begin(), subtree.end(), parent);
            if (inBranch != subtree.end())
                (void)Scene.SetParent(copy,
                    subtreeCopies[static_cast<std::size_t>(inBranch - subtree.begin())]);
            subtreeCopies.push_back(copy);
        }
        copies.push_back(subtreeCopies.front());
    }
    return copies;
}

void BrushManipulationSink::DestroyPreviewEntities(std::span<const EntityId> entities)
{
    // Preview roots carry their copied branch; destroying just the root would
    // hand the copied children to the scene as orphans.
    for (EntityId entity : entities)
        Scene.DestroySubtree(entity);
}

void BrushManipulationSink::CommitDuplicate(std::span<const EntityId> sources,
                                            std::span<const Transform3f> transforms)
{
    if (sources.empty())
        return;
    // Offset before executing: the command leaves the sources untouched, but
    // reading them first keeps the observer independent of command internals.
    std::optional<Vec3d> offset;
    if (!transforms.empty())
        if (const Transform3f* source = Scene.TryGetWorldTransform(sources.front()))
            offset = transforms.front().Position - source->Position;
    Commands.Execute(std::make_unique<DuplicateEntitiesCommand>(
        sources, transforms, Scene, Document, Selection, DuplicateBranchPolicy::Subtree, false,
        DuplicateRemap));
    if (DuplicateObserver && offset.has_value())
        DuplicateObserver(*offset);
}

std::optional<MeshEditTargetMesh> BrushManipulationSink::Resolve(EntityId entity) const
{
    return ResolveMesh(entity);
}

std::unique_ptr<ICommand> BrushManipulationSink::MakeEditCommand(EntityId entity,
                                                                BrushMesh before,
                                                                BrushMesh after)
{
    return MakeEditBrushMeshCommand(entity, std::move(before), std::move(after), Scene, Document);
}
