#pragma once

#include "../../commands/ICommand.h"
#include "../LevelDocument.h"

// Moves an entity by changing only its transform — never its geometry. Brush
// moves go through this (not EditBrushCommand) so translating a brush can't
// rebuild/clobber its mesh. (03-brush-representation.md §3: Translate stays on
// the entity transform; the mesh is local-space.)
class MoveEntityCommand : public ICommand
{
public:
    MoveEntityCommand(EntityId entity, Transform3f before, Transform3f after,
                      LevelScene& scene, LevelDocument& document)
        : Entity(entity)
        , Before(before)
        , After(after)
        , Scene(scene)
        , Document(document)
    {
    }

    void Execute() override
    {
        Scene.SetTransform(Entity, After);
        Document.MarkDirty();
    }

    void Undo() override
    {
        Scene.SetTransform(Entity, Before);
        Document.MarkDirty();
    }

private:
    EntityId       Entity;
    Transform3f    Before;
    Transform3f    After;
    LevelScene&    Scene;
    LevelDocument& Document;
};
