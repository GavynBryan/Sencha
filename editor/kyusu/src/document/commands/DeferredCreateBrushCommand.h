#pragma once

#include "commands/ICommand.h"
#include "document/EntitySnapshot.h"
#include "selection/ISelectionContext.h"

#include <ecs/EntityId.h>

#include <vector>

class EditorDocument;
class EditorScene;
class SelectionService;

// Makes an already-visible brush creation undoable. The first execution adopts
// the live entity; undo deletes it and redo restores the captured snapshot.
class DeferredCreateBrushCommand : public ICommand
{
public:
    DeferredCreateBrushCommand(EntityId entity, EntitySnapshot snapshot, EditorScene& scene,
                               EditorDocument& document, SelectionService& selection,
                               std::vector<SelectableRef> beforeSelection);

    void Execute() override;
    void Undo() override;

private:
    EntityId Entity;
    EntitySnapshot Snapshot;
    EditorScene& Scene;
    EditorDocument& Document;
    SelectionService& Selection;
    std::vector<SelectableRef> BeforeSelection;
    bool Adopted = false;
};
