#include "CreateBrushCommand.h"

CreateBrushCommand::CreateBrushCommand(Vec3d position,
                                       Vec3d halfExtents,
                                       LevelScene& scene,
                                       LevelDocument& document)
    : Position(position)
    , HalfExtents(halfExtents)
    , Scene(scene)
    , Document(document)
{
}

void CreateBrushCommand::Execute()
{
    CreatedEntity = Scene.CreateBrush(Position, HalfExtents);
    Document.MarkDirty();
}

void CreateBrushCommand::Undo()
{
    Scene.DestroyEntity(CreatedEntity);
    Document.MarkDirty();
}
