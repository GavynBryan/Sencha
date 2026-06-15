#pragma once

#include "../../commands/ICommand.h"
#include "../LevelDocument.h"
#include "../brush/BrushMesh.h"

#include <utility>

// Replaces a brush's mesh wholesale, undoably, by storing the before/after
// meshes. The general wrapper for every mesh-edit verb (extrude/clip/delete/…) —
// value semantics make undo trivial. (03-brush-representation.md §3)
class EditBrushMeshCommand : public ICommand
{
public:
    EditBrushMeshCommand(EntityId entity, BrushMesh before, BrushMesh after,
                         LevelScene& scene, LevelDocument& document)
        : Entity(entity)
        , Before(std::move(before))
        , After(std::move(after))
        , Scene(scene)
        , Document(document)
    {
    }

    void Execute() override
    {
        Scene.SetBrushMesh(Entity, After);
        Document.MarkDirty();
    }

    void Undo() override
    {
        Scene.SetBrushMesh(Entity, Before);
        Document.MarkDirty();
    }

private:
    EntityId       Entity;
    BrushMesh      Before;
    BrushMesh      After;
    LevelScene&    Scene;
    LevelDocument& Document;
};
