#include "DuplicateEntitiesCommand.h"

#include "../EditorScene.h"
#include "../../selection/SelectableRef.h"
#include "../../selection/SelectionService.h"

#include <world/registry/Registry.h>

DuplicateEntitiesCommand::DuplicateEntitiesCommand(std::span<const EntityId> sources,
                                                   std::span<const Transform3f> transforms,
                                                   EditorScene& scene, EditorDocument& document,
                                                   SelectionService& selection)
    : Scene(scene)
    , Document(document)
    , Selection(selection)
    , Sources(sources.begin(), sources.end())
    , Transforms(transforms.begin(), transforms.end())
{
}

void DuplicateEntitiesCommand::Execute()
{
    // Capture once: snapshots are id-independent, so they stay valid across
    // undo/redo even though each Execute mints fresh copies under new ids.
    if (!Captured)
    {
        PreviousSelection = Selection.GetSnapshot();
        Snapshots.reserve(Sources.size());
        for (EntityId source : Sources)
            Snapshots.push_back(Document.CaptureEntity(source));
        Captured = true;
    }

    Created.clear();
    Created.reserve(Snapshots.size());
    std::vector<SelectableRef> selection;
    selection.reserve(Snapshots.size());
    const RegistryId registry = Scene.GetRegistry().Id;
    for (std::size_t i = 0; i < Snapshots.size(); ++i)
    {
        const EntityId copy = Document.RestoreEntity(Snapshots[i], /*freshMesh*/ true);
        if (i < Transforms.size())
            Scene.SetTransform(copy, Transforms[i]);
        Created.push_back(copy);
        selection.push_back(SelectableRef::EntitySelection(registry, copy));
    }

    Selection.SetSelection(std::move(selection));
    Document.MarkDirty();
}

void DuplicateEntitiesCommand::Undo()
{
    for (EntityId copy : Created)
        Scene.DestroyEntity(copy);
    Created.clear();
    Selection.ApplySnapshot(PreviousSelection);
    Document.MarkDirty();
}
