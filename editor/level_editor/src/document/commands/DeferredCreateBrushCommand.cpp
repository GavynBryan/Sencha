#include "DeferredCreateBrushCommand.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "selection/SelectionService.h"

#include <utility>

DeferredCreateBrushCommand::DeferredCreateBrushCommand(
    EntityId entity, EntitySnapshot snapshot, EditorScene& scene, EditorDocument& document,
    SelectionService& selection, std::vector<SelectableRef> beforeSelection)
    : Entity(entity)
    , Snapshot(std::move(snapshot))
    , Scene(scene)
    , Document(document)
    , Selection(selection)
    , BeforeSelection(std::move(beforeSelection))
{
}

void DeferredCreateBrushCommand::Execute()
{
    if (!Adopted)
    {
        Adopted = true;
    }
    else
    {
        Entity = Document.RestoreEntity(Snapshot);
        Selection.SetSelection({ SelectableRef::EntitySelection(Scene.GetRegistry().Id, Entity) });
    }
    Document.MarkDirty();
}

void DeferredCreateBrushCommand::Undo()
{
    Scene.DestroyEntity(Entity);
    Selection.SetSelection(BeforeSelection);
    Document.MarkDirty();
}
