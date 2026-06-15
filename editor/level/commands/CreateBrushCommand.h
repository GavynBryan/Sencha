#pragma once

#include "../../commands/ICommand.h"
#include "../LevelDocument.h"

// Creates a brush entity at a position/size; Undo destroys it. The created
// entity id is exposed so a follow-up (e.g. select-on-create) can reference it.
class CreateBrushCommand : public ICommand
{
public:
    CreateBrushCommand(Vec3d position, Vec3d halfExtents, LevelScene& scene, LevelDocument& document);

    void Execute() override;
    void Undo() override;

    [[nodiscard]] EntityId GetCreatedEntity() const { return CreatedEntity; }

private:
    Vec3d Position;
    Vec3d HalfExtents;
    LevelScene& Scene;
    LevelDocument& Document;
    EntityId CreatedEntity = {};
};
