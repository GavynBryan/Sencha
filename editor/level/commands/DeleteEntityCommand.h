#pragma once

#include "../../commands/ICommand.h"
#include "../LevelDocument.h"

#include <optional>

// Deletes an entity, capturing enough state (transform + brush/camera) to
// recreate it on Undo.
class DeleteEntityCommand : public ICommand
{
public:
    DeleteEntityCommand(EntityId entity, LevelScene& scene, LevelDocument& document);

    void Execute() override;
    void Undo() override;

private:
    EntityId TargetEntity = {};
    LevelScene& Scene;
    LevelDocument& Document;
    std::optional<Transform3f> SavedTransform;
    std::optional<BrushComponent> SavedBrush;
    std::optional<CameraComponent> SavedCamera;
    EntityId RestoredEntity = {};
    bool CapturedState = false;
};
