#include "BrushManipulationSink.h"

#include "../document/EditorDocument.h"
#include "../document/EditorScene.h"
#include "../document/commands/DuplicateEntitiesCommand.h"
#include "../document/commands/ValueCommand.h"
#include "../commands/CommandStack.h"
#include "../commands/CompositeCommand.h"
#include "../selection/SelectionService.h"

#include <memory>
#include <utility>
#include <vector>

BrushManipulationSink::BrushManipulationSink(EditorScene& scene, EditorDocument& document,
                                             CommandStack& commands, SelectionService& selection)
    : Scene(scene)
    , Document(document)
    , Commands(commands)
    , Selection(selection)
{
}

std::optional<Transform3f> BrushManipulationSink::ResolveTransform(EntityId entity) const
{
    if (const Transform3f* transform = Scene.TryGetTransform(entity))
        return *transform;
    return std::nullopt;
}

std::optional<MeshEditTargetMesh> BrushManipulationSink::ResolveMesh(EntityId entity) const
{
    const BrushMesh* mesh = Scene.TryGetBrushMesh(entity);
    const Transform3f* transform = Scene.TryGetTransform(entity);
    if (mesh == nullptr || transform == nullptr)
        return std::nullopt;
    return MeshEditTargetMesh{ .Mesh = mesh, .Transform = *transform };
}

void BrushManipulationSink::PreviewTransform(EntityId entity, const Transform3f& transform)
{
    Scene.SetTransform(entity, transform);
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
        commands.push_back(MakeMoveCommand(edit.Entity, edit.Before, edit.After, Scene, Document));

    Commands.Execute(std::make_unique<CompositeCommand>(std::move(commands)));
}

void BrushManipulationSink::CommitMesh(EntityId entity, BrushMesh before, BrushMesh after)
{
    Commands.Execute(MakeEditCommand(entity, std::move(before), std::move(after)));
}

void BrushManipulationSink::SelectElements(std::span<const SelectableRef> refs)
{
    Selection.SetSelection(std::vector<SelectableRef>(refs.begin(), refs.end()));
}

std::vector<EntityId> BrushManipulationSink::CreatePreviewDuplicates(std::span<const EntityId> sources)
{
    std::vector<EntityId> copies;
    copies.reserve(sources.size());
    for (EntityId source : sources)
        copies.push_back(Document.DuplicateEntity(source));
    return copies;
}

void BrushManipulationSink::DestroyPreviewEntities(std::span<const EntityId> entities)
{
    for (EntityId entity : entities)
        Scene.DestroyEntity(entity);
}

void BrushManipulationSink::CommitDuplicate(std::span<const EntityId> sources,
                                            std::span<const Transform3f> transforms)
{
    if (sources.empty())
        return;
    Commands.Execute(std::make_unique<DuplicateEntitiesCommand>(
        sources, transforms, Scene, Document, Selection));
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
