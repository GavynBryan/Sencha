#include "BrushManipulationSink.h"

#include "LevelDocument.h"
#include "LevelScene.h"
#include "commands/EditBrushMeshCommand.h"
#include "commands/MoveEntityCommand.h"
#include "../commands/CommandStack.h"
#include "../commands/CompositeCommand.h"

#include <memory>
#include <utility>
#include <vector>

BrushManipulationSink::BrushManipulationSink(LevelScene& scene, LevelDocument& document, CommandStack& commands)
    : Scene(scene)
    , Document(document)
    , Commands(commands)
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
        commands.push_back(std::make_unique<MoveEntityCommand>(
            edit.Entity, edit.Before, edit.After, Scene, Document));

    Commands.Execute(std::make_unique<CompositeCommand>(std::move(commands)));
}

void BrushManipulationSink::CommitMesh(EntityId entity, BrushMesh before, BrushMesh after)
{
    Commands.Execute(MakeEditCommand(entity, std::move(before), std::move(after)));
}

std::optional<MeshEditTargetMesh> BrushManipulationSink::Resolve(EntityId entity) const
{
    return ResolveMesh(entity);
}

std::unique_ptr<ICommand> BrushManipulationSink::MakeEditCommand(EntityId entity,
                                                                BrushMesh before,
                                                                BrushMesh after)
{
    return std::make_unique<EditBrushMeshCommand>(
        entity, std::move(before), std::move(after), Scene, Document);
}
