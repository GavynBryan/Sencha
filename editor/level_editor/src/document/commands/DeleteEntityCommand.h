#pragma once

#include "commands/ICommand.h"
#include "document/EntitySnapshot.h"
#include "document/EditorDocument.h"

#include <ecs/EntityId.h>

#include <memory>
#include <span>

class SelectionService;

// Deletes an entity and makes it undoable by capturing its full state (every
// registered component via the serializer registry, plus the brush sidecar mesh
// and view flags) on first Execute. Undo recreates the entity and retargets to
// the new id, so a subsequent redo deletes the right entity.
class DeleteEntityCommand : public ICommand
{
public:
    DeleteEntityCommand(EntityId entity, EditorScene& scene, EditorDocument& document);

    void Execute() override;
    void Undo() override;

private:
    EntityId       Current; // the live id to delete; retargeted by Undo to the restored id
    EditorScene&    Scene;
    EditorDocument& Document;
    EntitySnapshot Snapshot;
    bool           Captured = false;
};

// Builds one undoable step that clears the selection (so a deleted entity's
// handle is not left selected) then deletes each given entity. Mirrors how the
// create path pairs a CreateEntityCommand with a SelectCommand.
[[nodiscard]] std::unique_ptr<ICommand> MakeDeleteEntitiesCommand(
    std::span<const EntityId> entities, EditorScene& scene, EditorDocument& document,
    SelectionService& selection);
