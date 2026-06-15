#pragma once

#include "../../commands/ICommand.h"
#include "../LevelDocument.h"

// Edits a brush's position and half-extents, with before/after snapshots for undo.
class EditBrushCommand : public ICommand
{
public:
    EditBrushCommand(EntityId entity,
                     Vec3d oldPosition, Vec3d newPosition,
                     Vec3d oldHalfExtents, Vec3d newHalfExtents,
                     LevelScene& scene, LevelDocument& document);

    void Execute() override;
    void Undo() override;

private:
    EntityId Entity;
    Vec3d OldPosition;
    Vec3d NewPosition;
    Vec3d OldHalfExtents;
    Vec3d NewHalfExtents;
    LevelScene& Scene;
    LevelDocument& Document;
};
