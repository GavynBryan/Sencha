#pragma once

#include "../../commands/ICommand.h"
#include "../LevelDocument.h"

// Creates a camera entity at a position; Undo destroys it.
class CreateCameraCommand : public ICommand
{
public:
    CreateCameraCommand(Vec3d position, LevelScene& scene, LevelDocument& document);

    void Execute() override;
    void Undo() override;

private:
    Vec3d Position;
    LevelScene& Scene;
    LevelDocument& Document;
    EntityId CreatedEntity = {};
};
