#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <math/geometry/3d/Transform3d.h>

#include <memory>
#include <vector>

// Re-origins the whole scene: every root entity shifts by the negated origin,
// children following through the hierarchy, so the chosen world point becomes
// the scene's origin. What a source authored away from origin needs before it
// places well -- a placement pivots around its source's origin, so the origin
// should sit where the designer expects to grab the thing. One undoable step;
// undo restores every root's captured transform exactly.
class SetSceneOriginCommand : public ICommand
{
public:
    struct Edit
    {
        EntityId Entity;
        Transform3f Before;
        Transform3f After;
    };

    SetSceneOriginCommand(std::vector<Edit> edits, EditorScene& scene,
                          EditorDocument& document)
        : Edits(std::move(edits))
        , Scene(scene)
        , Document(document)
    {
    }

    void Execute() override
    {
        for (const Edit& edit : Edits)
            Scene.SetTransform(edit.Entity, edit.After);
        Document.MarkDirty();
    }

    void Undo() override
    {
        for (const Edit& edit : Edits)
            Scene.SetTransform(edit.Entity, edit.Before);
        Document.MarkDirty();
    }

private:
    std::vector<Edit> Edits;
    EditorScene& Scene;
    EditorDocument& Document;
};

// Builds the re-origin to `worldOrigin`, or nullptr when there is nothing to
// move or the origin is already there.
[[nodiscard]] inline std::unique_ptr<ICommand> MakeSetSceneOriginCommand(
    EditorScene& scene, EditorDocument& document, Vec3d worldOrigin)
{
    if (worldOrigin.SqrMagnitude() <= 1.0e-10f)
        return nullptr;

    std::vector<SetSceneOriginCommand::Edit> edits;
    for (EntityId entity : scene.GetAllEntities())
    {
        if (scene.GetParent(entity).IsValid())
            continue; // children follow their roots
        const Transform3f* local = scene.TryGetLocalTransform(entity);
        if (local == nullptr)
            continue;
        Transform3f moved = *local;
        moved.Position = moved.Position - worldOrigin;
        edits.push_back({ entity, *local, moved });
    }
    if (edits.empty())
        return nullptr;
    return std::make_unique<SetSceneOriginCommand>(std::move(edits), scene, document);
}
