#pragma once

#include "commands/ICommand.h"
#include "document/EditorDocument.h"

#include <core/json/JsonValue.h>
#include <world/serialization/IComponentSerializer.h>

// Removes a component from an entity, undoably. Capture/restore go through the
// serializer round-trip (asset fields persist as stable paths, not handles), so
// the typed Remove releases asset refs and Undo's Load re-resolves them: refcount-
// balanced for any component, with no type named here. The entity id is stable
// across remove/undo, so redo just re-captures the identical state and removes.
class RawComponentRemoveCommand : public ICommand
{
public:
    RawComponentRemoveCommand(EntityId entity, IComponentSerializer& serializer,
                              EditorScene& scene, EditorDocument& document)
        : Entity(entity)
        , Serializer(serializer)
        , Scene(scene)
        , Document(document)
    {
    }

    void Execute() override
    {
        Snapshot = Document.CaptureComponent(Entity, Serializer);
        Serializer.Remove(Entity, Scene.GetRegistry());
        Document.MarkDirty();
    }

    void Undo() override
    {
        Document.RestoreComponent(Entity, Serializer, Snapshot);
        Document.MarkDirty();
    }

private:
    EntityId              Entity;
    IComponentSerializer& Serializer;
    EditorScene&           Scene;
    EditorDocument&        Document;
    JsonValue             Snapshot;
};
