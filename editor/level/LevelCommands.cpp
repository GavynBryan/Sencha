#include "LevelCommands.h"

CreateCubeCommand::CreateCubeCommand(Vec3d position,
                                     Vec3d halfExtents,
                                     LevelScene& scene,
                                     LevelDocument& document)
    : Position(position)
    , HalfExtents(halfExtents)
    , Scene(scene)
    , Document(document)
{
}

void CreateCubeCommand::Execute()
{
    CreatedEntity = Scene.CreateCube(Position, HalfExtents);
    Document.MarkDirty();
}

void CreateCubeCommand::Undo()
{
    Scene.DestroyEntity(CreatedEntity);
    Document.MarkDirty();
}

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

EditCubeCommand::EditCubeCommand(EntityId entity,
                                 Vec3d oldPosition, Vec3d newPosition,
                                 Vec3d oldHalfExtents, Vec3d newHalfExtents,
                                 LevelScene& scene, LevelDocument& document)
    : Entity(entity)
    , OldPosition(oldPosition)
    , NewPosition(newPosition)
    , OldHalfExtents(oldHalfExtents)
    , NewHalfExtents(newHalfExtents)
    , Scene(scene)
    , Document(document)
{
}

void EditCubeCommand::Execute()
{
    if (const Transform3f* t = Scene.TryGetTransform(Entity))
    {
        Transform3f updated = *t;
        updated.Position = NewPosition;
        Scene.SetTransform(Entity, updated);
    }
    Scene.SetCubeHalfExtents(Entity, NewHalfExtents);
    Document.MarkDirty();
}

void EditCubeCommand::Undo()
{
    if (const Transform3f* t = Scene.TryGetTransform(Entity))
    {
        Transform3f updated = *t;
        updated.Position = OldPosition;
        Scene.SetTransform(Entity, updated);
    }
    Scene.SetCubeHalfExtents(Entity, OldHalfExtents);
    Document.MarkDirty();
}

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
        if (const CubePrimitive* cube = Scene.TryGetCube(TargetEntity))
            SavedCube = *cube;
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

    if (SavedCube.has_value())
        RestoredEntity = Scene.CreateCube(SavedTransform->Position, SavedCube->HalfExtents);
    else if (SavedCamera.has_value())
        RestoredEntity = Scene.CreateCamera(SavedTransform->Position);
    else
        return;

    Transform3f restored = *SavedTransform;
    Scene.SetTransform(RestoredEntity, restored);
    Document.MarkDirty();
}
