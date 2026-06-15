#include "DeleteEntityCommand.h"

DeleteEntityCommand::DeleteEntityCommand(EntityId entity,
                                         LevelScene& scene,
                                         LevelDocument& document)
    : TargetEntity(entity)
    , Scene(scene)
    , Document(document)
{
}

void DeleteEntityCommand::Execute()
{
    if (!CapturedState)
    {
        if (const Transform3f* transform = Scene.TryGetTransform(TargetEntity))
            SavedTransform = *transform;
        if (const BrushComponent* brush = Scene.TryGetBrush(TargetEntity))
            SavedBrush = *brush;
        if (const CameraComponent* camera = Scene.TryGetCamera(TargetEntity))
            SavedCamera = *camera;
        CapturedState = true;
    }

    Scene.DestroyEntity(TargetEntity);
    Document.MarkDirty();
}

void DeleteEntityCommand::Undo()
{
    if (!SavedTransform.has_value())
        return;

    if (SavedBrush.has_value())
        RestoredEntity = Scene.CreateBrush(SavedTransform->Position, SavedBrush->HalfExtents);
    else if (SavedCamera.has_value())
        RestoredEntity = Scene.CreateCamera(SavedTransform->Position);
    else
        return;

    Transform3f restored = *SavedTransform;
    Scene.SetTransform(RestoredEntity, restored);
    Document.MarkDirty();
}
