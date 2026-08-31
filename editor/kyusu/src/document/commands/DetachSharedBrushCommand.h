#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <memory>

// Detaches a brush entity from its shared mesh: the entity gets its OWN copy
// (a fresh BrushId), so later edits no longer propagate to the other users of
// the mesh. The copy is taken from the LIVE mesh, so every edit made while
// shared is kept. Undo repoints back to the shared id and frees the copy.
// Unrelated to scene instances (BreakSceneInstanceCommand severs a placement);
// this is the brush-mesh-sharing mechanism only, which is why the name says
// brush.
class DetachSharedBrushCommand : public ICommand
{
public:
    DetachSharedBrushCommand(EditorScene& scene, EditorDocument& document,
                         EntityId entity, BrushId sharedId)
        : Scene(scene), Document(document), Entity(entity), SharedId(sharedId) {}

    void Execute() override
    {
        const BrushMesh* shared = Scene.GetBrushMeshStore().Find(SharedId);
        if (shared == nullptr)
            return;
        OwnId = Scene.GetBrushMeshStore().Create(*shared);
        Scene.SetComponent(Entity, BrushComponent{ OwnId });
        Document.MarkDirty();
    }

    void Undo() override
    {
        Scene.SetComponent(Entity, BrushComponent{ SharedId });
        Scene.GetBrushMeshStore().Destroy(OwnId);
        Document.MarkDirty();
    }

private:
    EditorScene& Scene;
    EditorDocument& Document;
    EntityId Entity;
    BrushId SharedId;
    BrushId OwnId;
};

// nullptr when the entity is not an instanced brush (nothing to break from).
[[nodiscard]] inline std::unique_ptr<ICommand> MakeDetachSharedBrushCommand(
    EditorScene& scene, EditorDocument& document, EntityId entity)
{
    const BrushComponent* brush = scene.TryGetBrush(entity);
    if (brush == nullptr || !scene.IsBrushInstanced(entity))
        return nullptr;
    return std::make_unique<DetachSharedBrushCommand>(scene, document, entity, brush->Id);
}
