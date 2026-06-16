#include "BrushEditTarget.h"

#include "LevelDocument.h"
#include "LevelScene.h"
#include "commands/EditBrushMeshCommand.h"

#include <utility>

BrushEditTarget::BrushEditTarget(LevelScene& scene, LevelDocument& document)
    : Scene(scene)
    , Document(document)
{
}

std::optional<MeshEditTargetMesh> BrushEditTarget::Resolve(EntityId entity) const
{
    const BrushMesh* mesh = Scene.TryGetBrushMesh(entity);
    const Transform3f* transform = Scene.TryGetTransform(entity);
    if (mesh == nullptr || transform == nullptr)
        return std::nullopt;

    return MeshEditTargetMesh{
        .Mesh = mesh,
        .Transform = *transform,
    };
}

std::unique_ptr<ICommand> BrushEditTarget::MakeEditCommand(EntityId entity,
                                                           BrushMesh before,
                                                           BrushMesh after)
{
    return std::make_unique<EditBrushMeshCommand>(
        entity, std::move(before), std::move(after), Scene, Document);
}
