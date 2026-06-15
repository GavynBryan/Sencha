#include "CreateCameraCommand.h"

CreateCameraCommand::CreateCameraCommand(Vec3d position,
                                         LevelScene& scene,
                                         LevelDocument& document)
    : Position(position)
    , Scene(scene)
    , Document(document)
{
}

void CreateCameraCommand::Execute()
{
    CreatedEntity = Scene.CreateCamera(Position);
    Document.MarkDirty();
}

void CreateCameraCommand::Undo()
{
    Scene.DestroyEntity(CreatedEntity);
    Document.MarkDirty();
}
