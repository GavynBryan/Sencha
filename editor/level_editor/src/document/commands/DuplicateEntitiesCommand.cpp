#include "DuplicateEntitiesCommand.h"

#include "document/EditorScene.h"
#include "selection/SelectableRef.h"
#include "selection/SelectionService.h"

#include <world/registry/Registry.h>

#include <cstdint>
#include <utility>

DuplicateEntitiesCommand::DuplicateEntitiesCommand(std::span<const EntityId> sources,
                                                   std::span<const Transform3f> transforms,
                                                   EditorScene& scene, EditorDocument& document,
                                                   SelectionService& selection,
                                                   DuplicateBranchPolicy branch,
                                                   bool asInstance,
                                                   std::function<void(std::vector<EntitySnapshot>&)> remap)
    : Scene(scene)
    , Document(document)
    , Selection(selection)
    , Sources(sources.begin(), sources.end())
    , Transforms(transforms.begin(), transforms.end())
    , Branch(branch)
    , AsInstance(asInstance)
    , Remap(std::move(remap))
{
}

void DuplicateEntitiesCommand::Execute()
{
    // Captured once: the records are id-independent, so they stay valid across
    // undo and redo even though each Execute mints fresh copies.
    if (!Captured)
    {
        PreviousSelection = Selection.GetSnapshot();
        Copy = CaptureBranchCopy(Document, Sources, Branch, Remap);
        Captured = true;
    }

    Created = RestoreBranchCopy(Document, Copy, Transforms, AsInstance);
    std::vector<SelectableRef> selection;
    const RegistryId registry = Scene.GetRegistry().Id;
    for (std::size_t i = 0; i < Created.size(); ++i)
        if (Copy.RootOf[i] != SIZE_MAX)
            selection.push_back(SelectableRef::EntitySelection(registry, Created[i]));
    Selection.SetSelection(std::move(selection));
    Document.MarkDirty();
}

void DuplicateEntitiesCommand::Undo()
{
    // Leaf-up: destroying a parent copy first would trigger the scene's
    // orphan adoption on children that are about to die anyway.
    for (auto it = Created.rbegin(); it != Created.rend(); ++it)
        Scene.DestroyEntity(*it);
    Created.clear();
    Selection.ApplySnapshot(PreviousSelection);
    Document.MarkDirty();
}
