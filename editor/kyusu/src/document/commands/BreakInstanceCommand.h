#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <memory>

// Breaks a brush entity out of its instance group: the entity gets its OWN copy
// of the shared mesh (a fresh BrushId), so later edits no longer propagate to
// the other instances. The copy is taken from the LIVE mesh, so every edit made
// while instanced is kept. Undo repoints back to the shared id and frees the
// copy.
class BreakInstanceCommand : public ICommand
{
public:
    BreakInstanceCommand(EditorScene& scene, EditorDocument& document,
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
[[nodiscard]] inline std::unique_ptr<ICommand> MakeBreakInstanceCommand(
    EditorScene& scene, EditorDocument& document, EntityId entity)
{
    const BrushComponent* brush = scene.TryGetBrush(entity);
    if (brush == nullptr || !scene.IsBrushInstanced(entity))
        return nullptr;
    return std::make_unique<BreakInstanceCommand>(scene, document, entity, brush->Id);
}
