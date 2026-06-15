#include "EditBrushCommand.h"

EditBrushCommand::EditBrushCommand(EntityId entity,
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

void EditBrushCommand::Execute()
{
    if (const Transform3f* t = Scene.TryGetTransform(Entity))
    {
        Transform3f updated = *t;
        updated.Position = NewPosition;
        Scene.SetTransform(Entity, updated);
    }
    Scene.SetBrushHalfExtents(Entity, NewHalfExtents);
    Document.MarkDirty();
}

void EditBrushCommand::Undo()
{
    if (const Transform3f* t = Scene.TryGetTransform(Entity))
    {
        Transform3f updated = *t;
        updated.Position = OldPosition;
        Scene.SetTransform(Entity, updated);
    }
    Scene.SetBrushHalfExtents(Entity, OldHalfExtents);
    Document.MarkDirty();
}
